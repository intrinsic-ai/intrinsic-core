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

#include "intrinsic/scene/service/scene_file_discovery.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "intrinsic/geometry/api/file_io.h"
#include "intrinsic/scene/usd/scene_object_from_usd.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"

namespace intrinsic::internal {

namespace {

constexpr absl::string_view kImportObjectDocumentation =
    "https://flowstate.intrinsic.ai/docs/assets/create_new_assets/"
    "create_scene_objects/#upload-new-objects-from-file";

}  // namespace

absl::flat_hash_set<std::string> SupportedSdfExtensions() {
  return absl::flat_hash_set<std::string>({"sdf"});
}

absl::StatusOr<std::vector<std::string>> FindFilesWithExtensionInDirectory(
    absl::string_view directory, absl::string_view ext) {
  std::vector<std::string> mesh_files;
  INTR_RETURN_IF_ERROR(file::Match(
      file::JoinPath(directory, absl::StrCat("*.", absl::AsciiStrToLower(ext))),
      &mesh_files, file::Defaults()));
  INTR_RETURN_IF_ERROR(file::Match(
      file::JoinPath(directory, absl::StrCat("*.", absl::AsciiStrToUpper(ext))),
      &mesh_files, file::Defaults()));
  // Sort, to keep the list deterministic
  std::sort(mesh_files.begin(), mesh_files.end());
  return mesh_files;
}

absl::StatusOr<std::vector<std::string>> FindFilesWithExtensionsInDirectory(
    absl::string_view directory, const absl::flat_hash_set<std::string>& exts) {
  std::vector<std::string> all_matches;
  for (const std::string& ext : exts) {
    INTR_ASSIGN_OR_RETURN(auto files,
                          FindFilesWithExtensionInDirectory(directory, ext));
    all_matches.insert(all_matches.end(), files.begin(), files.end());
  }
  // Sort, to keep the list deterministic
  std::sort(all_matches.begin(), all_matches.end());
  return all_matches;
}

absl::StatusOr<std::string> FindTopmostDirectoryWithContent(
    absl::string_view scene_file_directory) {
  std::string root_dir = std::string(scene_file_directory);
  while (true) {
    std::vector<std::string> relevant_entries;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(root_dir, ec)) {
      if (ec) break;
      const std::string filename = entry.path().filename().string();
      if (filename == ".DS_Store" || filename == "__MACOSX") {
        continue;
      }
      relevant_entries.push_back(entry.path().string());
    }
    if (ec) {
      return absl::InternalError(
          absl::StrCat("Error iterating directory: ", root_dir));
    }

    if (relevant_entries.size() == 1 &&
        std::filesystem::is_directory(relevant_entries[0], ec) && !ec) {
      root_dir = relevant_entries[0];
    } else {
      break;
    }
  }
  return root_dir;
}

absl::StatusOr<std::pair<std::string, SceneFileTypes>>
FindSceneFileToImportInDirectory(absl::string_view scene_file_directory) {
  // We look for a scene file that we can import, somewhere within the
  // directory. When there are multiple files that we can import, we pick one
  // according to these rules:
  // 1. We only consider files at the topmost dir (ignoring any empty parent
  //    dirs, which are common in zip-files)
  // 2. The priority order when there are multiple file-types is: SDF, CAD, USD,
  //    then MESH type files.
  // 3. When there are multiple of the same file-type (like two SDF files), we
  //    either pick one (in the case of USD) or return with an error.

  // 1. Find the root/topmost directory for import
  INTR_ASSIGN_OR_RETURN(std::string root_dir,
                        FindTopmostDirectoryWithContent(scene_file_directory));

  // 2 - Look for files to import, in priority order.

  // SDF
  INTR_ASSIGN_OR_RETURN(
      std::vector<std::string> sdf_files,
      FindFilesWithExtensionsInDirectory(root_dir, SupportedSdfExtensions()));
  if (!sdf_files.empty()) {
    if (sdf_files.size() > 1) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Found multiple SDF files in the directory: $0. We only support "
          "importing one file at a time. Please remove the others.",
          absl::StrJoin(sdf_files, ", ")));
    }
    return std::make_pair(sdf_files[0], SceneFileTypes::SDF);
  }
  // USD
  INTR_ASSIGN_OR_RETURN(std::vector<std::string> usd_files,
                        FindFilesWithExtensionsInDirectory(
                            root_dir, usd::SupportedUsdExtensions()));
  if (!usd_files.empty()) {
    // If there are multiple USD files, we log a warning and choose the first
    // one, in sorted order. This is usually the correct choice. For example,
    // "robot.usd" will be chosen before "robot_variantb.usd".
    if (usd_files.size() > 1) {
      LOG(WARNING)
          << "Found multiple USD files at the root of the folder. Choosing "
          << usd_files[0]
          << ". If you would like a certain file chosen, please remove the "
             "others. Found: "
          << absl::StrJoin(usd_files, ", ");
    }
    return std::make_pair(usd_files[0], SceneFileTypes::USD);
  }
  // MESH
  INTR_ASSIGN_OR_RETURN(std::vector<std::string> mesh_files,
                        FindFilesWithExtensionsInDirectory(
                            root_dir, geo::SupportedMeshExtensions()));
  if (!mesh_files.empty()) {
    if (mesh_files.size() > 1) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Found multiple mesh files in the directory: $0. We only support "
          "importing one file at a time. Please remove the others.",
          absl::StrJoin(mesh_files, ", ")));
    }
    return std::make_pair(mesh_files[0], SceneFileTypes::MESH);
  }

  return absl::NotFoundError(absl::Substitute(
      "Could not find a scene file to import in the folder. Was "
      "expecting a file with one of these extensions, placed at the "
      "folder root: $0. See $1 for more information.",
      absl::StrJoin(GetAllSupportedExtensions(), ", "),
      kImportObjectDocumentation));
}

std::vector<std::string> GetAllSupportedExtensions() {
  std::vector<std::string> all_supported_extensions;
  all_supported_extensions.insert(all_supported_extensions.end(),
                                  SupportedSdfExtensions().begin(),
                                  SupportedSdfExtensions().end());
  all_supported_extensions.insert(all_supported_extensions.end(),
                                  usd::SupportedUsdExtensions().begin(),
                                  usd::SupportedUsdExtensions().end());
  all_supported_extensions.insert(all_supported_extensions.end(),
                                  geo::SupportedMeshExtensions().begin(),
                                  geo::SupportedMeshExtensions().end());
  std::sort(all_supported_extensions.begin(), all_supported_extensions.end());
  return all_supported_extensions;
}

}  // namespace intrinsic::internal
