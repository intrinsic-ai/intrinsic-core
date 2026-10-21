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

#ifndef INTRINSIC_UTIL_STATUS_RETURN_H_
#define INTRINSIC_UTIL_STATUS_RETURN_H_

#include <type_traits>
#include <utility>

#include "absl/status/status.h"

namespace intrinsic {
namespace return_internal {
template <typename T>
struct ReturnImpl;
}  // namespace return_internal

template <typename T>
return_internal::ReturnImpl<std::decay_t<T>> Return(T&& value);

class ReturnVoid {
 public:
  void operator()(const absl::Status& s) const {}
};

namespace return_internal {
template <typename T>
struct ReturnImpl {
  T value;
  T operator()(const absl::Status& s) const& { return value; }
  T operator()(const absl::Status& s) && { return std::move(value); }
};
}  // namespace return_internal

template <typename T>
inline return_internal::ReturnImpl<std::decay_t<T>> Return(T&& value) {
  return return_internal::ReturnImpl<std::decay_t<T>>{std::forward<T>(value)};
}
}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STATUS_RETURN_H_
