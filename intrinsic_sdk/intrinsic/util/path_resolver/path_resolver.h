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

#ifndef INTRINSIC_UTIL_PATH_RESOLVER_PATH_RESOLVER_H_
#define INTRINSIC_UTIL_PATH_RESOLVER_PATH_RESOLVER_H_

#include <string>

#include "absl/strings/string_view.h"

namespace intrinsic {

/**
 * Generic resolver that will attempt to resolve runfiles or external
 * directories if available.
 *
 * This is for internal use only.
 */
class PathResolver {
 public:
  static std::string ResolveRunfilesPath(absl::string_view path);
  static std::string ResolveRunfilesPathForTest(absl::string_view path);
  static std::string ResolveRunfilesPathIfRelative(absl::string_view path);

 private:
  static std::string GetRunfilesRepository(absl::string_view path);
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PATH_RESOLVER_PATH_RESOLVER_H_
