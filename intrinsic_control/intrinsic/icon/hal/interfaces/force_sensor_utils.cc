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

#include "intrinsic/icon/hal/interfaces/force_sensor_utils.h"

#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "intrinsic/icon/flatbuffers/transform_types.fbs.h"
#include "intrinsic/icon/hal/interfaces/force_sensor.fbs.h"

namespace intrinsic_fbs {

// Creates a detached flatbuffer that stores a message defined as
// ForceSensorStatus.
flatbuffers::DetachedBuffer CreateForceSensorStatusBuffer() {
  intrinsic_fbs::Wrench wrench;

  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);

  builder.Finish(CreateForceSensorStatus(builder, &wrench, &wrench, &wrench,
                                         &wrench,
                                         ForceSensorStatusCode::GenericError));

  return builder.Release();
}

// Creates a detached flatbuffer that stores a message defined as
// ForceSensorCommand.
flatbuffers::DetachedBuffer CreateForceSensorCommandBuffer() {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  builder.Finish(CreateForceSensorCommand(builder, /*tare_sensor=*/false,
                                          /*num_taring_cycles=*/1));
  return builder.Release();
}

}  // namespace intrinsic_fbs
