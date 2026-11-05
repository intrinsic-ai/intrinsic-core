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

#ifndef INTRINSIC_EIGENMATH_TYPE_CHECKS_H_
#define INTRINSIC_EIGENMATH_TYPE_CHECKS_H_

#include <type_traits>

#include "intrinsic/eigenmath/pose2.h"
#include "intrinsic/eigenmath/so2.h"
#include "intrinsic/eigenmath/so3.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {
namespace eigenmath {

// Checks if a type is Pose2<K>.
template <typename T>
static constexpr bool IsPose2 = false;

template <typename Scalar, int Options>
static constexpr bool IsPose2<Pose2<Scalar, Options>> = true;

// Checks if a type is Pose3<K>.
template <typename T>
static constexpr bool IsPose3 = false;

template <typename Scalar, int Options>
static constexpr bool IsPose3<Pose3<Scalar, Options>> = true;

// Checks if a type is Pose2<K> or Pose3<K>.
template <typename T>
static constexpr bool IsPose = IsPose2<T> || IsPose3<T>;

template <typename T>
struct ScalarTraitOf {
  // Use double if everything else fails.
  template <typename U, typename = void>
  struct ScalarTypeFromEigen {
    using type = double;
  };
  // If it's an Eigen type of some sort, there must be a norm since isApprox
  // depends on that function.
  template <typename U>
  struct ScalarTypeFromEigen<U,
                             std::void_t<decltype(std::declval<U>().norm())>> {
    using type = decltype(std::declval<U>().norm());
  };

  using type = typename ScalarTypeFromEigen<T>::type;
};

template <typename Scalar, int Options>
struct ScalarTraitOf<SO2<Scalar, Options>> {
  using type = Scalar;
};

template <typename Scalar, int Options>
struct ScalarTraitOf<SO3<Scalar, Options>> {
  using type = Scalar;
};

template <typename Scalar, int Options>
struct ScalarTraitOf<Pose2<Scalar, Options>> {
  using type = Scalar;
};

template <typename Scalar, int Options>
struct ScalarTraitOf<Pose3<Scalar, Options>> {
  using type = Scalar;
};

template <typename T>
using ScalarTypeOf = typename ScalarTraitOf<T>::type;

}  // namespace eigenmath
}  // namespace intrinsic

#endif  // INTRINSIC_EIGENMATH_TYPE_CHECKS_H_
