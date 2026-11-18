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

#ifndef INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_STORAGE_URI_H_
#define INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_STORAGE_URI_H_

// Internal URI helpers for v1 `GeometryStorageRefs`.
//
// The URI schema is an internal implementation detail and is subject to change.
// Users should not rely on this schema.

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"

namespace intrinsic::geo {

// Storage backend scheme parsed from a `GeometryStorageRefs` URI.
enum class GeometryStorageUriScheme {
  kCas,
  kKvStore,
};

// Resolves the backend scheme for `refs` by validating `exact_geometry_ref`
// and, when non-empty, `renderable_ref`. Returns `INVALID_ARGUMENT` when a
// checked ref lacks `<scheme>://` or the schemes do not match, and
// `FAILED_PRECONDITION` when a scheme is unsupported.
absl::StatusOr<GeometryStorageUriScheme> ResolveGeometryStorageUriScheme(
    const intrinsic_proto::geometry::v1::GeometryStorageRefs& refs);

// Formats a KeyValueStore geometry ref URI for `storage_prefix` and `key`.
// This URI schema is an internal implementation detail and subject to change;
// users should not rely on this schema.
std::string FormatKvStoreGeometryUri(absl::string_view storage_prefix,
                                     absl::string_view key);

// Strips the KeyValueStore URI prefix and `storage_prefix` from `uri` and
// returns `<key>`, or `std::nullopt` if `uri` does not start with that prefix.
std::optional<absl::string_view> ParseKvStoreGeometryKey(
    absl::string_view uri, absl::string_view storage_prefix);

}  // namespace intrinsic::geo

#endif  // INTRINSIC_GEOMETRY_STORAGE_GEOMETRY_STORAGE_URI_H_
