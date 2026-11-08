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

#include "intrinsic/icon/actions/tare_force_torque_sensor_signature.h"

#include <gtest/gtest.h>

#include "intrinsic/icon/actions/tare_force_torque_sensor_info.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {
namespace {

TEST(ForceTorqueSensorTare, GetSignature) {
  intrinsic_proto::icon::v1::ActionSignature signature =
      GetForceTorqueSensorTareSignature();
  EXPECT_EQ(signature.action_type_name(),
            TareForceTorqueSensorInfo::kActionTypeName);
  EXPECT_EQ(signature.text_description(),
            TareForceTorqueSensorInfo::kActionDescription);
}

}  // namespace
}  // namespace intrinsic::icon
