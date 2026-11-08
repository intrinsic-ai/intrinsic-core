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

#ifndef INTRINSIC_ICON_HAL_TEST_TEST_HARDWARE_MODULE_CONFIG_H_
#define INTRINSIC_ICON_HAL_TEST_TEST_HARDWARE_MODULE_CONFIG_H_

#include <string>
#include <string_view>

#include "absl/strings/str_format.h"
#include "intrinsic/icon/utils/duration.h"

namespace test_hardware_module {

constexpr intrinsic::Duration kDefaultTestControlPeriod =
    intrinsic::Milliseconds(4);

inline std::string GetTestHardwareModuleConfig(
    std::string_view module_name = "my_test_hardware_module",
    std::string_view interface_prefix = "joint",
    bool allocate_memory_in_first_cycle = false,
    bool disable_malloc_guard = true,
    intrinsic::Duration control_period = kDefaultTestControlPeriod) {
  return absl::StrFormat(
      R"pb(
        name: "%s"
        disable_malloc_guard: %s
        control_period_ns: %v
        module_config {
          [type.googleapis.com/intrinsic_proto.testing.TestHardwareModule] {
            interface_prefix: "%s"
            do_illegal_mallocs: %s
          }
        }
      )pb",
      module_name, disable_malloc_guard ? "true" : "false",
      intrinsic::ToInt64Nanoseconds(control_period), interface_prefix,
      allocate_memory_in_first_cycle ? "true" : "false");
}

inline std::string GetStrictTestHardwareModuleConfig(
    std::string_view module_name = "my_test_hardware_module",
    std::string_view interface_prefix = "joint",
    std::string_view strict_dio_command_interface_name =
        "strict_dio_command_interface_name",
    intrinsic::Duration control_period = kDefaultTestControlPeriod) {
  return absl::StrFormat(
      R"pb(
        name: "%s"
        disable_malloc_guard: true
        control_period_ns: %v
        module_config {
          [type.googleapis.com/intrinsic_proto.testing.TestHardwareModule] {
            interface_prefix: "%s"
            strict_dio_command_interface_name: "%s"
          }
        }
      )pb",
      module_name, intrinsic::ToInt64Nanoseconds(control_period),
      interface_prefix, strict_dio_command_interface_name);
}

}  // namespace test_hardware_module

#endif  // INTRINSIC_ICON_HAL_TEST_TEST_HARDWARE_MODULE_CONFIG_H_
