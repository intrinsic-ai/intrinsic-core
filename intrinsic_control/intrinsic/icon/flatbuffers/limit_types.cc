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


#include "intrinsic/icon/flatbuffers/limit_types.h"

#include <cstdint>
#include <limits>
#include <vector>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "intrinsic/icon/flatbuffers/limit_types.fbs.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"

namespace intrinsic_fbs {
namespace internal {
flatbuffers::Offset<JointLimitsPVAJ> AddJointLimitsPVAJ(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs, bool unlimited) {
  std::vector<double> default_vector;

  fbb.ForceDefaults(true);

  if (unlimited) {
    default_vector.assign(num_dofs, std::numeric_limits<double>::infinity());
  } else {
    default_vector.assign(num_dofs, 0);
  }

  // Fill out the buffer
  auto max_position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  auto max_velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  auto max_acceleration =
      intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_acceleration =
      intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  auto max_jerk = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_jerk = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointLimitsPVAJBuilder builder(fbb);

  builder.add_min_jerk(min_jerk);
  builder.add_max_jerk(max_jerk);
  builder.add_min_acceleration(min_acceleration);
  builder.add_max_acceleration(max_acceleration);
  builder.add_min_velocity(min_velocity);
  builder.add_max_velocity(max_velocity);
  builder.add_min_position(min_position);
  builder.add_max_position(max_position);

  return builder.Finish();
}

flatbuffers::Offset<JointLimitsPVA> AddJointLimitsPVA(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs, bool unlimited) {
  std::vector<double> default_vector;

  fbb.ForceDefaults(true);

  if (unlimited) {
    default_vector.assign(num_dofs, std::numeric_limits<double>::infinity());
  } else {
    default_vector.assign(num_dofs, 0);
  }

  // Fill out the buffer
  auto max_position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  auto max_velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  auto max_acceleration =
      intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_acceleration =
      intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointLimitsPVABuilder builder(fbb);

  builder.add_min_acceleration(min_acceleration);
  builder.add_max_acceleration(max_acceleration);
  builder.add_min_velocity(min_velocity);
  builder.add_max_velocity(max_velocity);
  builder.add_min_position(min_position);
  builder.add_max_position(max_position);

  return builder.Finish();
}

flatbuffers::Offset<JointLimitsPAJ> AddJointLimitsPAJ(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs, bool unlimited) {
  std::vector<double> default_vector;

  fbb.ForceDefaults(true);

  if (unlimited) {
    default_vector.assign(num_dofs, std::numeric_limits<double>::infinity());
  } else {
    default_vector.assign(num_dofs, 0);
  }

  // Fill out the buffer
  auto max_position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  auto max_acceleration =
      intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_acceleration =
      intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  auto max_jerk = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_jerk = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointLimitsPAJBuilder builder(fbb);

  builder.add_min_jerk(min_jerk);
  builder.add_max_jerk(max_jerk);
  builder.add_min_acceleration(min_acceleration);
  builder.add_max_acceleration(max_acceleration);
  builder.add_min_position(min_position);
  builder.add_max_position(max_position);

  return builder.Finish();
}

flatbuffers::Offset<JointLimitsP> AddJointLimitsP(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs, bool unlimited) {
  std::vector<double> default_vector;

  fbb.ForceDefaults(true);

  if (unlimited) {
    default_vector.assign(num_dofs, std::numeric_limits<double>::infinity());
  } else {
    default_vector.assign(num_dofs, 0);
  }

  // Fill out the buffer
  auto max_position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto min_position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointLimitsPBuilder builder(fbb);

  builder.add_min_position(min_position);
  builder.add_max_position(max_position);

  return builder.Finish();
}
}  // namespace internal
flatbuffers::DetachedBuffer CreateJointLimitsPVAJBuffer(uint16_t num_dofs,
                                                        bool unlimited) {
  flatbuffers::FlatBufferBuilder builder;
  auto joint_limits =
      internal::AddJointLimitsPVAJ(builder, num_dofs, unlimited);
  builder.Finish(joint_limits);
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointLimitsPVABuffer(uint16_t num_dofs,
                                                       bool unlimited) {
  flatbuffers::FlatBufferBuilder builder;
  auto joint_limits = internal::AddJointLimitsPVA(builder, num_dofs, unlimited);
  builder.Finish(joint_limits);
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointLimitsPAJBuffer(uint16_t num_dofs,
                                                       bool unlimited) {
  flatbuffers::FlatBufferBuilder builder;
  auto joint_limits = internal::AddJointLimitsPAJ(builder, num_dofs, unlimited);
  builder.Finish(joint_limits);
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointLimitsPBuffer(uint16_t num_dofs,
                                                     bool unlimited) {
  flatbuffers::FlatBufferBuilder builder;
  auto joint_limits = internal::AddJointLimitsP(builder, num_dofs, unlimited);
  builder.Finish(joint_limits);
  return builder.Release();
}

}  // namespace intrinsic_fbs
