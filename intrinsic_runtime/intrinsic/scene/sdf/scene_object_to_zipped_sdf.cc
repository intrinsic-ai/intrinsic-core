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

#include "intrinsic/scene/sdf/scene_object_to_zipped_sdf.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "intrinsic/scene/sdf/scene_object_to_sdf.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/file.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"
#include "zip.h"

namespace intrinsic {
namespace scene_object {
namespace {

using ::intrinsic::sdf::SceneObjectToSdf;
using ::intrinsic::sdf::SceneObjectToSdfOptions;
using ::intrinsic_proto::scene_object::v1::SceneObject;

absl::StatusOr<std::string> CreateUniqueTempDir(absl::string_view prefix) {
  std::error_code ec;
  const std::filesystem::path temp_root =
      std::filesystem::temp_directory_path(ec);
  if (ec) {
    return absl::InternalError(
        absl::StrCat("Failed to get temp directory path: ", ec.message()));
  }
  const std::string temp_dir = temp_root.string();
  int i = 0;
  while (++i < 1000) {
    const std::string path = file::JoinPath(
        temp_dir,
        absl::StrCat(prefix, "_", absl::GetCurrentTimeNanos(), "_", i));
    if (std::filesystem::exists(path, ec)) {
      continue;
    }
    if (ec) {
      return absl::InternalError(
          absl::StrCat("Failed to check directory existence: ", ec.message()));
    }
    INTR_RETURN_IF_ERROR(file::RecursivelyCreateDir(path, file::Defaults()));
    return path;
  }

  return absl::InternalError("Failed to create a unique temporary directory.");
}

absl::StatusOr<std::string> CreateUniqueTempFile(absl::string_view prefix,
                                                 absl::string_view extension) {
  std::error_code ec;
  const std::filesystem::path temp_root =
      std::filesystem::temp_directory_path(ec);
  if (ec) {
    return absl::InternalError(
        absl::StrCat("Failed to get temp directory path: ", ec.message()));
  }
  const std::string temp_dir = temp_root.string();
  int i = 0;
  while (++i < 1000) {
    const std::string path = file::JoinPath(
        temp_dir, absl::StrCat(prefix, "_", absl::GetCurrentTimeNanos(), "_", i,
                               extension));
    if (std::filesystem::exists(path, ec)) {
      continue;
    }
    if (ec) {
      return absl::InternalError(
          absl::StrCat("Failed to check file existence: ", ec.message()));
    }
    // Creates it right away.
    std::ofstream of(path, std::ios::binary);
    if (!of.is_open()) {
      continue;
    }
    return path;
  }

  return absl::InternalError("Failed to create a unique file.");
}

// Zips all files in `directory` into `zip_path`.
absl::Status ZipDirectory(absl::string_view directory,
                          absl::string_view zip_path) {
  std::error_code ec;
  const std::filesystem::path abs_dir =
      std::filesystem::absolute(directory, ec);
  if (ec) {
    return absl::InternalError(absl::StrCat("Failed to resolve directory '",
                                            directory, "': ", ec.message()));
  }
  const std::filesystem::path abs_zip = std::filesystem::absolute(zip_path, ec);
  if (ec) {
    return absl::InternalError(absl::StrCat("Failed to resolve zip_path '",
                                            zip_path, "': ", ec.message()));
  }

  const std::filesystem::path rel = abs_zip.lexically_relative(abs_dir);
  const auto rel_it = rel.begin();
  if (abs_zip == abs_dir ||
      (!rel.empty() && !rel.is_absolute() && rel_it != rel.end() &&
       *rel_it != ".." && *rel_it != ".")) {
    return absl::InvalidArgumentError(absl::StrCat(
        "zip_path (", zip_path, ") cannot be inside or match directory (",
        directory, ")"));
  }

  if (!std::filesystem::is_directory(abs_dir, ec) || ec) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Directory does not exist or is not a directory: ", directory));
  }

  int err_code = 0;
  zip_t* const archive = zip_open(std::string(zip_path).c_str(),
                                  ZIP_CREATE | ZIP_TRUNCATE, &err_code);
  if (archive == nullptr) {
    zip_error_t ziperr;
    zip_error_init_with_code(&ziperr, err_code);
    const std::string msg = zip_error_strerror(&ziperr);
    zip_error_fini(&ziperr);
    return absl::InternalError(
        absl::StrCat("Failed to open zip archive '", zip_path, "': ", msg));
  }

