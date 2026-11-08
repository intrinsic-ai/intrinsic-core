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

#ifndef INTRINSIC_ICON_CONTROL_ALGORITHMS_ENSURE_CLAMPED_H_
#define INTRINSIC_ICON_CONTROL_ALGORITHMS_ENSURE_CLAMPED_H_

#include <stddef.h>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/log.h"

namespace intrinsic {
namespace control {

// Returns a vector of same type as input, with values clamped in
// specified range. Will issue LOGW if intervention was necessary.
template <typename T>
T EnsureClampedAndWarn(const T& input_vector, const T& min_vector,
                       const T& max_vector) {
  CHECK_EQ(input_vector.size(), min_vector.size()) << absl::StrFormat(
      "input vector size (%lu) and min vector size (%lu) mismatch.",
      input_vector.size(), min_vector.size());
  CHECK_EQ(input_vector.size(), max_vector.size()) << absl::StrFormat(
      "input vector size (%lu) and max vector size (%lu) mismatch.",
      input_vector.size(), max_vector.size());
  T clamped_vector = input_vector;
  for (size_t k = 0; k < input_vector.size(); ++k) {
    if (input_vector(k) < min_vector(k)) {
      INTRINSIC_RT_LOG(WARNING)
          << "Out of lower bound value detected in vector. Clamping! Value: "
          << input_vector(k) << " Index: " << k
          << ", Lower bound: " << min_vector(k);
      clamped_vector(k) = min_vector(k);
    } else if (input_vector(k) > max_vector(k)) {
      INTRINSIC_RT_LOG(WARNING)
          << "Out of upper bound value detected in vector. Clamping! Value: "
          << input_vector(k) << " Index: " << k
          << ", Upper bound: " << max_vector(k);
      clamped_vector(k) = max_vector(k);
    }
  }

  return clamped_vector;
}

}  // namespace control
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_CONTROL_ALGORITHMS_ENSURE_CLAMPED_H_
