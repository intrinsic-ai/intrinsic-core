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

#include "intrinsic/scene/sdf/sdf_path_resolver.h"

#include <string>
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "ortools/base/path.h"

// TODO(b/418782734): Remove this flag once we have a more sustainable solution
// for resolving uris in insrc.
ABSL_FLAG(std::string, additional_sdf_assets, "",
          "A '|' separated list of additional sdf asset files. The resolver "
          "resolves model:// uris and plain file uris. For model:// uris, the "
          "resolver attempts these paths before searching the runfiles "
          "directory. For plain file uris, if a path provided ends with the "
          "given uri, the resolver will return that path.");

namespace intrinsic {
namespace sdf {

absl::StatusOr<std::string> SdfPathResolver(const std::string& uri) {
  std::string_view uri_view(uri);
  if (absl::ConsumePrefix(&uri_view, "model://")) {
    std::string path(uri_view);
    // sun and ground_plane are special built in models for gazebo.
    if (path == "sun" || path == "ground_plane") {
      path = file::JoinPath("intrinsic_simulation/models/gazebo", path);
    }

    for (absl::string_view additional_asset :
         absl::StrSplit(absl::GetFlag(FLAGS_additional_sdf_assets), '|')) {
      // Some model:// paths that are direct file references.
      if (absl::EndsWith(additional_asset, path)) {
        return std::string(additional_asset);
      }
      // Attempt to find a model.config file in the additional_sdf_assets flag.
      // This matches <uri>'s that specify a model directory.
      if (absl::ConsumeSuffix(&additional_asset, "model.config")) {
        if (absl::EndsWith(additional_asset, path)) {
          return std::string(additional_asset);
        }
        // Remove the trailing slash to allow for model://path/without/slash
        absl::ConsumeSuffix(&additional_asset, "/");
        if (absl::EndsWith(additional_asset, path)) {
          return std::string(additional_asset);
        }
      }
    }

    return PathResolver::ResolveRunfilesPath(path);
  } else if (absl::ConsumePrefix(&uri_view, "testfile://")) {
    return PathResolver::ResolveRunfilesPathForTest(uri_view);
  } else if (absl::ConsumePrefix(&uri_view, "bypass://")) {
    return std::string(uri_view);
  } else if (absl::ConsumePrefix(&uri_view, "external://")) {
    return absl::InvalidArgumentError(
        "external:// is no longer supported, use model:// instead.");
  } else {
    for (absl::string_view additional_asset :
         absl::StrSplit(absl::GetFlag(FLAGS_additional_sdf_assets), '|')) {
      if (absl::EndsWith(additional_asset, uri)) {
        return std::string(additional_asset);
      }
    }
    return uri;
  }
}

}  // namespace sdf
}  // namespace intrinsic
