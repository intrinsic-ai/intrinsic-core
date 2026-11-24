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

#ifndef INTRINSIC_PERCEPTION_STORAGE_KVSTORE_OPTIONS_H_
#define INTRINSIC_PERCEPTION_STORAGE_KVSTORE_OPTIONS_H_

#include <optional>

#include "absl/time/time.h"
#include "intrinsic/perception/storage/kvstore_factory.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"

namespace intrinsic::perception {

inline constexpr absl::Duration kDefaultKeyValueStoreTimeout = absl::Seconds(5);

// Options controlling external buffer reads when converting protos
// (`FromProto`) that may carry `ImageBuffer::buffer_storage_location` instead
// of inline `data`.
struct KeyValueStoreReadOptions {
  // Optional non-owning factory for opening KeyValueStores. Because
  // `KeyValueStoreFactoryRef` is non-owning, the referenced callable must
  // outlive this options struct (prefer inline call-site initialization or a
  // named lvalue callable; do not bind a temporary to a named options
  // variable). When `std::nullopt`, a PubSub-backed factory is initialized
  // lazily only if an `ImageBuffer` uses `buffer_storage_location`.
  std::optional<KeyValueStoreFactoryRef> kvstore_factory;
  // Overall timeout for fetching external image buffers from KeyValueStore.
  absl::Duration timeout = kDefaultKeyValueStoreTimeout;
};

// Options controlling external buffer writes when converting images to protos
// (`ToProto`). When `storage_location` is set, raw image buffer bytes are
// written to KeyValueStore and `ImageBuffer::buffer_storage_location` is
// populated instead of inline `data`.
struct KeyValueStoreWriteOptions {
  // Optional non-owning factory for opening KeyValueStores. Because
  // `KeyValueStoreFactoryRef` is non-owning, the referenced callable must
  // outlive this options struct (prefer inline call-site initialization or a
  // named lvalue callable; do not bind a temporary to a named options
  // variable). When `std::nullopt`, a PubSub-backed factory is initialized
  // lazily when `storage_location` is set.
  std::optional<KeyValueStoreFactoryRef> kvstore_factory;
  // Target storage location for external raw buffer storage (`std::nullopt`
  // keeps inline `ImageBuffer::data`).
  // - For single-image `ToProto` (`Image`, `SensorImage`), `key` is the exact
  //   KeyValueStore key for that buffer.
  // - For `CaptureResult` `ToProto`, `key` must contain only the base key
  //   prefix (`<base_key>`); each non-empty sensor buffer is written to
  //   `<base_key>/<sensor_id>`.
  std::optional<intrinsic_proto::kvstore::StorageLocation> storage_location;
  // Overall timeout for deleting stale keys and verifying raw buffer writes.
  absl::Duration timeout = kDefaultKeyValueStoreTimeout;
  // Whether to issue a per-key `Delete` before writing.
  // - When `true` (default), deletes any existing entry at `storage_location`
  //   first.
  // - When `false` (for batch callers that have already issued an upfront
  //   wildcard `Delete`, such as `Delete("<base_key>/*")`), skips the redundant
  //   per-key `Delete` while still polling until any prior entry disappears so
  //   `kFirstReply` verification cannot match stale data (returning
  //   `DeadlineExceededError` if an entry remains present).
  bool delete_existing = true;
};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_STORAGE_KVSTORE_OPTIONS_H_
