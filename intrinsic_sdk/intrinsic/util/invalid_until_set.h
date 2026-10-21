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

#ifndef INTRINSIC_UTIL_INVALID_UNTIL_SET_H_
#define INTRINSIC_UTIL_INVALID_UNTIL_SET_H_

#include <optional>

namespace intrinsic {

// Use this type for member variables that a class initializes outside of its
// constructor (f.i. in an Init() method) instead of a plain std::optional for
// better semantics.
template <typename T>
using InvalidUntilSet = std::optional<T>;

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_INVALID_UNTIL_SET_H_
