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

#ifndef INTRINSIC_PERCEPTION_STORAGE_KVSTORE_UTILS_H_
#define INTRINSIC_PERCEPTION_STORAGE_KVSTORE_UTILS_H_

#include <memory>
#include <optional>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"
#include "intrinsic/perception/storage/kvstore_factory.h"
#include "intrinsic/perception/storage/kvstore_options.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::perception {

namespace kvstore_utils_internal {

absl::Status RetryOnTransientKeyNotFound(
    absl::string_view key, absl::Time deadline,
    absl::FunctionRef<absl::Status(absl::Duration)> try_fetch);

}  // namespace kvstore_utils_internal

// Resolves a non-null `KeyValueStore` for `store`, falling back to
// `DefaultKeyValueStoreFactory()` when `kvstore_factory` is `std::nullopt`.
absl::StatusOr<absl_nonnull std::shared_ptr<KeyValueStore>>
ResolveKeyValueStore(absl::string_view store,
                     std::optional<KeyValueStoreFactoryRef> kvstore_factory);

// Deletes `key` from `kvstore`, treating `NotFound` as success.
absl::Status DeleteKeyIfPresent(KeyValueStore& kvstore, absl::string_view key);

// Polls `probe_get(poll_timeout)` with exponential backoff until it returns
// `NotFound`, bounded by a short deletion-propagation window and `deadline`, so
// a subsequent `kFirstReply` verification cannot match stale data. Returns the
// number of poll attempts performed.
absl::StatusOr<int> WaitForKeyDeletion(
    absl::string_view key, absl::Time deadline,
    absl::FunctionRef<absl::Status(absl::Duration)> probe_get);

// Retries `fetch_fn` on transient `NotFound` replies for `key` (Zenoh routing
// race in a freshly opened session, b/577138515) within a short bounded retry
// window capped by `deadline`, returning the fetched value on success.
template <typename T>
absl::StatusOr<T> GetWithTransientKeyNotFoundRetry(
    absl::string_view key, absl::Time deadline,
    absl::FunctionRef<absl::StatusOr<T>(absl::Duration)> fetch_fn) {
  std::optional<T> result;
  INTR_RETURN_IF_ERROR(kvstore_utils_internal::RetryOnTransientKeyNotFound(
      key, deadline, [&](absl::Duration remaining_timeout) -> absl::Status {
        absl::StatusOr<T> value = fetch_fn(remaining_timeout);
        if (!value.ok()) {
          return value.status();
        }
        result.emplace(*std::move(value));
        return absl::OkStatus();
      }));
  return *std::move(result);
}

// Fetches a raw byte buffer from `location` in KeyValueStore, retrying
// transient `NotFound` replies within a short bounded window (capped by
// `options.timeout`). Uses `DefaultKeyValueStoreFactory()` lazily when
// `options.kvstore_factory` is `std::nullopt`.
absl::StatusOr<ImmutableBuffer> GetRawBufferWithRetry(
    const intrinsic_proto::kvstore::StorageLocation& location,
    const KeyValueStoreReadOptions& options = {});

// Writes `buffer` with `kFirstReply` verification to
// `*options.storage_location` (which must be populated with a non-empty `key`).
// - When `options.delete_existing` is `true` (default), deletes any prior entry
//   at `*options.storage_location` first.
// - When `options.delete_existing` is `false` (for batch callers that already
//   issued an upfront wildcard `Delete`, such as `Delete("<base_key>/*")`),
//   skips issuing a redundant per-key `Delete`, but still polls
//   `WaitForKeyDeletion` to ensure the prior deletion has propagated before
//   `kFirstReply` verification runs (returning `DeadlineExceededError` if an
//   entry remains present at `*options.storage_location`).
// Uses `DefaultKeyValueStoreFactory()` lazily when `options.kvstore_factory` is
// `std::nullopt`.
absl::Status SetRawBufferWithVerification(
    MutableBuffer&& buffer, const KeyValueStoreWriteOptions& options);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_STORAGE_KVSTORE_UTILS_H_