  absl::Cleanup discard_archive = [archive]() { zip_discard(archive); };

  if (zip_set_archive_flag(
          archive, ZIP_AFL_CREATE_OR_KEEP_FILE_FOR_EMPTY_ARCHIVE, 1) < 0) {
    const std::string error_msg = zip_strerror(archive);
    return absl::InternalError(absl::StrCat(
        "Failed to set zip archive flag for empty archive: ", error_msg));
  }

  for (std::filesystem::recursive_directory_iterator it(abs_dir, ec), end;
       !ec && it != end; it.increment(ec)) {
    const bool is_reg = it->is_regular_file(ec);
    if (ec) {
      return absl::InternalError(
          absl::StrCat("Failed to inspect directory entry '",
                       it->path().string(), "': ", ec.message()));
    }
    if (!is_reg) {
      continue;
    }
    const std::string file_path = it->path().string();
    const std::string relative_path =
        it->path().lexically_relative(abs_dir).generic_string();

    zip_source_t* const src = zip_source_file(archive, file_path.c_str(), 0, 0);
    if (src == nullptr) {
      const std::string error_msg = zip_strerror(archive);
      return absl::InternalError(absl::StrCat(
          "Failed to create zip source for '", file_path, "': ", error_msg));
    }

    const zip_int64_t ix =
        zip_file_add(archive, relative_path.c_str(), src, ZIP_FL_ENC_UTF_8);
    if (ix < 0) {
      const std::string error_msg = zip_strerror(archive);
      zip_source_free(src);
      return absl::InternalError(absl::StrCat(
          "Failed to add file '", relative_path, "' to zip: ", error_msg));
    }

    // Uses highest compression possible.
    if (zip_set_file_compression(archive, ix, ZIP_CM_DEFLATE, 9) < 0) {
      const std::string error_msg = zip_strerror(archive);
      return absl::InternalError(absl::StrCat("Failed to set compression for '",
                                              relative_path, "': ", error_msg));
    }
  }

  if (ec) {
    return absl::InternalError(absl::StrCat("Failed to iterate directory '",
                                            directory, "': ", ec.message()));
  }

  if (zip_close(archive) < 0) {
    const std::string error_msg = zip_strerror(archive);
    return absl::InternalError(
        absl::StrCat("Failed to close and save zip archive: ", error_msg));
  }
  std::move(discard_archive).Cancel();

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::string> SceneObjectToZippedSdf(
    const SceneObject& scene_object, const SceneObjectToSdfOptions& options) {
  INTR_ASSIGN_OR_RETURN(const std::string base_path,
                        CreateUniqueTempDir("export"));
  const absl::Cleanup cleanup = [&base_path]() {
    std::error_code ec;
    std::filesystem::remove_all(base_path, ec);
  };

  const std::string sdf_path = file::JoinPath(base_path, "model.sdf");
  const std::string meshes_dir = file::JoinPath(base_path, "meshes");

  SceneObjectToSdfOptions sdf_options = options;
  sdf_options.save_geopath.clear();
  if (sdf_options.geometry_deserializer.has_value() &&
      *sdf_options.geometry_deserializer != nullptr) {
    INTR_RETURN_IF_ERROR(
        file::RecursivelyCreateDir(meshes_dir, file::Defaults()));
    sdf_options.save_geopath = meshes_dir;
  }

  INTR_ASSIGN_OR_RETURN(std::string sdf_content,
                        SceneObjectToSdf(scene_object, sdf_options));

  // Makes the mesh URIs relative to the SDF.
  if (sdf_options.geometry_deserializer.has_value() &&
      *sdf_options.geometry_deserializer != nullptr) {
    absl::StrReplaceAll({{meshes_dir, "meshes"}}, &sdf_content);
  }
  INTR_RETURN_IF_ERROR(
      file::SetContents(sdf_path, sdf_content, file::Defaults()));

  // Zips the entire base_path directory.
  INTR_ASSIGN_OR_RETURN(const std::string zip_path,
                        CreateUniqueTempFile("export_", ".zip"));
  const absl::Cleanup del_zip = [&zip_path]() {
    std::error_code ec;
    std::filesystem::remove(zip_path, ec);
  };

  INTR_RETURN_IF_ERROR(ZipDirectory(base_path, zip_path));

  std::string zip_archive;
  INTR_RETURN_IF_ERROR(
      file::GetContents(zip_path, &zip_archive, file::Defaults()));
  return zip_archive;
}

}  // namespace scene_object
}  // namespace intrinsic
