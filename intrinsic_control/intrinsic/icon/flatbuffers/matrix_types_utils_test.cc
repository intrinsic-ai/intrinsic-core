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

#include "intrinsic/icon/flatbuffers/matrix_types_utils.h"

#include <gtest/gtest.h>

#include "fuzztest/fuzztest.h"
#include "fuzztest/init_fuzztest.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/flatbuffers/matrix_types.fbs.h"
#include "intrinsic/math/testing/fuzz_math_utils.h"

namespace intrinsic_fbs {
namespace {

void CopyToCopiesMatrixCorrectly(const intrinsic::eigenmath::Matrix3d& matrix) {
  Matrix3d matrix_fbs;

  CopyTo(matrix, matrix_fbs);
}
FUZZ_TEST(CopyToTest, CopyToCopiesMatrixCorrectly)
    .WithDomains(intrinsic::testing::FiniteMatrix3d());

void ViewAsViewsCorrectMatrix(const intrinsic::eigenmath::Matrix3d& matrix) {
  Matrix3d matrix_fbs;
  CopyTo(matrix, matrix_fbs);

  EXPECT_EQ(ViewAs<intrinsic::eigenmath::Matrix3d>(matrix_fbs), matrix);
}
FUZZ_TEST(ViewAsTest, ViewAsViewsCorrectMatrix)
    .WithDomains(intrinsic::testing::FiniteMatrix3d());

}  // namespace
}  // namespace intrinsic_fbs

int main(int argc, char** argv) {
  fuzztest::ParseAbslFlags(argc, argv);
  fuzztest::InitFuzzTest(&argc, &argv);
  return RUN_ALL_TESTS();
}
