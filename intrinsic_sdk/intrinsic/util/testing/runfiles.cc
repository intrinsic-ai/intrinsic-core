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

#include "intrinsic/util/testing/runfiles.h"

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "rules_cc/cc/runfiles/runfiles.h"

namespace intrinsic::testing {

absl::StatusOr<std::string> ResolveRlocation(
    absl::string_view source_repository, absl::string_view rlocation_path) {
  std::string error;
  std::unique_ptr<rules_cc::cc::runfiles::Runfiles> runfiles(
      rules_cc::cc::runfiles::Runfiles::CreateForTest(
          std::string(source_repository), &error));
  if (runfiles == nullptr) {
    return absl::InternalError(
        absl::StrCat("Failed to create Runfiles object: ", error));
  }
  std::string path = runfiles->Rlocation(std::string(rlocation_path));
  if (path.empty()) {
    return absl::NotFoundError(
        absl::StrCat("Failed to resolve rlocation path: ", rlocation_path));
  }
  return path;
}

}  // namespace intrinsic::testing
