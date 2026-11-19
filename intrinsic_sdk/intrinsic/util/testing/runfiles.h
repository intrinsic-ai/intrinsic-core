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

#ifndef INTRINSIC_UTIL_TESTING_RUNFILES_H_
#define INTRINSIC_UTIL_TESTING_RUNFILES_H_

#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace intrinsic::testing {

// Returns the absolute path to a runfile, given its rlocation path.
//
// Call this only from tests run by `bazel test`.
//
// Always pass `BAZEL_CURRENT_REPOSITORY` as `source_repository`, and write it
// in the same call where you hardcode `rlocation_path`. To get
// `BAZEL_CURRENT_REPOSITORY` defined, add `@rules_cc//cc/runfiles` to your
// target's `deps`.
//
// Start `rlocation_path` with the apparent repository name, then give the path
// within that repository, e.g. "my_repo/path/to/testdata/file.txt".
//
// Check that the file exists yourself if you need to; this function only
// resolves the path. It returns an error if the runfiles can't be found or
// `rlocation_path` can't be mapped.
//
// Example:
//   ASSERT_OK_AND_ASSIGN(
//       std::string path,
//       intrinsic::testing::ResolveRlocation(
//           BAZEL_CURRENT_REPOSITORY, "my_repo/path/to/testdata/file.txt"));
absl::StatusOr<std::string> ResolveRlocation(
    absl::string_view source_repository, absl::string_view rlocation_path);

}  // namespace intrinsic::testing

#endif  // INTRINSIC_UTIL_TESTING_RUNFILES_H_
