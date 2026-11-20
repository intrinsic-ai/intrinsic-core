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

#include "intrinsic/platform/pubsub/fake_kvstore.h"

#include <string>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/buffer_factory.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_helpers.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

absl::Status FakeKeyValueStore::Set(absl::string_view key,
                                    const google::protobuf::Any& value) {
  INTR_RETURN_IF_ERROR(ValidZenohKey(key));

  absl::MutexLock lock(&mutex_);
  store_[std::string(key)] = value;
  return absl::OkStatus();
}

absl::Status FakeKeyValueStore::SetWithVerification(
    absl::string_view key, const google::protobuf::Any& value,
    const SetWithVerificationOptions& options) {
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "SetWithVerificationOptions::timeout must be positive");
  }
  return Set(key, value);
}

absl::Status FakeKeyValueStore::SetRaw(absl::string_view key,
                                       ImmutableBuffer value) {
  if (!value.valid()) {
    return absl::InvalidArgumentError("Cannot set an invalid ImmutableBuffer");
  }
  INTR_RETURN_IF_ERROR(ValidZenohKey(key));
  // Preserve the 256-byte alignment contract of `GetRaw`, `GetAllRaw`, and
  // `CreateRawSubscription` when storing an unaligned `Slice()`.
  if (value.alignment() < ImmutableBuffer::kAlignment) {
    INTR_ASSIGN_OR_RETURN(value,
                          CopyToImmutableBuffer(value.data(), value.size()));
  }

  absl::MutexLock lock(&mutex_);
  store_[std::string(key)] = value;
  return absl::OkStatus();
}

absl::Status FakeKeyValueStore::SetRawWithVerification(
    absl::string_view key, ImmutableBuffer value,
    const SetWithVerificationOptions& options) {
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "SetWithVerificationOptions::timeout must be positive");
  }
  return SetRaw(key, std::move(value));
}

absl::StatusOr<ImmutableBuffer> FakeKeyValueStore::GetRaw(
    absl::string_view key, absl::Duration timeout) {
  return GetTyped<ImmutableBuffer>(
      key, timeout, "' stores a Protobuf message, not raw bytes");
}

absl::Status FakeKeyValueStore::Delete(absl::string_view key) {
  INTR_RETURN_IF_ERROR(ValidZenohKey(key));

  absl::MutexLock lock(&mutex_);
  auto it = store_.find(key);
  if (it == store_.end()) {
    return absl::NotFoundError(absl::StrCat("Key not found: ", key));
  }
  store_.erase(it);
  return absl::OkStatus();
}

absl::StatusOr<google::protobuf::Any> FakeKeyValueStore::GetAny(
    absl::string_view key, absl::Duration timeout) {
  return GetTyped<google::protobuf::Any>(
      key, timeout, "' stores raw bytes, not a Protobuf message");
}

template <typename T>
absl::StatusOr<T> FakeKeyValueStore::GetTyped(
    absl::string_view key, absl::Duration timeout,
    absl::string_view mismatch_error) {
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Timeout must be zero or positive");
  }
  INTR_RETURN_IF_ERROR(ValidZenohKey(key));

  absl::MutexLock lock(&mutex_);
  auto it = store_.find(key);
  if (it == store_.end()) {
    return absl::NotFoundError(absl::StrCat("Key not found: ", key));
  }
  if (!std::holds_alternative<T>(it->second)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Key '", key_prefix(), "/", key, mismatch_error));
  }
  return std::get<T>(it->second);
}

}  // namespace intrinsic
