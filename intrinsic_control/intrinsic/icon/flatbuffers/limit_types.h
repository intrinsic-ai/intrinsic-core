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


#ifndef INTRINSIC_ICON_FLATBUFFERS_LIMIT_TYPES_H_
#define INTRINSIC_ICON_FLATBUFFERS_LIMIT_TYPES_H_

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "intrinsic/icon/flatbuffers/limit_types.fbs.h"

namespace intrinsic_fbs {
namespace internal {
// Reserves and sets up FlatBuffer builder memory with JointLimitsPVAJ.
// By default, all vectors are set to 0. Setting `unlimited` parameter to true
// will initialize all vectors to the max double values.
flatbuffers::Offset<JointLimitsPVAJ> AddJointLimitsPVAJ(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs,
    bool unlimited = false);

// Reserves and sets up FlatBuffer builder memory with JointLimitsPVA.
// By default, all vectors are set to 0. Setting `unlimited` parameter to true
// will initialize all vectors to the max double values.
flatbuffers::Offset<JointLimitsPVA> AddJointLimitsPVA(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs,
    bool unlimited = false);

// Reserves and sets up FlatBuffer builder memory with JointLimitsPAJ.
// By default, all vectors are set to 0. Setting `unlimited` parameter to true
// will initialize all vectors to the max double values.
flatbuffers::Offset<JointLimitsPAJ> AddJointLimitsPAJ(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs,
    bool unlimited = false);

// Reserves and sets up FlatBuffer builder memory with JointLimitsP.
// By default, all vectors are set to 0. Setting `unlimited` parameter to true
// will initialize all vectors to the max double values.
flatbuffers::Offset<JointLimitsP> AddJointLimitsP(
    flatbuffers::FlatBufferBuilder& fbb, uint16_t num_dofs,
    bool unlimited = false);
}  // namespace internal

// Creates a DetachedBuffer with a JointLimitsPVAJ with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointLimitsPVAJBuffer(uint16_t num_dofs,
                                                        bool unlimited = false);

// Creates a DetachedBuffer with a JointLimitsPVA with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointLimitsPVABuffer(uint16_t num_dofs,
                                                       bool unlimited = false);

// Creates a DetachedBuffer with a JointLimitsPAJ with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointLimitsPAJBuffer(uint16_t num_dofs,
                                                       bool unlimited = false);

// Creates a DetachedBuffer with a JointLimitsP with the
// given number of dofs.
flatbuffers::DetachedBuffer CreateJointLimitsPBuffer(uint16_t num_dofs,
                                                     bool unlimited = false);

}  // namespace intrinsic_fbs

#endif  // INTRINSIC_ICON_FLATBUFFERS_LIMIT_TYPES_H_
