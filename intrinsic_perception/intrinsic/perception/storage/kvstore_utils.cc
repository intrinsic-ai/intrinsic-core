// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "intrinsic/perception/storage/kvstore_utils.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"
#include "intrinsic/perception/storage/kvstore_factory.h"
#include "intrinsic/perception/storage/kvstore_options.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/time/deadline_timeout.h"

namespace intrinsic::perception {
namespace {

// Shared bounded retry window and exponential backoff schedule for waiting on
// Zenoh router convergence — both `DELETE` tombstone propagation before
// `kFirstReply`-verified writes and transient `NotFound` routing races in
// freshly opened sessions (b/577138515).
constexpr absl::Duration kRetryWindow = absl::Seconds(2);
constexpr absl::Duration kRetryDelayMin = absl::Milliseconds(10);
constexpr absl::Duration kRetryDelayMax = absl::Milliseconds(250);
constexpr double kRetryDelayFactor = 2.0;

constexpr absl::Duration kDeletePollTimeout = absl::Milliseconds(100);

}  // namespace

namespace kvstore_utils_internal {

absl::Status RetryOnTransientKeyNotFound(
    absl::string_view key, absl::Time deadline,
    absl::FunctionRef<absl::Status(absl::Duration)> try_fetch) {
  const absl::Time retry_deadline =
      std::min(deadline, absl::Now() + kRetryWindow);
  absl::Duration delay = kRetryDelayMin;
  while (true) {
    const absl::Status status = try_fetch(ToTimeout(deadline));
    if (!absl::IsNotFound(status) || absl::Now() + delay > retry_deadline) {
      INTR_RETURN_IF_ERROR(status).EmitStackTrace()
          << "Failed to get key '" << key << "' from KeyValueStore.";
      return absl::OkStatus();
    }
    LOG(WARNING) << "Key '" << key
                 << "' not found in KeyValueStore yet, retrying in " << delay
                 << ".";
    absl::SleepFor(delay);
    delay = std::min(delay * kRetryDelayFactor, kRetryDelayMax);
  }
}

}  // namespace kvstore_utils_internal

absl::StatusOr<absl_nonnull std::shared_ptr<KeyValueStore>>
ResolveKeyValueStore(absl::string_view store,
                     std::optional<KeyValueStoreFactoryRef> kvstore_factory) {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<KeyValueStore> kvstore,
                        kvstore_factory.has_value()
                            ? (*kvstore_factory)(store)
                            : DefaultKeyValueStoreFactory()(store));
  if (kvstore == nullptr) {
    return absl::InternalError(
        "KeyValueStoreFactory returned a null KeyValueStore.");
  }
  return kvstore;
}

absl::Status DeleteKeyIfPresent(KeyValueStore& kvstore, absl::string_view key) {
  const absl::Status status = kvstore.Delete(key);
  if (absl::IsNotFound(status)) {
    return absl::OkStatus();
  }
  return status;
}

absl::StatusOr<int> WaitForKeyDeletion(
    absl::string_view key, absl::Time deadline,
    absl::FunctionRef<absl::Status(absl::Duration)> probe_get) {
  const absl::Time delete_deadline =
      std::min(deadline, absl::Now() + kRetryWindow);
  absl::Duration delay = kRetryDelayMin;
  for (int attempts = 1;; ++attempts) {
    const absl::Duration remaining = ToTimeout(delete_deadline);
    if (remaining <= absl::ZeroDuration()) {
      return absl::DeadlineExceededError(absl::StrCat(
          "Timed out waiting for key deletion to propagate: ", key));
    }
    const absl::Duration poll_timeout = std::min(kDeletePollTimeout, remaining);
    const absl::Status get_status = probe_get(poll_timeout);
    if (absl::IsNotFound(get_status)) {
      return attempts;
    }
    if (!get_status.ok() && !absl::IsDeadlineExceeded(get_status)) {
      return get_status;
    }
    if (absl::Now() + delay >= delete_deadline) {
      return absl::DeadlineExceededError(absl::StrCat(
          "Timed out waiting for key deletion to propagate: ", key));
    }
    absl::SleepFor(delay);
    delay = std::min(delay * kRetryDelayFactor, kRetryDelayMax);
  }
}

absl::StatusOr<ImmutableBuffer> GetRawBufferWithRetry(
    const intrinsic_proto::kvstore::StorageLocation& location,
    const KeyValueStoreReadOptions& options) {
  if (location.key().empty()) {
    return absl::InvalidArgumentError("StorageLocation key must not be empty.");
  }
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::DeadlineExceededError(
        absl::StrCat("Timed out before reading raw key: ", location.key()));
  }
  const absl::Time deadline = ToDeadline(options.timeout);
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<KeyValueStore> kvstore,
      ResolveKeyValueStore(location.store(), options.kvstore_factory));
  return GetWithTransientKeyNotFoundRetry<ImmutableBuffer>(
      location.key(), deadline, [&](absl::Duration remaining_timeout) {
        return kvstore->GetRaw(location.key(), remaining_timeout);
      });
}

absl::Status SetRawBufferWithVerification(
    MutableBuffer&& buffer, const KeyValueStoreWriteOptions& options) {
  if (!options.storage_location.has_value() ||
      options.storage_location->key().empty()) {
    return absl::InvalidArgumentError(
        "KeyValueStoreWriteOptions::storage_location key must not be empty.");
  }
  if (!buffer.valid()) {
    return absl::InvalidArgumentError("Cannot set an invalid MutableBuffer.");
  }
  const intrinsic_proto::kvstore::StorageLocation& location =
      *options.storage_location;
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::DeadlineExceededError(
        absl::StrCat("Timed out before writing raw key: ", location.key()));
  }
  const absl::Time deadline = ToDeadline(options.timeout);
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<KeyValueStore> kvstore,
      ResolveKeyValueStore(location.store(), options.kvstore_factory));
  if (options.delete_existing) {
    INTR_RETURN_IF_ERROR(DeleteKeyIfPresent(*kvstore, location.key()));
  }
  INTR_RETURN_IF_ERROR(
      WaitForKeyDeletion(
          location.key(), deadline,
          [&](absl::Duration poll_timeout) {
            return kvstore->GetRaw(location.key(), poll_timeout).status();
          })
          .status());

  const absl::Duration remaining_timeout = ToTimeout(deadline);
  if (remaining_timeout <= absl::ZeroDuration()) {
    return absl::DeadlineExceededError(
        absl::StrCat("Timed out before writing raw key: ", location.key()));
  }
  const KeyValueStore::SetWithVerificationOptions set_options{
      .mode = KeyValueStore::SetWithVerificationOptions::VerificationMode::
          kFirstReply,
      .timeout = remaining_timeout,
  };
  return kvstore->SetRawWithVerification(location.key(), std::move(buffer),
                                         set_options);
}

}  // namespace intrinsic::perception
