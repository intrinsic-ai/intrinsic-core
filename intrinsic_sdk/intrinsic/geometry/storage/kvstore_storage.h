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

#ifndef INTRINSIC_GEOMETRY_STORAGE_KVSTORE_STORAGE_H_
#define INTRINSIC_GEOMETRY_STORAGE_KVSTORE_STORAGE_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/platform/pubsub/kvstore.h"

namespace intrinsic::geo {

// Returns a V1-only GeometryLibrary backed by `kv_store` (V0 methods return
// `UNIMPLEMENTED`). Stored references use `intkv://<key_prefix>/<key>`, where
// `<key_prefix>` is `kv_store->key_prefix()`. Returns `INVALID_ARGUMENT` if
// `kv_store` is null, `kv_store->key_prefix()` is empty or contains Zenoh
// reserved characters (`/*?#[]$`), or either timeout is non-positive.
absl::StatusOr<std::unique_ptr<GeometryLibrary>> GetKvStoreGeometryLibrary(
    std::shared_ptr<KeyValueStore> kv_store,
    absl::Duration read_timeout = kDefaultGetTimeout,
    KeyValueStore::SetWithVerificationOptions write_options = {});

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_STORAGE_KVSTORE_STORAGE_H_
