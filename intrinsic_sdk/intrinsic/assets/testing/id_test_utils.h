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

#ifndef INTRINSIC_ASSETS_TESTING_ID_TEST_UTILS_H_
#define INTRINSIC_ASSETS_TESTING_ID_TEST_UTILS_H_

#include <string>

#include "absl/strings/string_view.h"

namespace intrinsic {

inline constexpr absl::string_view kTestVersion = "0.0.1";

// Constructs an ID version for the given name that is suitable for use in a
// test.
//
// The resulting string is: `ai.intrinsic.<name>.<kTestVersion>`.
//
// If all characters in the name are not in [a-zA-Z_], there is no guarantee
// that the resulting id is valid.
std::string TestIdVersionFrom(absl::string_view name);

}  // namespace intrinsic

#endif  // INTRINSIC_ASSETS_TESTING_ID_TEST_UTILS_H_
