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

#include "intrinsic/icon/equipment/testing/equipment_test_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "intrinsic/icon/equipment/force_control_settings.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::intrinsic::ParseTextProtoOrDie;
using ::protobuf_matchers::EqualsProto;
using ::protobuf_matchers::proto::IgnoringRepeatedFieldOrdering;
using ::testing::ElementsAre;
using ::testing::Pair;

const char kSkillID[] = "package.skill_name";
const char kEquipmentSlot[] = "equipment_slot";
const char kRobotId[] = "robot_id";
const char kOtherRobotId[] = "other_robot_id";
const char kTarget[] = "target";
const char kPositionPart[] = "position_part";
const char kOtherPositionPart[] = "other_position_part";
const char kGripperPart[] = "gripper_part";
const char kAdioPart[] = "adio_part";
const char kOtherAdioPart[] = "other_adio_part";
const char kForceTorqueSensorPart[] = "ft_sensor_part";
const char kRangefinderPart[] = "rangefinder_part";

intrinsic_proto::icon::ForceControlSettings ForceControlSettings() {
  intrinsic_proto::icon::ForceControlSettings settings = ParseTextProtoOrDie(
      R"pb(
        excessive_force_threshold: 10.0
        excessive_torque_threshold: 10.0
        virtual_translational_inertia: 1.0
        virtual_rotational_inertia: 1.0
        sensed_wrench_deadband { x: 1.0 y: 1.0 z: 1.0 rx: 1.0 ry: 1.0 rz: 1.0 }
      )pb");

  return settings;
}

TEST(Icon2EquipmentHandleBuilderTest, MakePlainIcon2RobotHandle) {
  intrinsic_proto::resources::ResourceHandle expected = ParseTextProtoOrDie(
      R"pb(
        name: "robot_id"
        resource_data { key: "intrinsic_proto.icon.v1.IconApi" },
        connection_info {
          grpc {
            address: "target",
            server_instance: "robot_id",
          }
        }
      )pb");

  EXPECT_THAT(icon::Icon2EquipmentHandleBuilder(kRobotId, kTarget).Build(),
              EqualsProto(expected));
  EXPECT_THAT(icon::Icon2EquipmentHandleBuilder(kRobotId, kTarget)
                  .BuildMapForEquipmentSlot(kEquipmentSlot),
              ElementsAre(Pair(kEquipmentSlot, EqualsProto(expected))));
}

TEST(Icon2EquipmentHandleBuilderTest, MakeFullIcon2RobotHandle) {
  intrinsic_proto::resources::ResourceHandle expected = ParseTextProtoOrDie(
      R"pb(
        name: "robot_id"
        resource_data { key: "intrinsic_proto.icon.v1.IconApi" }
        resource_data {
          key: "Icon2PositionPart"
          value {
            contents {
              [type.googleapis.com/intrinsic_proto.icon.Icon2PositionPart] {
                part_name: "position_part",
                world_robot_collection_name: "robot_id"
                object_names { key: "position_part" value: "robot_id" }
                object_names {
                  key: "other_position_part"
                  value: "other_robot_id"
                }
              }
            }
          }
        }
        resource_data {
          key: "Icon2GripperPart"
          value {
            contents {
              [type.googleapis.com/intrinsic_proto.icon.Icon2GripperPart] {
                part_name: "gripper_part",
                part_names: "gripper_part"
              }
            }
          }
        }
        resource_data {
          key: "Icon2AdioPart"
          value {
            contents {
              [type.googleapis.com/intrinsic_proto.icon.Icon2AdioPart] {
                icon_target {
                  part_name: "adio_part",
                }
                icon_parts: "adio_part"
                icon_parts: "other_adio_part"
              }
            }
          }
        }
        resource_data {
          key: "Icon2ForceTorqueSensorPart"
          value {
            contents {
              [type.googleapis.com/
               intrinsic_proto.icon.Icon2ForceTorqueSensorPart] {
                part_name: "ft_sensor_part",
                force_control_settings {
                  excessive_force_threshold: 10.0
                  excessive_torque_threshold: 10.0
                  virtual_translational_inertia: 1.0
                  virtual_rotational_inertia: 1.0
                  sensed_wrench_deadband {
                    x: 1.0
                    y: 1.0
                    z: 1.0
                    rx: 1.0
                    ry: 1.0
                    rz: 1.0
                  }
                }
                settings: {
                  key: "ft_sensor_part"
                  value {
                    excessive_force_threshold: 10.0
                    excessive_torque_threshold: 10.0
                    virtual_translational_inertia: 1.0
                    virtual_rotational_inertia: 1.0
                    sensed_wrench_deadband {
                      x: 1.0
                      y: 1.0
                      z: 1.0
                      rx: 1.0
                      ry: 1.0
                      rz: 1.0
                    }
                  }
                }
              }
            }
          }
        }
        resource_data {
          key: "Icon2RangefinderPart"
          value {
            contents {
              [type.googleapis.com/intrinsic_proto.icon.Icon2RangefinderPart] {
                part_name: "rangefinder_part",
                part_names: "rangefinder_part"
              }
            }
          }
        }
        connection_info {
          grpc {
            address: "target",
            server_instance: "robot_id",
          }
        }
      )pb");

  EXPECT_THAT(icon::Icon2EquipmentHandleBuilder(kRobotId, kTarget)
                  .WithPositionControlledPart(kPositionPart)
                  .WithPositionControlledPart(kOtherPositionPart, kOtherRobotId)
                  .WithGripperPart(kGripperPart)
                  .WithAdioPart(kAdioPart)
                  .WithAdioPart(kOtherAdioPart)
                  .WithForceTorqueSensorPart(kForceTorqueSensorPart,
                                             ForceControlSettings())
                  .WithRangefinderPart(kRangefinderPart)
                  .Build(),
              IgnoringRepeatedFieldOrdering(EqualsProto(expected)));
  EXPECT_THAT(icon::Icon2EquipmentHandleBuilder(kRobotId, kTarget)
                  .WithPositionControlledPart(kPositionPart)
                  .WithPositionControlledPart(kOtherPositionPart, kOtherRobotId)
                  .WithGripperPart(kGripperPart)
                  .WithAdioPart(kAdioPart)
                  .WithAdioPart(kOtherAdioPart)
                  .WithForceTorqueSensorPart(kForceTorqueSensorPart,
                                             ForceControlSettings())
                  .WithRangefinderPart(kRangefinderPart)
                  .BuildMapForEquipmentSlot(kEquipmentSlot),
              ElementsAre(Pair(kEquipmentSlot, IgnoringRepeatedFieldOrdering(
                                                   EqualsProto(expected)))));
}

