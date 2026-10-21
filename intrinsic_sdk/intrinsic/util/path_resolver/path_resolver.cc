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

#include "intrinsic/util/path_resolver/path_resolver.h"

#include <unistd.h>

#include <cstdlib>
#include <string>
#include <vector>  

#include "absl/debugging/leak_check.h"
#include "absl/log/log.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"  
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"  
#include "ortools/base/path.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace intrinsic {

using bazel::tools::cpp::runfiles::Runfiles;

std::string BuildFullyQualifiedRunfilesPath(absl::string_view path) {

  //
  // Special handling for paths within the `google3` subdirectory.
  //
  // `_main` prefix in the runfiles directory represents the canonical
  // repository name of the root (main) workspace under Bzlmod.
  if (absl::StartsWith(path, "google3/")) {
    return file::JoinPath("_main", path);
  }

  if (absl::StartsWith(path, "intrinsic")) {
    std::string err;
    std::unique_ptr<Runfiles> rf(
        getenv("BAZEL_TEST") ? Runfiles::CreateForTest(&err)
                             : Runfiles::Create(program_invocation_name, &err));
    const char* const kLocations[] = {
        "_main",
        "_main/intrinsic_sdk",
        "_main/intrinsic_control",
        "_main/intrinsic_kinematics",
        "_main/intrinsic_motion_planning",
        "_main/intrinsic_perception",
        "_main/intrinsic_hardware",
        "_main/intrinsic_apis",
    };
    for (const char* loc : kLocations) {
      std::string candidate = file::JoinPath(loc, path);
      if (rf != nullptr) {
        std::string resolved = rf->Rlocation(candidate);
        if (!resolved.empty() && access(resolved.c_str(), F_OK) == 0) {
          return candidate;
        }
      }
    }
    return file::JoinPath("_main", path);
  }

  if (absl::StartsWith(path, "third_party")) {
    if (access(file::JoinPath("google3", path).c_str(), F_OK) == 0) {
      return file::JoinPath("_main", path);
    }
  }

  // If the path is already prefixed with `_main`, return as-is.
  if (absl::StartsWith(path, "_main/")) {
    return std::string(path);
  }

  // Fallback for other paths outside the google3 directory (e.g.
  // `third_party`).
  return file::JoinPath("_main", path);

  // For SDK, we prefix the path with the name of the bazel module assuming that
  // the path provided is relative to the repo. This is the recommended way to
  // resolve paths instead of prefixing with `_main`.





}


namespace {

std::string ResolveRunfilesWithCandidates(Runfiles* runfiles,
                                          absl::string_view path,
                                          const std::string& full_path) {
  absl::string_view subpath = path;
  if (absl::StartsWith(subpath, "google3/")) {
    subpath = absl::StripPrefix(subpath, "google3/");
  } else if (absl::StartsWith(subpath, "/google3/")) {
    subpath = absl::StripPrefix(subpath, "/google3/");
  }

  // Candidate Bzlmod relative roots
  std::vector<std::string> candidates = {
      full_path,
      BuildFullyQualifiedRunfilesPath(subpath),
      file::JoinPath("intrinsic-core+/google3", subpath),
      file::JoinPath("intrinsic-core/google3", subpath),
      file::JoinPath("intrinsic-core+/intrinsic_sdk", subpath),
      file::JoinPath("intrinsic-core/intrinsic_sdk", subpath),
      file::JoinPath("intrinsic-core+", path),
      file::JoinPath("intrinsic-core", path),
      file::JoinPath("insrc+/google3", subpath),
      file::JoinPath("intrinsic-core", subpath),
      file::JoinPath("insrc+/intrinsic_sdk", subpath),
      file::JoinPath("intrinsic-core/intrinsic_sdk", subpath),
      file::JoinPath("ioc/google3", subpath),
      file::JoinPath("insrc+", path),
      file::JoinPath("insrc", path),
      file::JoinPath("ioc", path),
      file::JoinPath("ai_intrinsic_sdks", path),
      file::JoinPath("ai_intrinsic_sdks/google3", subpath),
      std::string(path),
      std::string(subpath),
  };

  for (const auto& candidate : candidates) {
    std::string resolved = runfiles->Rlocation(candidate);
    if (!resolved.empty() && access(resolved.c_str(), F_OK) == 0) {
      return resolved;
    }
  }

  // Fallback to container paths
  std::vector<std::string> container_candidates = {
      file::JoinPath("/google3", path),
      file::JoinPath("/google3", subpath),
      file::JoinPath("/", path),
      file::JoinPath("/", subpath),
      file::JoinPath("/data", file::Basename(path)),
      file::JoinPath("/data/resources", file::Basename(path)),
  };

  for (const auto& candidate : container_candidates) {
    if (access(candidate.c_str(), F_OK) == 0) {
      return candidate;
    }
  }

  std::string resolved = runfiles->Rlocation(full_path);
  LOG(WARNING)
      << "PathResolver::ResolveRunfilesPath could not find an existing file"
         " for '"
      << path << "'. Tried candidates: [" << absl::StrJoin(candidates, ", ")
      << "]. Falling back to default: '" << resolved << "'";
  return resolved;
}

}  // namespace


std::string PathResolver::ResolveRunfilesPath(absl::string_view path) {
  std::string error;
  auto runfiles = std::unique_ptr<Runfiles>(
      Runfiles::Create(program_invocation_name, &error));
  if (runfiles == nullptr) {
    LOG(ERROR) << "Error creating Runfiles object: " << error;

    if (absl::StartsWith(path, "/")) return std::string(path);
    std::string direct_path = file::JoinPath("/google3", path);
    if (access(direct_path.c_str(), F_OK) == 0) return direct_path;
    return std::string(path);


  }

  std::string full_path = BuildFullyQualifiedRunfilesPath(path);

  return ResolveRunfilesWithCandidates(runfiles.get(), path, full_path);


}

std::string PathResolver::ResolveRunfilesPathForTest(absl::string_view path) {
  if (!getenv("BAZEL_TEST")) {
    return ResolveRunfilesPath(path);
  }

  std::string error;
  auto runfiles = Runfiles::CreateForTest(&error);
  if (runfiles == nullptr) {
    LOG(ERROR) << "Error creating Runfiles object for test: " << error;
    return "";
  }

  absl::IgnoreLeak(runfiles);

  std::string full_path = BuildFullyQualifiedRunfilesPath(path);

  return ResolveRunfilesWithCandidates(runfiles, path, full_path);


}

std::string PathResolver::ResolveRunfilesPathIfRelative(
    absl::string_view path) {
  if (absl::StartsWith(path, "/")) return std::string(path);
  return ResolveRunfilesPath(path);
}

}  // namespace intrinsic
