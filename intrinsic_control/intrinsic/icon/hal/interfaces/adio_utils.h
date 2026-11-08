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

#ifndef INTRINSIC_ICON_HAL_INTERFACES_ADIO_UTILS_H_
#define INTRINSIC_ICON_HAL_INTERFACES_ADIO_UTILS_H_

#include <cstdint>
#include <string>

#include "flatbuffers/detached_buffer.h"
#include "intrinsic/icon/hal/interfaces/adio.fbs.h"

namespace intrinsic_fbs {

flatbuffers::DetachedBuffer BuildDigitalOutputCommand(std::string name,
                                                      bool value,
                                                      uint32_t bit_number);

flatbuffers::DetachedBuffer BuildDigitalInputStatus(std::string name,
                                                    bool value,
                                                    uint32_t bit_number);

flatbuffers::DetachedBuffer BuildAnalogInputStatus(std::string name,
                                                   AnalogInputUnit unit,
                                                   double value,
                                                   bool is_enabled = false);

flatbuffers::DetachedBuffer BuildAnalogOutputCommand(std::string name,
                                                     AnalogInputUnit unit,
                                                     double value,
                                                     bool is_enabled = false);

}  // namespace intrinsic_fbs

#endif  // INTRINSIC_ICON_HAL_INTERFACES_ADIO_UTILS_H_
