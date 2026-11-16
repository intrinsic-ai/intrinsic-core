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

#ifndef INTRINSIC_UTIL_FULL_PRECISION_H_
#define INTRINSIC_UTIL_FULL_PRECISION_H_

#include <concepts>
#include <limits>
#include <string>

#include "absl/strings/str_format.h"

namespace intrinsic {

template <typename T>
  requires std::floating_point<T>
std::string FullPrecision(T val) {
  return absl::StrFormat("%.*g", std::numeric_limits<T>::max_digits10, val);
}

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_FULL_PRECISION_H_