TEST(Icon2SkillInstanceBuilderTest, MakePlainIcon2RobotSkillInstance) {
  intrinsic_proto::skills::SkillInstance generated =
      icon::Icon2SkillInstanceBuilder(kSkillID, kTarget, kEquipmentSlot,
                                      kRobotId)
          .Build();

  intrinsic_proto::skills::SkillInstance expected = ParseTextProtoOrDie(
      R"pb(
        instance_name: "test_instance.package.skill_name"
        id_version: "package.skill_name.0.0.1"
        resource_handles: {
          key: "equipment_slot"
          value: {
            name: "robot_id"
            resource_data { key: "intrinsic_proto.icon.v1.IconApi" }
            connection_info {
              grpc {
                address: "target",
                server_instance: "robot_id",
              }
            }
          }
        }
      )pb");

  EXPECT_THAT(generated, EqualsProto(expected));
}

TEST(Icon2SkillInstanceBuilderTest, MakeFullIcon2RobotSkillInstance) {
  intrinsic_proto::skills::SkillInstance generated =
      icon::Icon2SkillInstanceBuilder(kSkillID, kTarget, kEquipmentSlot,
                                      kRobotId)
          .WithPositionControlledPart(kPositionPart)
          .WithPositionControlledPart(kOtherPositionPart, kOtherRobotId)
          .WithGripperPart(kGripperPart)
          .WithAdioPart(kAdioPart)
          .WithAdioPart(kOtherAdioPart)
          .WithForceTorqueSensorPart(kForceTorqueSensorPart)
          .WithRangefinderPart(kRangefinderPart)
          .Build();

  intrinsic_proto::skills::SkillInstance expected = ParseTextProtoOrDie(
      R"pb(
        instance_name: "test_instance.package.skill_name"
        id_version: "package.skill_name.0.0.1"
        resource_handles: {
          key: "equipment_slot"
          value: {
            name: "robot_id"
            resource_data { key: "intrinsic_proto.icon.v1.IconApi" }
            resource_data {
              key: "Icon2PositionPart"
              value {
                contents {
                  [type.googleapis.com/intrinsic_proto.icon.Icon2PositionPart] {
                    part_name: "position_part",
                    world_robot_collection_name: "robot_id"
                    object_names { key: "position_part" value: "robot_id" }
                    object_names {
                      key: "other_position_part"
                      value: "other_robot_id"
                    }
                  }
                }
              }
            }
            resource_data {
              key: "Icon2GripperPart"
              value {
                contents {
                  [type.googleapis.com/intrinsic_proto.icon.Icon2GripperPart] {
                    part_name: "gripper_part",
                    part_names: "gripper_part"
                  }
                }
              }
            }
            resource_data {
              key: "Icon2AdioPart"
              value {
                contents {
                  [type.googleapis.com/intrinsic_proto.icon.Icon2AdioPart] {
                    icon_target {
                      part_name: "adio_part",
                    }
                    icon_parts: "adio_part"
                    icon_parts: "other_adio_part"
                  }
                }
              }
            }
            resource_data {
              key: "Icon2ForceTorqueSensorPart"
              value {
                contents {
                  [type.googleapis.com/
                   intrinsic_proto.icon.Icon2ForceTorqueSensorPart] {
                    part_name: "ft_sensor_part",
                  }
                }
              }
            }
            resource_data {
              key: "Icon2RangefinderPart"
              value {
                contents {
                  [type.googleapis.com/
                   intrinsic_proto.icon.Icon2RangefinderPart] {
                    part_name: "rangefinder_part",
                    part_names: "rangefinder_part"
                  }
                }
              }
            }
            connection_info {
              grpc {
                address: "target",
                server_instance: "robot_id",
              }
            }
          }
        },
      )pb");

  EXPECT_THAT(generated, IgnoringRepeatedFieldOrdering(EqualsProto(expected)));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
