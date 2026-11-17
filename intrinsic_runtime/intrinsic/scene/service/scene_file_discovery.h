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

#ifndef INTRINSIC_SCENE_SERVICE_SCENE_FILE_DISCOVERY_H_
#define INTRINSIC_SCENE_SERVICE_SCENE_FILE_DISCOVERY_H_

#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

// Helpers to locate the scene file to import inside an unpacked directory and
// to enumerate the file extensions supported by scene object import.
namespace intrinsic::internal {

// The different high-level file types that we support for import.
enum class SceneFileTypes {
  SDF,
  USD,
  CAD,
  MESH,
};

// Returns all files in the directory that have the given extension.
// Does not look into subdirectories.
absl::StatusOr<std::vector<std::string>> FindFilesWithExtensionInDirectory(
    absl::string_view directory, absl::string_view ext);

// Returns all files in the directory that have any of the given extensions.
// Does not look into subdirectories.
absl::StatusOr<std::vector<std::string>> FindFilesWithExtensionsInDirectory(
    absl::string_view directory, const absl::flat_hash_set<std::string>& exts);

// Finds a scene file to import within the given directory. If we find a
// suitable scene file to import, returns {filepath, filetype}. Otherwise,
// returns an error. If there are multiple different file-types in the
// directory, attempts to select a "best" file / most-expected file
// to import, or fails with an error telling the user to resolve the ambiguity.
absl::StatusOr<std::pair<std::string, SceneFileTypes>>
FindSceneFileToImportInDirectory(absl::string_view scene_file_directory);

// Finds the topmost directory within the given directory that contains at least
// one relevant file (excluding metadata files and directories like __MACOSX
// and .DS_Store). This is used to skip redundant parent directories often found
// in zip files.
//
// Examples:
//
// Handles simple nesting:
// mydir/
//   mydir/
//     robot.sdf
// ==> mydir/mydir/
//
// Skips junk files:
// mydir/
//   .DS_Store
//   mydir/
//     robot.sdf
// ==> mydir/mydir/
//
// If it is ambigous which folder to traverse into, we return the parent:
// mydir/
//   folderA/
//      robot.sdf
//   folderB/
//      robot.sdf
// ==> mydir/
absl::StatusOr<std::string> FindTopmostDirectoryWithContent(
    absl::string_view directory_path);

// Returns a list of all supported file extensions for scene object import,
// in sorted order.
std::vector<std::string> GetAllSupportedExtensions();

// Returns supported extensions for SDF files.
absl::flat_hash_set<std::string> SupportedSdfExtensions();

}  // namespace intrinsic::internal

#endif  // INTRINSIC_SCENE_SERVICE_SCENE_FILE_DISCOVERY_H_
