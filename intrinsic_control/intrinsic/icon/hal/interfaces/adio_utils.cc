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

#include "intrinsic/icon/hal/interfaces/adio_utils.h"

#include <cstdint>
#include <string>

#include "flatbuffers/buffer.h"
#include "flatbuffers/detached_buffer.h"
#include "flatbuffers/flatbuffer_builder.h"
#include "intrinsic/icon/hal/interfaces/adio.fbs.h"

namespace intrinsic_fbs {

flatbuffers::DetachedBuffer BuildDigitalOutputCommand(std::string name,
                                                      bool value,
                                                      uint32_t bit_number) {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  auto name_fbs = builder.CreateString(name);
  builder.Finish(CreateDigitalOutputCommand(
      builder, /*name=*/name_fbs, /*value=*/value, /*bit_number=*/bit_number));
  return builder.Release();
}

flatbuffers::DetachedBuffer BuildDigitalInputStatus(std::string name,
                                                    bool value,
                                                    uint32_t bit_number) {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  auto name_fbs = builder.CreateString(name);
  builder.Finish(CreateDigitalInputStatus(
      builder, /*name=*/name_fbs, /*value=*/value, /*bit_number=*/bit_number));
  return builder.Release();
}

flatbuffers::DetachedBuffer BuildAnalogOutputCommand(std::string name,
                                                     AnalogInputUnit unit,
                                                     double value,
                                                     bool is_enabled) {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  auto name_fbs = builder.CreateString(name);
  builder.Finish(CreateAnalogOutputCommand(builder, /*name=*/name_fbs,
                                           /*unit=*/unit, /*value=*/value,
                                           /*is_enabled=*/is_enabled));
  return builder.Release();
}

flatbuffers::DetachedBuffer BuildAnalogInputStatus(std::string name,
                                                   AnalogInputUnit unit,
                                                   double value,
                                                   bool is_enabled) {
  flatbuffers::FlatBufferBuilder builder;
  builder.ForceDefaults(true);
  auto name_fbs = builder.CreateString(name);
  builder.Finish(CreateAnalogInputStatus(builder, /*name=*/name_fbs,
                                         /*unit=*/unit, /*value=*/value,
                                         /*is_enabled=*/is_enabled));
  return builder.Release();
}

}  // namespace intrinsic_fbs
