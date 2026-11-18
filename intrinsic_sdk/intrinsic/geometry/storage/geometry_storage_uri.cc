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

#include "intrinsic/geometry/storage/geometry_storage_uri.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::geo {

using ::intrinsic_proto::geometry::v1::GeometryStorageRefs;

namespace {

constexpr absl::string_view kCasUriScheme = "intcas";
constexpr absl::string_view kKvStoreUriScheme = "intkv";
constexpr absl::string_view kUriSchemeDelimiter = "://";

// Extracts and validates the `<scheme>` prefix before `"://"` from `uri`,
// returning `INVALID_ARGUMENT` when `"://"` is missing and
// `FAILED_PRECONDITION` for unsupported schemes.
absl::StatusOr<GeometryStorageUriScheme> ExtractGeometryStorageUriScheme(
    absl::string_view uri) {
  if (!absl::StrContains(uri, kUriSchemeDelimiter)) {
    return absl::InvalidArgumentError(
        absl::StrCat("'", uri, "' lacks '<scheme>://'."));
  }
  const std::pair<absl::string_view, absl::string_view> parts =
      absl::StrSplit(uri, absl::MaxSplits(kUriSchemeDelimiter, 1));
  const absl::string_view scheme = parts.first;
  if (scheme == kCasUriScheme) {
    return GeometryStorageUriScheme::kCas;
  }
  if (scheme == kKvStoreUriScheme) {
    return GeometryStorageUriScheme::kKvStore;
  }
  return absl::FailedPreconditionError(
      absl::StrCat("Unsupported URI scheme '", scheme, "'. Expected '",
                   kCasUriScheme, "' or '", kKvStoreUriScheme, "'."));
}

}  // namespace

absl::StatusOr<GeometryStorageUriScheme> ResolveGeometryStorageUriScheme(
    const GeometryStorageRefs& refs) {
  INTR_ASSIGN_OR_RETURN(
      const GeometryStorageUriScheme scheme,
      ExtractGeometryStorageUriScheme(refs.exact_geometry_ref()),
      _.SetPrepend() << "exact_geometry_ref: ");
  if (!refs.renderable_ref().empty()) {
    INTR_ASSIGN_OR_RETURN(
        const GeometryStorageUriScheme renderable_scheme,
        ExtractGeometryStorageUriScheme(refs.renderable_ref()),
        _.SetPrepend() << "renderable_ref: ");
    if (renderable_scheme != scheme) {
      return absl::InvalidArgumentError(
          absl::StrCat("Mismatched schemes between exact_geometry_ref ('",
                       refs.exact_geometry_ref(), "') and renderable_ref ('",
                       refs.renderable_ref(), "')."));
    }
  }
  return scheme;
}

// Formats a KeyValueStore geometry ref URI as
// `"intkv://<storage_prefix>/<key>"`.
std::string FormatKvStoreGeometryUri(absl::string_view storage_prefix,
                                     absl::string_view key) {
  return absl::StrCat(kKvStoreUriScheme, kUriSchemeDelimiter, storage_prefix,
                      "/", key);
}

// Strips `"intkv://<storage_prefix>/"` from `uri` and returns `<key>`, or
// `std::nullopt` if `uri` does not start with that prefix.
std::optional<absl::string_view> ParseKvStoreGeometryKey(
    absl::string_view uri, absl::string_view storage_prefix) {
  if (!absl::ConsumePrefix(&uri, kKvStoreUriScheme) ||
      !absl::ConsumePrefix(&uri, kUriSchemeDelimiter) ||
      !absl::ConsumePrefix(&uri, storage_prefix) ||
      !absl::ConsumePrefix(&uri, "/")) {
    return std::nullopt;
  }
  return uri;
}

}  // namespace intrinsic::geo
