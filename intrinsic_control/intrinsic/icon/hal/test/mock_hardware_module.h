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

#ifndef INTRINSIC_ICON_HAL_TEST_MOCK_HARDWARE_MODULE_H_
#define INTRINSIC_ICON_HAL_TEST_MOCK_HARDWARE_MODULE_H_

#include <gmock/gmock.h>

#include "absl/status/status.h"
#include "intrinsic/icon/hal/hardware_module_init_context.h"
#include "intrinsic/icon/hal/hardware_module_interface.h"
#include "intrinsic/icon/utils/realtime_status.h"

namespace intrinsic::icon {

class MockHardwareModule : public HardwareModuleInterface {
 public:
  MOCK_METHOD(absl::Status, Init, (HardwareModuleInitContext & init_context),
              (override));
  MOCK_METHOD(absl::Status, Prepare, (), (override));
  MOCK_METHOD(RealtimeStatus, Activate, (), (override));
  MOCK_METHOD(RealtimeStatus, Deactivate, (), (override));
  MOCK_METHOD(absl::Status, EnableMotion, (), (override));
  MOCK_METHOD(RealtimeStatus, Enabled, (), (override));
  MOCK_METHOD(RealtimeStatus, Disabled, (), (override));
  MOCK_METHOD(absl::Status, DisableMotion, (), (override));
  MOCK_METHOD(absl::Status, ClearFaults, (), (override));
  MOCK_METHOD(absl::Status, Shutdown, (), (override));
  MOCK_METHOD(RealtimeStatus, ReadStatus, (), (override));
  MOCK_METHOD(RealtimeStatus, ApplyCommand, (), (override));
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_HAL_TEST_MOCK_HARDWARE_MODULE_H_
