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


#ifndef INTRINSIC_ICON_FLATBUFFERS_STATE_TYPES_H_
#define INTRINSIC_ICON_FLATBUFFERS_STATE_TYPES_H_

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "intrinsic/icon/flatbuffers/state_types.fbs.h"

namespace intrinsic_fbs {

// Reserves and sets up FlatBuffer builder memory with JointStateV.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStateV> AddJointStateV(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs);

// Reserves and sets up FlatBuffer builder memory with JointStatePV.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStatePV> AddJointStatePV(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs);

// Reserves and sets up FlatBuffer builder memory with JointStatePVT.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStatePVTorque> AddJointStatePVT(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs);

// Reserves and sets up FlatBuffer builder memory with JointStatePVA.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStatePVA> AddJointStatePVA(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs);

// Reserves and sets up FlatBuffer builder memory with JointStatePVAJ.
// By default, all vectors are set to 0.
flatbuffers::Offset<JointStatePVAJ> AddJointStatePVAJ(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs);

// Creates a DetachedBuffer with a JointStatePVT with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointStatePVTBuffer(uint16_t num_dofs);

// Creates a DetachedBuffer with a JointStateVAJ with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointStateVAJBuffer(uint16_t num_dofs);

// Creates a DetachedBuffer with a JointStatePVAJ with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointStatePVAJBuffer(uint16_t num_dofs);

// Creates a DetachedBuffer with a JointStatePVA with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointStatePVABuffer(uint16_t num_dofs);

// Creates a DetachedBuffer with a JointStateP with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointStatePBuffer(uint16_t num_dofs);

// Creates a DetachedBuffer with a JointStatePV with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointStatePVBuffer(uint16_t num_dofs);

// Creates a DetachedBuffer with a JointStateV with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointStateVBuffer(uint16_t num_dofs);

// Creates a DetachedBuffer with a JointStateVA with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointStateVABuffer(uint16_t num_dofs);

}  // namespace intrinsic_fbs

#endif  // INTRINSIC_ICON_FLATBUFFERS_STATE_TYPES_H_
