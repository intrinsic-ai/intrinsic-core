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

#include "intrinsic/scene/sdf/scene_object_from_zipped_sdf.h"

#include <filesystem>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/sdf/scene_object_from_sdf.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/util/archive/unzip.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"
#include "ortools/base/temp_path.h"

namespace intrinsic {
namespace scene_object {

namespace fs = std::filesystem;

namespace {
// Resolves sdf uri fields relative to root_dir. This is useful for self
// contained sdf packages.
constexpr char kDefaultModelFileName[] = "model.sdf";
constexpr absl::string_view kDirectoriesToSkip[] = {"__MACOSX"};

::intrinsic::sdf::UriResolver RootDirUriResolver(
    absl::string_view root_dir_view) {
  return [root_dir = std::string(root_dir_view)](
             const std::string& uri) -> absl::StatusOr<std::string> {
    if (absl::StartsWith(uri, "/")) {
      return uri;
    }
    absl::string_view uri_view(uri);
    if (absl::ConsumePrefix(&uri_view, "file://")) {
      return file::JoinPath(root_dir, uri_view);
    }
    if (absl::ConsumePrefix(&uri_view, "model://")) {
      return file::JoinPath(root_dir, uri_view);
    }
    return file::JoinPath(root_dir, uri);
  };
}

// Searches for the main entry point sdf file from the root directory of a self
// contained sdf scene.
absl::StatusOr<std::string> FindMainSdfFile(absl::string_view sdf_root_dir) {
  std::vector<std::string> all_sdfs;
  std::error_code ec;

  // Finds all SDFs recursively, skipping kDirectoriesToSkip.
  auto it = fs::recursive_directory_iterator(std::string(sdf_root_dir), ec);
  if (ec) {
    return absl::InternalError(absl::Substitute(
        "Failed to open directory $0: $1", sdf_root_dir, ec.message()));
  }

  for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) {
      return absl::InternalError(
          absl::Substitute("Error while iterating over directory $0: $1",
                           sdf_root_dir, ec.message()));
    }
    const auto& path = it->path();
    const bool should_skip = absl::c_any_of(path, [](const auto& part) {
      return absl::c_linear_search(kDirectoriesToSkip, part.native());
    });
    if (should_skip) {
      // Skips descending into this directory.
      it.disable_recursion_pending();
      continue;
    }

    if (it->is_regular_file() && path.extension() == ".sdf") {
      all_sdfs.push_back(path.string());
    }
  }

  if (all_sdfs.empty()) {
    return absl::NotFoundError(absl::Substitute(
        "No sdf files found in sdf root directory $0", sdf_root_dir));
  }

  // Case 1: returns the single SDF.
  if (all_sdfs.size() == 1) {
    return all_sdfs.front();
  }

  // Case 2: returns the single SDF found in the root directory.
  std::vector<std::string> top_level_sdfs;
  absl::c_copy_if(
      all_sdfs, std::back_inserter(top_level_sdfs), [&](const std::string& f) {
        return fs::path(f).parent_path() == fs::path(std::string(sdf_root_dir));
      });

  if (top_level_sdfs.size() == 1) {
    return top_level_sdfs.front();
  }

  // Case 3: returns the single model.sdf found in the root directory.
  auto model_it = absl::c_find_if(top_level_sdfs, [](const auto& f) {
    return fs::path(f).filename() == kDefaultModelFileName;
  });
  if (model_it != top_level_sdfs.end()) {
    return *model_it;
  }

  // Case 4: returns an error for multiple SDFs.
  std::vector<std::string> relative_sdfs;
  for (const auto& f : all_sdfs) {
    relative_sdfs.push_back(std::string(absl::StripPrefix(f, sdf_root_dir)));
  }
  return absl::NotFoundError(absl::Substitute(
      "Multiple SDF files found, but none are the obvious entry point (single "
      "SDF, single top-level SDF, or top-level model.sdf). Discovered files: "
      "[$0]",
      absl::StrJoin(relative_sdfs, ", ")));
}

}  // namespace

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromZippedSdf(absl::string_view zipped_data,
                         GeometrySerializer& geometry_serializer,
                         const SceneObjectFromSdfOptions& options) {
  TempPath temp_path("/tmp/");
  INTR_RETURN_IF_ERROR(UnzipData(zipped_data, temp_path.path(),
                                 {.strip_common_prefix_directories = true}));

  return SceneObjectFromSdfInDirectory(temp_path.path(), geometry_serializer,
                                       options);
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfInDirectory(absl::string_view sdf_root_directory,
                              GeometrySerializer& geometry_serializer,
                              const SceneObjectFromSdfOptions& options) {
  auto main_sdf_file = FindMainSdfFile(sdf_root_directory);
  if (!main_sdf_file.ok()) {
    return absl::NotFoundError(absl::Substitute(
        "Cannot find main SDF file to import as SceneObject. error: $0.",
        main_sdf_file.status().ToString(
            absl::StatusToStringMode::kWithNoExtraData)));
  }

  // Uses the directory containing the main SDF as the root for URI resolution.
  std::string actual_root_dir = std::string(file::Dirname(*main_sdf_file));
  auto scene_object = SceneObjectFromSdfFile(
      *main_sdf_file, RootDirUriResolver(actual_root_dir), geometry_serializer,
      options);
  if (!scene_object.ok()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to convert SDF file $0 to SceneObject. error: $1.",
        file::Basename(*main_sdf_file),
        scene_object.status().ToString(
            absl::StatusToStringMode::kWithNoExtraData)));
  }
  return *scene_object;
}

}  // namespace scene_object
}  // namespace intrinsic
