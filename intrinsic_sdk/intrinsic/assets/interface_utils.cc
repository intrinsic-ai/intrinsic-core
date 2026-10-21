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

#include "intrinsic/assets/interface_utils.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "re2/re2.h"

namespace intrinsic {
namespace assets {
namespace {
static constexpr LazyRE2 kUriRegex = {
    R"(^(grpc://|data://)([A-Za-z_][A-Za-z0-9_]*\.)+[A-Za-z_][A-Za-z0-9_]*$)"};
}

// Validates an interface name with a protocol prefix.
absl::Status ValidateInterfaceName(absl::string_view uri) {
  if (!RE2::FullMatch(uri, *kUriRegex)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Expected URI to be formatted as "
                     "'<protocol>://<package>.<message>', got '",
                     uri, "'"));
  }
  return absl::OkStatus();
}

}  // namespace assets
}  // namespace intrinsic
