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

#include "intrinsic/icon/actions/point_to_point_move_signature.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "intrinsic/icon/actions/point_to_point_move_info.h"
#include "intrinsic/icon/proto/v1/types.pb.h"

namespace intrinsic::icon {
namespace {

TEST(PointToPointMove, GetSignature) {
  intrinsic_proto::icon::v1::ActionSignature signature =
      GetPointToPointMoveSignature();
  EXPECT_EQ(signature.action_type_name(),
            PointToPointMoveInfo::kActionTypeName);
  EXPECT_EQ(signature.text_description(),
            PointToPointMoveInfo::kActionDescription);
  EXPECT_THAT(signature.behavior_override_infos(),
              testing::Contains(testing::Property(
                  &intrinsic_proto::icon::v1::ActionSignature::
                      BehaviorOverrideInfo::override_request,
                  intrinsic_proto::icon::v1::BehaviorOverrideRequest::
                      BEHAVIOR_OVERRIDE_REQUEST_PAUSE)));
}

}  // namespace
}  // namespace intrinsic::icon
