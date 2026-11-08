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


#include "intrinsic/icon/flatbuffers/state_types.h"

#include <cstdint>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/flatbuffers/state_types.fbs.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"

namespace intrinsic_fbs {

// Reserves and sets up FlatBuffer builder memory with JointStatePVT.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStatePVTorque> AddJointStatePVT(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs) {
  CHECK_LE(num_dofs, intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE)
      << absl::StrFormat(
             "eigenmath::MAX_EIGEN_VECTOR_SIZE set to %d, but num_dofs= %d",
             intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE, num_dofs);

  std::vector<double> default_vector;
  fbb.ForceDefaults(true);
  default_vector.assign(num_dofs, 0);

  // Fill out the buffer
  auto position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto torque = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointStatePVTorqueBuilder builder(fbb);

  builder.add_position(position);
  builder.add_velocity(velocity);
  builder.add_torque(torque);

  return builder.Finish();
}
// Reserves and sets up FlatBuffer builder memory with JointStateVAJ.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStateVAJ> AddJointStateVAJ(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs) {
  CHECK_LE(num_dofs, intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE)
      << absl::StrFormat(
             "eigenmath::MAX_EIGEN_VECTOR_SIZE set to %d, but num_dofs= %d",
             intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE, num_dofs);
  std::vector<double> default_vector;
  fbb.ForceDefaults(true);
  default_vector.assign(num_dofs, 0);

  // Fill out the buffer
  auto velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto acceleration = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto jerk = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointStateVAJBuilder builder(fbb);

  builder.add_velocity(velocity);
  builder.add_acceleration(acceleration);
  builder.add_jerk(jerk);

  return builder.Finish();
}

// Reserves and sets up FlatBuffer builder memory with JointStatePVAJ.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStatePVAJ> AddJointStatePVAJ(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs) {
  CHECK_LE(num_dofs, intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE)
      << absl::StrFormat(
             "eigenmath::MAX_EIGEN_VECTOR_SIZE set to %d, but num_dofs= %d",
             intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE, num_dofs);
  std::vector<double> default_vector;
  fbb.ForceDefaults(true);
  default_vector.assign(num_dofs, 0);

  // Fill out the buffer
  auto position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto acceleration = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto jerk = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointStatePVAJBuilder builder(fbb);

  builder.add_position(position);
  builder.add_velocity(velocity);
  builder.add_acceleration(acceleration);
  builder.add_jerk(jerk);

  return builder.Finish();
}

// Reserves and sets up FlatBuffer builder memory with JointStatePVA.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStatePVA> AddJointStatePVA(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs) {
  CHECK_LE(num_dofs, intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE)
      << absl::StrFormat(
             "eigenmath::MAX_EIGEN_VECTOR_SIZE set to %d, but num_dofs= %d",
             intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE, num_dofs);
  std::vector<double> default_vector;
  fbb.ForceDefaults(true);
  default_vector.assign(num_dofs, 0);

  // Fill out the buffer
  auto position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto acceleration = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointStatePVABuilder builder(fbb);

  builder.add_position(position);
  builder.add_velocity(velocity);
  builder.add_acceleration(acceleration);

  return builder.Finish();
}

// Reserves and sets up FlatBuffer builder memory with JointStateP.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStateP> AddJointStateP(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs) {
  CHECK_LE(num_dofs, intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE)
      << absl::StrFormat(
             "eigenmath::MAX_EIGEN_VECTOR_SIZE set to %d, but num_dofs= %d",
             intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE, num_dofs);
  std::vector<double> default_vector;
  fbb.ForceDefaults(true);
  default_vector.assign(num_dofs, 0);

  // Fill out the buffer
  auto position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointStatePBuilder builder(fbb);

  builder.add_position(position);

  return builder.Finish();
}

// Reserves and sets up FlatBuffer builder memory with JointStatePA.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStatePV> AddJointStatePV(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs) {
  CHECK_LE(num_dofs, intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE)
      << absl::StrFormat(
             "eigenmath::MAX_EIGEN_VECTOR_SIZE set to %d, but num_dofs= %d",
             intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE, num_dofs);
  std::vector<double> default_vector;
  fbb.ForceDefaults(true);
  default_vector.assign(num_dofs, 0);

  // Fill out the buffer
  auto position = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointStatePVBuilder builder(fbb);

  builder.add_position(position);
  builder.add_velocity(velocity);

  return builder.Finish();
}

// Reserves and sets up FlatBuffer builder memory with JointStateV.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStateV> AddJointStateV(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs) {
  CHECK_LE(num_dofs, intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE)
      << absl::StrFormat(
             "eigenmath::MAX_EIGEN_VECTOR_SIZE set to %d, but num_dofs= %d",
             intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE, num_dofs);
  std::vector<double> default_vector;
  fbb.ForceDefaults(true);
  default_vector.assign(num_dofs, 0);

  // Fill out the buffer
  auto velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointStateVBuilder builder(fbb);

  builder.add_velocity(velocity);

  return builder.Finish();
}

// Reserves and sets up FlatBuffer builder memory with JointStateVA.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStateVA> AddJointStateVA(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs) {
  CHECK_LE(num_dofs, intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE)
      << absl::StrFormat(
             "eigenmath::MAX_EIGEN_VECTOR_SIZE set to %d, but num_dofs= %d",
             intrinsic::eigenmath::MAX_EIGEN_VECTOR_SIZE, num_dofs);
  std::vector<double> default_vector;
  fbb.ForceDefaults(true);
  default_vector.assign(num_dofs, 0);

  // Fill out the buffer
  auto velocity = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);
  auto acceleration = intrinsic_fbs::CreateVectorNdDirect(fbb, &default_vector);

  JointStateVABuilder builder(fbb);

  builder.add_velocity(velocity);
  builder.add_acceleration(acceleration);

  return builder.Finish();
}

flatbuffers::DetachedBuffer CreateJointStatePVTBuffer(uint16_t num_dofs) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(AddJointStatePVT(builder, num_dofs));
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointStateVAJBuffer(uint16_t num_dofs) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(AddJointStateVAJ(builder, num_dofs));
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointStatePVAJBuffer(uint16_t num_dofs) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(AddJointStatePVAJ(builder, num_dofs));
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointStatePVABuffer(uint16_t num_dofs) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(AddJointStatePVA(builder, num_dofs));
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointStatePBuffer(uint16_t num_dofs) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(AddJointStateP(builder, num_dofs));
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointStatePVBuffer(uint16_t num_dofs) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(AddJointStatePV(builder, num_dofs));
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointStateVBuffer(uint16_t num_dofs) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(AddJointStateV(builder, num_dofs));
  return builder.Release();
}

flatbuffers::DetachedBuffer CreateJointStateVABuffer(uint16_t num_dofs) {
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(AddJointStateVA(builder, num_dofs));
  return builder.Release();
}

}  // namespace intrinsic_fbs
