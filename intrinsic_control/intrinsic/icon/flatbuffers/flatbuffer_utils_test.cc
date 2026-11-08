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

#include "intrinsic/icon/flatbuffers/flatbuffer_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "flatbuffers/buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "flatbuffers/vector.h"
#include "internal/testing.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/segment_info.fbs.h"

namespace intrinsic_fbs {
namespace {

using ::absl_testing::StatusIs;

TEST(FlatbufferArrayNumElementsTest, ReturnsCorrectNumElements) {
  EXPECT_EQ(FlatbufferArrayNumElements(&SegmentInfo::names),
            SegmentInfo{}.names()->size());
}

TEST(FlatbufferUtilsTest, CopiesFlatbufferDoubleVector) {
  auto create_vector = [](flatbuffers::FlatBufferBuilder& builder,
                          const std::vector<double>& data) {
    auto vectorOffset = builder.CreateVector(data);
    builder.Finish(vectorOffset);

    uint8_t* buffer = builder.GetBufferPointer();
    flatbuffers::Vector<double>* retrievedVector =
        flatbuffers::GetMutableRoot<flatbuffers::Vector<double>>(buffer);
    return retrievedVector;
  };

  const size_t kNDof = 6;
  std::vector<double> ones(kNDof, 1.0);
  std::vector<double> zeros(kNDof, 0.0);
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::FlatBufferBuilder builder2;
  flatbuffers::Vector<double>* vector = create_vector(builder, ones);
  flatbuffers::Vector<double>* vector2 = create_vector(builder2, zeros);
  for (int i = 0; i < kNDof; ++i) {
    EXPECT_EQ(zeros.at(i), vector2->Get(i));
  }
  EXPECT_OK(CopyFbsVector(*vector, *vector2));
  for (int i = 0; i < kNDof; ++i) {
    EXPECT_EQ(ones.at(i), vector2->Get(i));
  }
}

TEST(FlatbufferUtilsTest, CopiesFlatbufferPointVector) {
  intrinsic_fbs::Point zero;
  zero.mutate_x(0.0);
  zero.mutate_y(0.0);
  zero.mutate_z(0.0);
  intrinsic_fbs::Point one;
  one.mutate_x(1.0);
  one.mutate_y(1.0);
  one.mutate_z(1.0);

  auto create_vector = [](flatbuffers::FlatBufferBuilder& builder,
                          const std::vector<intrinsic_fbs::Point>& data) {
    auto vectorOffset = builder.CreateVectorOfStructs(data);
    builder.Finish(vectorOffset);

    uint8_t* buffer = builder.GetBufferPointer();
    flatbuffers::Vector<intrinsic_fbs::Point>* retrievedVector =
        flatbuffers::GetMutableRoot<flatbuffers::Vector<intrinsic_fbs::Point>>(
            buffer);
    return retrievedVector;
  };

  const size_t kNDof = 6;
  std::vector<intrinsic_fbs::Point> ones;
  for (int i = 0; i < kNDof; ++i) {
    ones.push_back(one);
  }
  std::vector<intrinsic_fbs::Point> zeros;
  for (int i = 0; i < kNDof; ++i) {
    zeros.push_back(zero);
  }
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::FlatBufferBuilder builder2;
  flatbuffers::Vector<intrinsic_fbs::Point>* vector =
      create_vector(builder, ones);
  flatbuffers::Vector<intrinsic_fbs::Point>* vector2 =
      create_vector(builder2, zeros);
  for (int i = 0; i < kNDof; ++i) {
    EXPECT_EQ(zero, *vector2->Get(i));
  }
  EXPECT_OK(CopyFbsVector(*vector, *vector2));
  for (int i = 0; i < kNDof; ++i) {
    EXPECT_EQ(one, *vector2->Get(i));
  }
}

TEST(FlatbufferUtilsTest, CopiesFlatbufferDoubleVectorWithWrongSize) {
  auto create_vector = [](flatbuffers::FlatBufferBuilder& builder,
                          const std::vector<double>& data) {
    auto vectorOffset = builder.CreateVector(data);
    builder.Finish(vectorOffset);

    uint8_t* buffer = builder.GetBufferPointer();
    flatbuffers::Vector<double>* retrievedVector =
        flatbuffers::GetMutableRoot<flatbuffers::Vector<double>>(buffer);
    return retrievedVector;
  };

  const size_t kNDof = 6;
  std::vector<double> ones(kNDof, 1.0);
  std::vector<double> zeros(kNDof + 1, 0.0);
  flatbuffers::FlatBufferBuilder builder;
  flatbuffers::FlatBufferBuilder builder2;
  flatbuffers::Vector<double>* vector = create_vector(builder, ones);
  flatbuffers::Vector<double>* vector2 = create_vector(builder2, zeros);

  EXPECT_THAT(CopyFbsVector(*vector, *vector2),
              StatusIs(absl::StatusCode::kOutOfRange));
}

}  // namespace
}  // namespace intrinsic_fbs
