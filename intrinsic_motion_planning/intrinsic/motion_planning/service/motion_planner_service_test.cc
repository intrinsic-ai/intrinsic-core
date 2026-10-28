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

#include "intrinsic/motion_planning/service/motion_planner_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/channel_arguments.h"
#include "internal/testing.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/service/in_memory_geometry_service.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/logging/proto/logger_service_mock.grpc.pb.h"
#include "intrinsic/logging/structured_logging_client.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_test_util.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/proto/motion_planner_service_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/compute_ik.pb.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/service/cache/motion_planner_cache.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/service/motion_planner_service_base_test_fixture.h"
#include "intrinsic/motion_planning/service/motion_planner_service_proxy.h"
#include "intrinsic/motion_planning/service/motion_planner_service_utils.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.pb.h"
#include "intrinsic/motion_planning/skills/motion_planning_util.h"
#include "intrinsic/skills/internal/world_service_utils.h"
#include "intrinsic/storage/content_addressable_storage/cpp/client_helpers.h"
#include "intrinsic/storage/content_addressable_storage/testing/in_memory_cas_service.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_checker.h"
#include "intrinsic/world/collision/util/make_collision_settings.h"
#include "intrinsic/world/component/robot_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/service/objects/object_world_converter.h"
#include "intrinsic/world/service/objects/world_comparator.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"
#include "intrinsic/world/world.pb.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"
#include "re2/re2.h"

namespace intrinsic {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::intrinsic::CreateDefault4RobotsCubeWorld;
using ::intrinsic::ParseTextProtoOrDie;
using ::protobuf_matchers::EqualsProto;
using ::protobuf_matchers::proto::IgnoringFieldPaths;
using ::protobuf_matchers::proto::IgnoringRepeatedFieldOrdering;
using ::testing::_;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::Pointwise;

using ::google::protobuf::Empty;
using ::intrinsic_proto::motion_planning::v1::BatchCheckRequest;
using ::intrinsic_proto::motion_planning::v1::BatchCheckResponse;
using ::intrinsic_proto::motion_planning::v1::BatchCollisionQueryResult;
using ::intrinsic_proto::motion_planning::v1::CheckCollisionsRequest;
using ::intrinsic_proto::motion_planning::v1::CheckCollisionsResponse;
using ::intrinsic_proto::motion_planning::v1::FkRequest;
using ::intrinsic_proto::motion_planning::v1::FkResponse;
using ::intrinsic_proto::motion_planning::v1::IkRequest;
using ::intrinsic_proto::motion_planning::v1::IkResponse;
using ::intrinsic_proto::motion_planning::v1::MotionPlanningRequest;
using ::intrinsic_proto::motion_planning::v1::PathPlanningResponse;
using ::intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse;
using JointConfigurationValidationResultProto =
    intrinsic_proto::motion_planning::v1::JointConfigurationValidationResult;

TEST(MotionPlannerServiceConfigProtoTest, FromProto) {
  intrinsic_proto::motion_planning::v1::MotionPlannerServiceConfig proto_config;
  proto_config.mutable_flags()->set_enable_path_refinement_validation_step(
      true);
  proto_config.set_enable_motion_planner_service_caching_fuzzy_check(true);
  proto_config.set_enable_motion_planner_service_swept_volumes(true);
  proto_config.set_joint_tolerance_in_rad(0.005);
  proto_config.set_log_all_recordings(true);
  MotionPlannerServiceConfig config;
  FromProto(proto_config, config);

  // Expect that `FromProto()` populates the configuration fields from
  // `proto_config`.
  EXPECT_TRUE(config.flags.enable_path_refinement_validation_step);
  EXPECT_TRUE(config.enable_motion_planner_service_caching_fuzzy_check);
  EXPECT_TRUE(config.enable_motion_planner_service_swept_volumes);
  EXPECT_DOUBLE_EQ(
      config.max_diff_in_rad_for_starting_robot_configuration_threshold, 0.005);
  EXPECT_TRUE(config.log_all_recordings);
}
class MotionPlannerServiceWithSweptVolumesDisabledTest
    : public MotionPlannerServiceBaseTest,
      public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_OK_AND_ASSIGN(auto worlds, CreateDefault4RobotsCubeWorld(
                                          /*set_infinite_jerk_limits=*/true));
    MotionPlannerFlags motion_planner_flags;
    ASSERT_OK(CheckJerkLimitsSupported(/*set_infinite_jerk_limits=*/true));

    Init(/*use_nonvolatile_cache=*/false, /*extra_flags=*/motion_planner_flags,
         /*worlds_by_name=*/std::move(worlds),
         /*logger_client=*/nullptr,
         /*enable_motion_planner_service_swept_volumes=*/false);
  }

  static intrinsic_proto::motion_planning::v1::MotionPlanningRequest
  CreateDefaultTestMotionPlanningRequest() {
    return ParseTextProtoOrDie(R"pb(
      world_id: "world"
      motion_specification {
        motion_segments {
          target {
            joint_position { joints: [ -0.10, 0.48, -1.36, -0.77, 0.83, -1.0 ] }
          }
        }
      }
      robot_specification {
        robot_reference { object_id { by_name { object_name: "agilus_04" } } }
      }
    )pb");
  }
};

TEST_F(MotionPlannerServiceWithSweptVolumesDisabledTest,
       PlanTrajectoryFailsWhenSweptVolumesDisabled) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      CreateDefaultTestMotionPlanningRequest();
  request.set_compute_swept_volume(true);
  EXPECT_THAT(
      PlanTrajectory(request),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("Swept volume computation is not enabled for this "
                         "MotionPlannerService instance.")));
}

TEST_F(MotionPlannerServiceWithSweptVolumesDisabledTest,
       PlanPathFailsWhenSweptVolumesDisabled) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      CreateDefaultTestMotionPlanningRequest();
  request.set_compute_swept_volume(true);
  EXPECT_THAT(
      PlanPath(request),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("Swept volume computation is not enabled for this "
                         "MotionPlannerService instance.")));
}

TEST_F(MotionPlannerServiceWithSweptVolumesDisabledTest,
       PlanTrajectorySucceedsWhenComputeSweptVolumeNotRequested) {
  const intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      CreateDefaultTestMotionPlanningRequest();
  ASSERT_OK_AND_ASSIGN(
      const intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse
          response,
      PlanTrajectory(request));
  EXPECT_THAT(response.swept_volumes(), ::testing::IsEmpty());
}

TEST_F(MotionPlannerServiceWithSweptVolumesDisabledTest,
       PlanPathSucceedsWhenComputeSweptVolumeNotRequested) {
  const intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      CreateDefaultTestMotionPlanningRequest();
  ASSERT_OK_AND_ASSIGN(
      const intrinsic_proto::motion_planning::v1::PathPlanningResponse response,
      PlanPath(request));
  EXPECT_THAT(response.swept_volumes(), ::testing::IsEmpty());
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, ValidIk) {
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position { x: 0.1 y: 0 z: 0 }
              orientation { w: 1 x: 0 y: 0 z: 0 }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(request));
  // At least one solutions should be found.
  ASSERT_GE(result.solutions_size(), 1);
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       IkDoesNotCheckCollisionsByDefault) {
  // The offset here is specifically chosen so that it is reachable, but there
  // are no collision-free solutions. This confirms that the collision checking
  // is turned on when requested.
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position {
                x: 0.46446806722712169
                y: -1.1102230246251565e-16
                z: 0.076376560949138583
              }
              orientation {
                x: 1.0524250304047396e-19
                y: 0.99989339507657427
                z: -2.1840039627915106e-19
                w: -0.014601317825520939
              }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(request));
  // At least one solutions should be found.
  ASSERT_GE(result.solutions_size(), 1);
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, IkWithCollisionChecking) {
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position { x: 0.1 y: 0 z: 0 }
              orientation { w: 1 x: 0 y: 0 z: 0 }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        collision_settings {}
      )pb");

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(request));
  // At least one solutions should be found.
  ASSERT_GE(result.solutions_size(), 1);
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       IkWithCollisionCheckingNoSolution) {
  // The offset here is specifically chosen so that it is reachable, but there
  // are no collision-free solutions. This confirms that the collision checking
  // is turned on when requested.
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position {
                x: 0.46446806722712169
                y: -1.1102230246251565e-16
                z: 0.076376560949138583
              }
              orientation {
                x: 1.0524250304047396e-19
                y: 0.99989339507657427
                z: -2.1840039627915106e-19
                w: -0.014601317825520939
              }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        collision_settings {}
      )pb");

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  EXPECT_THAT(
      ComputeIk(request),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("IK could not find a collision free configuration.")));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, IkWithoutCollisionChecking) {
  // The offset here is specifically chosen so that it is reachable, but there
  // are no collision-free solutions. In this case, we turn off collision
  // checking and verify that we got back at least one solution.
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position {
                x: 0.46446806722712169
                y: -1.1102230246251565e-16
                z: 0.076376560949138583
              }
              orientation {
                x: 1.0524250304047396e-19
                y: 0.99989339507657427
                z: -2.1840039627915106e-19
                w: -0.014601317825520939
              }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(request));
  // All solutions are expected to be in collision.
  ASSERT_GE(result.solutions_size(), 1);
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, IkStartingJoints) {
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
          }
        }
        starting_joints { joints: [ 0.3, 0.3, 0.3, 0.3, 0.3, 0.3 ] }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        max_num_solutions: 1
      )pb");

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(request));
  // At least one solutions should be found.
  ASSERT_EQ(result.solutions_size(), 1);
  // The solution returned should be close the same as the starting joint
  // values (because of how we specified the target).
  EXPECT_THAT(result.solutions().at(0).joints(),
              Pointwise(DoubleNear(1e-8), {0.3, 0.3, 0.3, 0.3, 0.3, 0.3}));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, EnsureSameBranchIkWorks) {
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position { x: 0.1 y: 0 z: 0 }
              orientation { w: 1 x: 0 y: 0 z: 0 }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        ensure_same_branch: true
      )pb");

  // Ensure that same branch IK flag is set to true but prefer same branch IK
  // flag is false.
  EXPECT_TRUE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(request));
  // Only one solution should be found.
  EXPECT_THAT(result.solutions_size(), 1);
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, EnsureSameBranchIkFails) {
  // The offset here is specifically chosen so that it is reachable, but not
  // reachable with the same branch as the robot state in the world.
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position { x: 0.3 y: 0.0 z: 0.0 }
              orientation {
                x: 1.0524250304047396e-19
                y: 0.99989339507657427
                z: -2.1840039627915106e-19
                w: -0.014601317825520939
              }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        ensure_same_branch: true
      )pb");

  // Ensure that same branch IK flag is set to true but prefer same branch IK
  // flag is false.
  EXPECT_TRUE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  EXPECT_THAT(ComputeIk(request),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("no IK solution on the same branch")));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       EnsureSameBranchIkWithCollisionChecking) {
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position { x: 0.1 y: 0 z: 0 }
              orientation { w: 1 x: 0 y: 0 z: 0 }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        collision_settings {}
        ensure_same_branch: true
      )pb");

  // Ensure that collision checking is enabled.
  EXPECT_TRUE(request.has_collision_settings());
  EXPECT_FALSE(request.collision_settings().disable_collision_checking());

  // Ensure that same branch IK flag is set to true but prefer same branch IK
  // flag is false.
  EXPECT_TRUE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(request));
  // Only one solution should be found.
  EXPECT_THAT(result.solutions_size(), 1);
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       EnsureSameBranchIkWithCollisionCheckingNoSolution) {
  // The offset here is specifically chosen so that it is reachable, but there
  // are no collision-free solutions. This proto is first used to obtain an IK
  // solution without collision checking which will then be used as starting
  // joints for computing same branch IK with collision checking.
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position {
                x: 0.46446806722712169
                y: -1.1102230246251565e-16
                z: 0.076376560949138583
              }
              orientation {
                x: 1.0524250304047396e-19
                y: 0.99989339507657427
                z: -2.1840039627915106e-19
                w: -0.014601317825520939
              }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        max_num_solutions: 1
      )pb");

  // Ensure that collision checking is disabled.
  EXPECT_FALSE(request.has_collision_settings());

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  // Compute an IK solution. The IK solution will be used to specify starting
  // joints for performing same branch IK with collision checking below.
  ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(request));
  ASSERT_EQ(result.solutions_size(), 1);

  // Since the above IK solution already accounts for the offset and specifying
  // starting_joints updates the robot state in the world, the offset is removed
  // for performing same branch IK with starting joints and collision checking
  // enabled. It is expected that the same branch IK solution is in collision.
  intrinsic_proto::motion_planning::v1::IkRequest same_branch_request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        collision_settings {}
        ensure_same_branch: true
      )pb");

  // Ensure that collision checking is enabled.
  EXPECT_TRUE(same_branch_request.has_collision_settings());
  EXPECT_FALSE(
      same_branch_request.collision_settings().disable_collision_checking());

  // Ensure that same branch IK flag is set to true but prefer same branch IK
  // flag is false.
  EXPECT_TRUE(same_branch_request.ensure_same_branch());
  EXPECT_FALSE(same_branch_request.prefer_same_branch());

  // Set starting_joints to the IK solution above and compute same branch IK
  // with collision checking enabled. It is expected that same branch IK is
  // infeasible due to collision.
  auto solution = result.solutions()[0];
  same_branch_request.mutable_starting_joints()->mutable_joints()->Assign(
      solution.joints().begin(), solution.joints().end());

  // Ensure that the starting joints are set to the solution.
  EXPECT_THAT(same_branch_request.starting_joints().joints(),
              Pointwise(DoubleEq(), solution.joints()));

  EXPECT_THAT(
      ComputeIk(same_branch_request),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("IK could not find a collision free configuration.")));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       EnsureSameBranchIkStartingJoints) {
  // Create an IkRequest with offset to obtain all possible IK solutions for the
  // pose. The offset is specifically chosen such that there are more than one
  // IK solutions.
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position { x: 0.1 y: 0 z: 0 }
              orientation { w: 1 x: 0 y: 0 z: 0 }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  // Get all possible IK solutions and ensure there are more than one solutions.
  // Each of the IK solutions will be used to specify starting joints for
  // performing same branch IK below.
  ASSERT_OK_AND_ASSIGN(auto ik_solutions, ComputeIk(request));
  ASSERT_GT(ik_solutions.solutions_size(), 1);

  // Since the above IK solutions already account for the offset and specifying
  // starting_joints updates the robot state in the world, the offset is removed
  // for performing same branch IK with starting joints.
  intrinsic_proto::motion_planning::v1::IkRequest same_branch_request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        ensure_same_branch: true
      )pb");

  // Ensure that same branch IK flag is set to true but prefer same branch IK
  // flag is false.
  EXPECT_TRUE(same_branch_request.ensure_same_branch());
  EXPECT_FALSE(same_branch_request.prefer_same_branch());

  // Set starting_joints to each of the IK solutions above and compute same
  // branch IK. It is expected that the returned solution is the same as the set
  // starting_joints.
  for (auto solution : ik_solutions.solutions()) {
    same_branch_request.mutable_starting_joints()->mutable_joints()->Assign(
        solution.joints().begin(), solution.joints().end());

    // Ensure that the starting joints are set to the solution.
    EXPECT_THAT(same_branch_request.starting_joints().joints(),
                Pointwise(DoubleEq(), solution.joints()));

    // Check that the computed same branch IK solution is the same as the
    // starting joints.
    ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(same_branch_request));
    EXPECT_EQ(result.solutions_size(), 1);
    EXPECT_THAT(result.solutions().at(0).joints(),
                Pointwise(DoubleNear(1e-8),
                          same_branch_request.starting_joints().joints()));
  }
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PreferSameBranchIkStartingJoints) {
  // Create an IkRequest with offset to obtain all possible IK solutions for the
  // pose. The offset is specifically chosen such that there are more than one
  // IK solutions.
  intrinsic_proto::motion_planning::v1::IkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
            target_frame_offset {
              position { x: 0.1 y: 0 z: 0 }
              orientation { w: 1 x: 0 y: 0 z: 0 }
            }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  // Ensure that same branch IK flags are false by default.
  EXPECT_FALSE(request.ensure_same_branch());
  EXPECT_FALSE(request.prefer_same_branch());

  // Get all possible IK solutions and ensure there are more than one solutions.
  // Each of the IK solutions will be used to specify starting joints for
  // performing same branch IK below.
  ASSERT_OK_AND_ASSIGN(auto ik_solutions, ComputeIk(request));
  ASSERT_GT(ik_solutions.solutions_size(), 1);

  // Since the above IK solutions already account for the offset and specifying
  // starting_joints updates the robot state in the world, the offset is removed
  // for performing same branch IK with starting joints.
  intrinsic_proto::motion_planning::v1::IkRequest prefer_same_branch_request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        target {
          cartesian_pose {
            target_frame { by_name { object { object_name: "iris622_tool" } } }
            moving_frame { by_name { object { object_name: "iris622_tool" } } }
          }
        }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        prefer_same_branch: true
      )pb");

  // Ensure that same branch IK is set to true.
  EXPECT_TRUE(prefer_same_branch_request.prefer_same_branch());

  // Set starting_joints to each of the IK solutions above and compute IK with
  // prefer same branch flag as true. It is expected that the first solution in
  // the returned IK solutions is the same as the set starting_joints.
  for (auto solution : ik_solutions.solutions()) {
    prefer_same_branch_request.mutable_starting_joints()
        ->mutable_joints()
        ->Assign(solution.joints().begin(), solution.joints().end());

    // Ensure that the starting joints are set to the solution.
    EXPECT_THAT(prefer_same_branch_request.starting_joints().joints(),
                Pointwise(DoubleEq(), solution.joints()));

    // Check that the first IK solution is the same as the starting joints and
    // the number of solutions is the same as without the flag.
    ASSERT_OK_AND_ASSIGN(auto result, ComputeIk(prefer_same_branch_request));
    EXPECT_THAT(
        result.solutions().at(0).joints(),
        Pointwise(DoubleNear(1e-8),
                  prefer_same_branch_request.starting_joints().joints()));
    EXPECT_EQ(result.solutions_size(), ik_solutions.solutions_size());
  }
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, ValidFk) {
  intrinsic_proto::motion_planning::v1::FkRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        joints { joints: [ 0.3, 0.3, 0.3, 0.3, 0.3, 0.3 ] }
        reference { by_name { object { object_name: "iris622" } } }
        target { by_name { object { object_name: "iris622_tool" } } }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  ASSERT_OK_AND_ASSIGN(auto result, ComputeFk(request));
  ASSERT_OK_AND_ASSIGN(auto reference_t_target,
                       FromProto(result.reference_t_target()));

  EXPECT_THAT(reference_t_target.translation(),
              Pointwise(DoubleNear(1e-8), {
                                              0.83555583794703825,
                                              -0.26669506936708615,
                                              -0.020497142151342129,
                                          }));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, CheckCollisions) {
  // The joint values there are specifically chosen so that a collision occurs
  // somewhere in the middle of the path.
  intrinsic_proto::motion_planning::v1::CheckCollisionsRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        waypoint { joints: [ 0, -0.6, 1.7, 0, 0, 0 ] }
        waypoint { joints: [ 1.2, -0.6, 1.7, 0, 0, 0 ] }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  ASSERT_OK_AND_ASSIGN(auto result, CheckCollisions(request));
  EXPECT_TRUE(result.has_collision());
  EXPECT_THAT(result.collision_debug_msg(),
              HasSubstr("Invalid path segment (most likely due to collisions) "
                        "detected between point"));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, CheckCollisionsNoCollisions) {
  // The joint values there are specifically chosen so that no collision
  // occurs on the path.
  intrinsic_proto::motion_planning::v1::CheckCollisionsRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        waypoint { joints: [ 0, -2, 2, 0, 0, 0 ] }
        waypoint { joints: [ 1.2, -2, 2, 0, 0, 0 ] }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  ASSERT_OK_AND_ASSIGN(auto result, CheckCollisions(request));
  EXPECT_FALSE(result.has_collision());
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, CheckCollisionsEmptyPath) {
  // Invalid request with no waypoints.
  intrinsic_proto::motion_planning::v1::CheckCollisionsRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  EXPECT_THAT(CheckCollisions(request),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("The input path must contain at least one "
                                 "joint configuration.")));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, CheckCollisionsOnePoint) {
  intrinsic_proto::motion_planning::v1::CheckCollisionsRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        waypoint { joints: [ 0, -0.6, 1.7, 0, 0, 0 ] }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
      )pb");

  ASSERT_OK_AND_ASSIGN(auto result, CheckCollisions(request));
  EXPECT_FALSE(result.has_collision());
  EXPECT_THAT(result.collision_debug_msg(), HasSubstr("OK"));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       CheckCollisionsUsesCollisionSettings) {
  // The joint values there are specifically chosen so that no collision would
  // occur on the path if not for the margin setting.
  intrinsic_proto::motion_planning::v1::CheckCollisionsRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        waypoint { joints: [ 0, -2, 2, 0, 0, 0 ] }
        waypoint { joints: [ 1.2, -2, 2, 0, 0, 0 ] }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        collision_settings { minimum_margin: 10 }
      )pb");

  ASSERT_OK_AND_ASSIGN(auto result, CheckCollisions(request));
  EXPECT_TRUE(result.has_collision());
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, CheckCollisionsDisabled) {
  // The joint values there are specifically chosen so that a collision occurs
  // somewhere in the middle of the path.
  intrinsic_proto::motion_planning::v1::CheckCollisionsRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        waypoint { joints: [ 0, -0.6, 1.7, 0, 0, 0 ] }
        waypoint { joints: [ 1.2, -0.6, 1.7, 0, 0, 0 ] }
        robot_reference { object_id { by_name { object_name: "iris622" } } }
        collision_settings { disable_collision_checking: true }
      )pb");

  ASSERT_OK_AND_ASSIGN(auto result, CheckCollisions(request));
  EXPECT_FALSE(result.has_collision());
}

// Returns a snapshot of the world `world_id` as a client holding one would
// send it in `BatchCheckRequest.world_snapshot`: the objects from
// `ObjectWorldService.GetWorld` with `WORLD_VIEW_FULL` and the collision
// settings from `GetCollisionSettings`, which include the world's collision
// exclusion pairs (e.g. between adjacent robot links).
absl::StatusOr<intrinsic_proto::world::World> GetWorldSnapshot(
    intrinsic_proto::world::ObjectWorldService::StubInterface& stub,
    absl::string_view world_id) {
  intrinsic_proto::world::GetWorldRequest get_world_request;
  get_world_request.set_world_id(std::string(world_id));
  get_world_request.set_view(intrinsic_proto::world::WORLD_VIEW_FULL);
  grpc::ClientContext get_world_context;
  intrinsic_proto::world::World snapshot;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub.GetWorld(&get_world_context, get_world_request, &snapshot)));

  intrinsic_proto::world::GetCollisionSettingsRequest
      get_collision_settings_request;
  get_collision_settings_request.set_world_id(std::string(world_id));
  grpc::ClientContext get_collision_settings_context;
  INTR_RETURN_IF_ERROR(ToAbslStatus(stub.GetCollisionSettings(
      &get_collision_settings_context, get_collision_settings_request,
      snapshot.mutable_collision_settings())));
  return snapshot;
}

// Returns the object `object_name` in `snapshot`. The pointer is valid as long
// as no objects are added to `snapshot`.
absl::StatusOr<intrinsic_proto::world::Object*> FindObjectInSnapshot(
    absl::string_view object_name, intrinsic_proto::world::World& snapshot) {
  for (intrinsic_proto::world::Object& object : *snapshot.mutable_objects()) {
    if (object.name() == object_name) return &object;
  }
  return absl::NotFoundError(
      absl::StrCat("Object '", object_name, "' not in snapshot."));
}

// Returns the root entity of `object`.
absl::StatusOr<intrinsic_proto::world::Entity*> FindRootEntity(
    intrinsic_proto::world::Object& object) {
  const auto root_entity =
      object.mutable_entities()->find(object.root_entity_id());
  if (root_entity == object.mutable_entities()->end()) {
    return absl::NotFoundError(absl::StrCat(
        "Root entity of object '", object.name(), "' not in snapshot."));
  }
  return &root_entity->second;
}

// Moves the object `object_name` of `snapshot` to the pose of the object
// `target_object_name`. Both objects must be attached to the same parent
// entity. The pose is stored redundantly in the object and in its root entity,
// both are updated.
absl::Status MoveObjectInSnapshotOnto(absl::string_view object_name,
                                      absl::string_view target_object_name,
                                      intrinsic_proto::world::World& snapshot) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::Object* const object,
                        FindObjectInSnapshot(object_name, snapshot));
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::Object* const target_object,
                        FindObjectInSnapshot(target_object_name, snapshot));
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::Entity* const root_entity,
                        FindRootEntity(*object));
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::world::Entity* const target_root_entity,
      FindRootEntity(*target_object));
  if (root_entity->parent_id() != target_root_entity->parent_id()) {
    return absl::InvalidArgumentError(
        "Objects are attached to different parent entities.");
  }
  *root_entity->mutable_parent_t_this() = target_root_entity->parent_t_this();
  *object->mutable_object_component()->mutable_parent_t_this() =
      target_object->object_component().parent_t_this();
  return absl::OkStatus();
}

// Id of an entity that does not exist in the test world. Attaching an object to
// it makes a world snapshot invalid.
constexpr absl::string_view kNonexistentEntityId = "nonexistent_entity";

// Attaches the object `object_name` of `snapshot` to the entity
// `kNonexistentEntityId`, so that no world can be constructed from `snapshot`.
absl::Status AttachObjectInSnapshotToNonexistentParent(
    absl::string_view object_name, intrinsic_proto::world::World& snapshot) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::Object* const object,
                        FindObjectInSnapshot(object_name, snapshot));
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::Entity* const root_entity,
                        FindRootEntity(*object));
  root_entity->set_parent_id(std::string(kNonexistentEntityId));
  return absl::OkStatus();
}

// Verifies top-level validation rules for BatchCheckRequest.
TEST_P(MotionPlannerServiceWithoutLoggerTest, BatchCheckValidationFailures) {
  // Scenario 1: Neither world_snapshot nor world_id is set.
  const BatchCheckRequest request_no_world = ParseTextProtoOrDie(R"pb(
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    collision_queries { waypoints { joints: [ 0, -2, 2, 0, 0, 0 ] } }
  )pb");
  ASSERT_EQ(request_no_world.world_source_case(),
            BatchCheckRequest::WORLD_SOURCE_NOT_SET);
  EXPECT_THAT(BatchCheck(request_no_world),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("One of world_id or world_snapshot must be "
                                 "set.")));

  // Scenario 1b: world_id is selected but empty.
  BatchCheckRequest request_empty_world_id = request_no_world;
  request_empty_world_id.set_world_id("");
  ASSERT_EQ(request_empty_world_id.world_source_case(),
            BatchCheckRequest::kWorldId);
  EXPECT_THAT(BatchCheck(request_empty_world_id),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("world_id must not be empty.")));

  // Scenario 2: Neither IK queries nor collision queries are provided.
  const BatchCheckRequest request_no_queries = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
  )pb");
  EXPECT_THAT(
      BatchCheck(request_no_queries),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("BatchCheckRequest must contain at least one IK query "
                         "or collision query.")));

  // Scenario 3: Non-positive collision_check_spacing.
  BatchCheckRequest request_invalid_spacing = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    collision_queries {
      waypoints { joints: [ 0, -2, 2, 0, 0, 0 ] }
      waypoints { joints: [ 1.2, -2, 2, 0, 0, 0 ] }
    }
  )pb");
  // Non-positive collision check spacing used to verify validation rejection.
  constexpr double kInvalidCollisionCheckSpacing = 0.0;
  request_invalid_spacing.mutable_collision_queries(0)
      ->set_collision_check_spacing(kInvalidCollisionCheckSpacing);
  EXPECT_THAT(
      BatchCheck(request_invalid_spacing),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("collision_check_spacing must be positive. Got 0")));

  // Scenario 4: A world_snapshot from which no world can be constructed, here
  // because an object is attached to a parent entity that does not exist.
  BatchCheckRequest request_invalid_snapshot = ParseTextProtoOrDie(R"pb(
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    collision_queries { waypoints { joints: [ 0, -2, 2, 0, 0, 0 ] } }
  )pb");
  ASSERT_OK_AND_ASSIGN(*request_invalid_snapshot.mutable_world_snapshot(),
                       GetWorldSnapshot(*object_world_service_stub_, "world"));
  ASSERT_OK(AttachObjectInSnapshotToNonexistentParent(
      "workpiece", *request_invalid_snapshot.mutable_world_snapshot()));
  EXPECT_THAT(BatchCheck(request_invalid_snapshot),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid world_snapshot: ")));
}

// Verifies evaluating multiple IK queries in a single BatchCheck RPC using
// world_id.
TEST_P(MotionPlannerServiceWithoutLoggerTest, BatchCheckIkOnlyWithWorldId) {
  const BatchCheckRequest request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
    }
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.05 y: 0.05 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(const auto response, BatchCheck(request));
  ASSERT_EQ(response.ik_results_size(), 2);
  EXPECT_EQ(response.collision_results_size(), 0);

  for (const auto& ik_result : response.ik_results()) {
    EXPECT_TRUE(ik_result.success());
    EXPECT_GE(ik_result.solutions_size(), 1);
  }
}

// Verifies evaluating multiple collision queries (single configuration and
// multi-waypoint paths) in a single BatchCheck RPC.
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       BatchCheckCollisionOnlyWithWorldId) {
  const BatchCheckRequest request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    # Query 0: Single configuration, collision-free.
    collision_queries { waypoints { joints: [ 0, -0.6, 1.7, 0, 0, 0 ] } }
    # Query 1: Single configuration, in collision.
    collision_queries { waypoints { joints: [ 0.6, -0.6, 1.7, 0, 0, 0 ] } }
    # Query 2: Multi-waypoint path, collision-free.
    collision_queries {
      waypoints { joints: [ 0, -2, 2, 0, 0, 0 ] }
      waypoints { joints: [ 1.2, -2, 2, 0, 0, 0 ] }
    }
    # Query 3: Multi-waypoint path, in collision.
    collision_queries {
      waypoints { joints: [ 0, -0.6, 1.7, 0, 0, 0 ] }
      waypoints { joints: [ 1.2, -0.6, 1.7, 0, 0, 0 ] }
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(const auto response, BatchCheck(request));
  ASSERT_EQ(response.collision_results_size(), 4);
  EXPECT_EQ(response.ik_results_size(), 0);

  // Single config collision-free.
  EXPECT_TRUE(response.collision_results(0).success());
  EXPECT_FALSE(response.collision_results(0).has_collision());

  // Single config in collision.
  EXPECT_TRUE(response.collision_results(1).success());
  EXPECT_TRUE(response.collision_results(1).has_collision());

  // Multi-waypoint path collision-free.
  EXPECT_TRUE(response.collision_results(2).success());
  EXPECT_FALSE(response.collision_results(2).has_collision());

  // Multi-waypoint path in collision.
  EXPECT_TRUE(response.collision_results(3).success());
  EXPECT_TRUE(response.collision_results(3).has_collision());
}

// Verifies that starting_joints seed in one IK query does not bleed into
// subsequent queries.
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       BatchCheckCrossQuerySeedIndependence) {
  // Baseline request with no seed to capture the default unseeded solution.
  const BatchCheckRequest baseline_request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
      max_num_solutions: 1
    }
  )pb");
  ASSERT_OK_AND_ASSIGN(const auto baseline_response,
                       BatchCheck(baseline_request));
  ASSERT_EQ(baseline_response.ik_results_size(), 1);
  ASSERT_TRUE(baseline_response.ik_results(0).success());
  ASSERT_EQ(baseline_response.ik_results(0).solutions_size(), 1);
  const auto expected_unseeded_solution =
      baseline_response.ik_results(0).solutions(0);

  // Batch with Seed A in query 0, unseeded query in query 1, Seed B in query 2.
  const BatchCheckRequest request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
      starting_joints { joints: [ 0.1, -0.5, 1.5, 0.1, -0.2, 0.3 ] }
      max_num_solutions: 1
    }
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
      max_num_solutions: 1
    }
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
      starting_joints { joints: [ -0.2, 0.3, 1.0, -0.5, 0.4, -0.1 ] }
      max_num_solutions: 1
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(const auto response, BatchCheck(request));
  ASSERT_EQ(response.ik_results_size(), 3);
  for (const auto& ik_result : response.ik_results()) {
    EXPECT_TRUE(ik_result.success());
    EXPECT_EQ(ik_result.solutions_size(), 1);
  }

  // Unseeded query at index 1 must match the baseline unseeded solution.
  EXPECT_THAT(response.ik_results(1).solutions(0),
              EqualsProto(expected_unseeded_solution));
}

// Verifies evaluating both IK and collision queries combined within the same
// BatchCheck request.
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       BatchCheckCombinedIkAndCollisions) {
  const BatchCheckRequest request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
    }
    collision_queries {
      waypoints { joints: [ 0, -2, 2, 0, 0, 0 ] }
      waypoints { joints: [ 1.2, -2, 2, 0, 0, 0 ] }
    }
    collision_queries {
      waypoints { joints: [ 0, -0.6, 1.7, 0, 0, 0 ] }
      waypoints { joints: [ 1.2, -0.6, 1.7, 0, 0, 0 ] }
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(const auto response, BatchCheck(request));
  ASSERT_EQ(response.ik_results_size(), 1);
  EXPECT_TRUE(response.ik_results(0).success());
  EXPECT_GE(response.ik_results(0).solutions_size(), 1);

  ASSERT_EQ(response.collision_results_size(), 2);
  EXPECT_TRUE(response.collision_results(0).success());
  EXPECT_FALSE(response.collision_results(0).has_collision());
  EXPECT_TRUE(response.collision_results(1).success());
  EXPECT_TRUE(response.collision_results(1).has_collision());
}

// IK and collision queries used by the BatchCheck world snapshot tests. The
// collision queries are collision-free in the unmodified test world.
constexpr absl::string_view kBatchCheckSnapshotTestQueries = R"pb(
  robot_reference { object_id { by_name { object_name: "iris622" } } }
  ik_queries {
    target {
      cartesian_pose {
        target_frame { by_name { object { object_name: "iris622_tool" } } }
        moving_frame { by_name { object { object_name: "iris622_tool" } } }
        target_frame_offset {
          position { x: 0.1 y: 0 z: 0 }
          orientation { w: 1 x: 0 y: 0 z: 0 }
        }
      }
    }
  }
  collision_queries { waypoints { joints: [ 0, -2, 2, 0, 0, 0 ] } }
  collision_queries {
    waypoints { joints: [ 0, -2, 2, 0, 0, 0 ] }
    waypoints { joints: [ 1.2, -2, 2, 0, 0, 0 ] }
  }
)pb";

// Verifies that a `world_snapshot` of the world gives the same results as
// fetching the world via `world_id`.
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       BatchCheckWorldSnapshotMatchesWorldId) {
  BatchCheckRequest world_id_request =
      ParseTextProtoOrDie(kBatchCheckSnapshotTestQueries);
  world_id_request.set_world_id("world");
  ASSERT_OK_AND_ASSIGN(const BatchCheckResponse world_id_response,
                       BatchCheck(world_id_request));

  BatchCheckRequest snapshot_request =
      ParseTextProtoOrDie(kBatchCheckSnapshotTestQueries);
  ASSERT_OK_AND_ASSIGN(*snapshot_request.mutable_world_snapshot(),
                       GetWorldSnapshot(*object_world_service_stub_, "world"));
  ASSERT_OK_AND_ASSIGN(const BatchCheckResponse snapshot_response,
                       BatchCheck(snapshot_request));

  ASSERT_EQ(snapshot_response.ik_results_size(), 1);
  EXPECT_TRUE(snapshot_response.ik_results(0).success());
  ASSERT_EQ(snapshot_response.collision_results_size(), 2);
  EXPECT_FALSE(snapshot_response.collision_results(0).has_collision());
  EXPECT_FALSE(snapshot_response.collision_results(1).has_collision());
  EXPECT_THAT(snapshot_response, EqualsProto(world_id_response));
}

// Verifies that the queries are evaluated against the `world_snapshot` rather
// than the world in the world service.
TEST_P(MotionPlannerServiceWithoutLoggerTest, BatchCheckUsesWorldSnapshot) {
  // Moving an obstacle onto the robot's base makes every configuration of the
  // robot collide, but only in the snapshot.
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::world::World snapshot,
                       GetWorldSnapshot(*object_world_service_stub_, "world"));
  ASSERT_OK(MoveObjectInSnapshotOnto("workpiece", "iris622", snapshot));

  BatchCheckRequest request =
      ParseTextProtoOrDie(kBatchCheckSnapshotTestQueries);
  *request.mutable_world_snapshot() = snapshot;
  ASSERT_OK_AND_ASSIGN(const BatchCheckResponse response, BatchCheck(request));

  // No collision-free IK solution exists either.
  ASSERT_EQ(response.ik_results_size(), 1);
  EXPECT_FALSE(response.ik_results(0).success());
  // The collisions are caused by the moved workpiece, not by e.g. missing
  // collision exclusions between the robot's links.
  ASSERT_EQ(response.collision_results_size(), 2);
  for (const BatchCollisionQueryResult& result : response.collision_results()) {
    EXPECT_TRUE(result.success());
    EXPECT_TRUE(result.has_collision());
    EXPECT_THAT(result.collision_debug_msg(), HasSubstr("workpiece"));
  }

  // The world in the world service is unaffected.
  BatchCheckRequest world_id_request =
      ParseTextProtoOrDie(kBatchCheckSnapshotTestQueries);
  world_id_request.set_world_id("world");
  ASSERT_OK_AND_ASSIGN(const BatchCheckResponse world_id_response,
                       BatchCheck(world_id_request));
  ASSERT_EQ(world_id_response.ik_results_size(), 1);
  EXPECT_TRUE(world_id_response.ik_results(0).success());
  ASSERT_EQ(world_id_response.collision_results_size(), 2);
  EXPECT_FALSE(world_id_response.collision_results(0).has_collision());
  EXPECT_FALSE(world_id_response.collision_results(1).has_collision());
}

// Verifies that errors in individual queries do not fail the overall RPC and
// are isolated.
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       BatchCheckPerQueryErrorIsolation) {
  const BatchCheckRequest request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    # Query 0 (IK): Valid reachable target.
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
    }
    # Query 1 (IK): Invalid query with negative max_num_solutions.
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
      max_num_solutions: -1
    }
    # Query 2 (Collision): Valid collision query.
    collision_queries { waypoints { joints: [ 0, -2, 2, 0, 0, 0 ] } }
    # Query 3 (Collision): Invalid collision query with empty waypoints.
    collision_queries {}
  )pb");

  ASSERT_OK_AND_ASSIGN(const auto response, BatchCheck(request));

  // Verify IK results.
  ASSERT_EQ(response.ik_results_size(), 2);
  EXPECT_TRUE(response.ik_results(0).success());
  EXPECT_GE(response.ik_results(0).solutions_size(), 1);

  EXPECT_FALSE(response.ik_results(1).success());
  EXPECT_THAT(response.ik_results(1).error_message(),
              HasSubstr("max_num_solutions must be non-negative"));

  // Verify Collision results.
  ASSERT_EQ(response.collision_results_size(), 2);
  EXPECT_TRUE(response.collision_results(0).success());
  EXPECT_FALSE(response.collision_results(0).has_collision());

  EXPECT_FALSE(response.collision_results(1).success());
  EXPECT_THAT(response.collision_results(1).error_message(),
              HasSubstr("The input path must contain at least one joint "
                        "configuration."));
}

// Verifies that a failing batched IK query reports the same structured
// `MotionPlanningError` that the single-query ComputeIk RPC surfaces as a gRPC
// status detail, and that `error_message` is populated as well.
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       BatchCheckIkFailurePopulatesStructuredError) {
  const BatchCheckRequest request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    # Query 0 (IK): target 100 m from the world root, i.e. far outside the
    # reachable workspace of any robot in the test world.
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "root" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 100.0 y: 100.0 z: 100.0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
    }
    # Query 1 (IK): reachable target, used to check that the structured error
    # stays unset on success.
    ik_queries {
      target {
        cartesian_pose {
          target_frame { by_name { object { object_name: "iris622_tool" } } }
          moving_frame { by_name { object { object_name: "iris622_tool" } } }
          target_frame_offset {
            position { x: 0.1 y: 0 z: 0 }
            orientation { w: 1 x: 0 y: 0 z: 0 }
          }
        }
      }
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(const auto response, BatchCheck(request));
  ASSERT_EQ(response.ik_results_size(), 2);

  const auto& failed_result = response.ik_results(0);
  EXPECT_FALSE(failed_result.success());
  // `error_message` must contain the IK solver's message. It is the only
  // failure information available to callers built against the stripped proto.
  EXPECT_THAT(failed_result.error_message(),
              HasSubstr("IK solver couldn't find any solutions for the given "
                        "constraints"));
  ASSERT_TRUE(failed_result.has_error());
  ASSERT_EQ(
      failed_result.error().error_case(),
      intrinsic_proto::motion_planning::v1::MotionPlanningError::kIkError);
  EXPECT_EQ(failed_result.error().ik_error().error_context(),
            intrinsic_proto::motion_planning::v1::ErrorContext::
                IK_NO_SOLUTIONS_FOUND);
  EXPECT_EQ(failed_result.error().ik_error().error_message(),
            failed_result.error_message());

  const auto& successful_result = response.ik_results(1);
  EXPECT_TRUE(successful_result.success());
  EXPECT_FALSE(successful_result.has_error());
}

// Verifies that disabling collision checking applies properly in BatchCheck.
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       BatchCheckCollisionSettingsVariation) {
  // Path that collides under default collision checking.
  const BatchCheckRequest default_request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    collision_queries {
      waypoints { joints: [ 0, -0.6, 1.7, 0, 0, 0 ] }
      waypoints { joints: [ 1.2, -0.6, 1.7, 0, 0, 0 ] }
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(const auto default_response,
                       BatchCheck(default_request));
  ASSERT_EQ(default_response.collision_results_size(), 1);
  EXPECT_TRUE(default_response.collision_results(0).success());
  EXPECT_TRUE(default_response.collision_results(0).has_collision());

  // Same path evaluated with disable_collision_checking = true should not
  // detect collision.
  const BatchCheckRequest disabled_request = ParseTextProtoOrDie(R"pb(
    world_id: "world"
    robot_reference { object_id { by_name { object_name: "iris622" } } }
    collision_settings { disable_collision_checking: true }
    collision_queries {
      waypoints { joints: [ 0, -0.6, 1.7, 0, 0, 0 ] }
      waypoints { joints: [ 1.2, -0.6, 1.7, 0, 0, 0 ] }
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(const auto disabled_response,
                       BatchCheck(disabled_request));
  ASSERT_EQ(disabled_response.collision_results_size(), 1);
  EXPECT_TRUE(disabled_response.collision_results(0).success());
  EXPECT_FALSE(disabled_response.collision_results(0).has_collision());
}


TEST_P(MotionPlannerServiceWithoutLoggerTest, MotionEventCalculated) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              joint_position {
                joints: [ -0.9, 0.4, -0.1194, -0.705302, 0.673298, -1.554707 ]
              }
            }
            motion_segment_events {
              event_location {
                relative_to: SEGMENT_TARGET
                event_offset { time { seconds: 1 nanos: 0 } }
              }
              event {
                [type.googleapis.com/google.protobuf.DoubleValue] { value: 1.0 }
              }
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(auto result, PlanTrajectory(request));

  intrinsic_proto::motion_planning::v1::MotionEvent expected_motion_event;
  *expected_motion_event.mutable_event() = request.motion_specification()
                                               .motion_segments()
                                               .at(0)
                                               .motion_segment_events()
                                               .at(0)
                                               .event();
  intrinsic_proto::motion_planning::v1::Keypoint expected_event_keypoint;
  *expected_event_keypoint.mutable_event_offset() =
      request.motion_specification()
          .motion_segments()
          .at(0)
          .motion_segment_events()
          .at(0)
          .event_location()
          .event_offset();
  // Keypoint is the last point in the trajectory since relative to target.
  expected_event_keypoint.set_keypoint_cartesian_arc_length_from_start_meters(
      result.discretized().cartesian_arc_length_meters().at(
          result.discretized().cartesian_arc_length_meters_size() - 1));
  *expected_event_keypoint.mutable_keypoint_time_from_start() =
      result.discretized().time_since_start().at(
          result.discretized().time_since_start_size() - 1);
  *expected_motion_event.mutable_keypoint() = expected_event_keypoint;
  // We don't care about checking the joint path variable of the keypoint so we
  // just clear it in the result.
  result.mutable_motion_events()
      ->at(0)
      .mutable_keypoint()
      ->clear_keypoint_joint_path_variable_from_start();

  EXPECT_THAT(result.motion_events().at(0), EqualsProto(expected_motion_event));
}


TEST_P(MotionPlannerServiceWithoutLoggerTest,
       ValidTrajectoryPlanForSingleMotionTarget) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              joint_position {
                joints: [ -0.9, 0.4, -0.1194, -0.705302, 0.673298, -1.554707 ]
              }
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(auto result, PlanTrajectory(request));
  ASSERT_GE(result.discretized().state_size(), 2);
  // Check that the start and end are as expected.
  EXPECT_THAT(result.discretized().state().at(0).position(),
              Pointwise(DoubleEq(), {0, -2, 2, 0, 0, 0}));
  EXPECT_THAT(result.discretized()
                  .state()
                  .at(result.discretized().state_size() - 1)
                  .position(),
              Pointwise(DoubleNear(1e-6),
                        {-0.9, 0.4, -0.1194, -0.705302, 0.673298, -1.554707}));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       ValidTrajectoryPlanForMultipleTargets) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              joint_position {
                joints: [
                  1.1916,
                  -1.47557,
                  -1.94578,
                  -1.78062,
                  -1.02402,
                  -0.450498
                ]
              }
            }
          }
          motion_segments {
            target {
              joint_position {
                joints: [
                  1.13851,
                  -1.06023,
                  1.04918,
                  -0.658946,
                  -1.29353,
                  -0.322911
                ]
              }
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(auto result, PlanTrajectory(request));
  ASSERT_TRUE(result.has_discretized());
  EXPECT_EQ(result.swept_volumes_size(), 0);
  ASSERT_GE(result.discretized().state_size(), 2);
  // Check that the start and end are as expected.
  EXPECT_THAT(result.discretized().state().at(0).position(),
              Pointwise(DoubleEq(), {0, -2, 2, 0, 0, 0}));
  EXPECT_THAT(result.discretized()
                  .state()
                  .at(result.discretized().state_size() - 1)
                  .position(),
              Pointwise(DoubleNear(1e-6), {1.13851, -1.06023, 1.04918,
                                           -0.658946, -1.29353, -0.322911}));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, ValidTrajectoryForLinearMove) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              cartesian_pose {
                moving_frame {
                  by_name {
                    frame { object_name: "agilus_04" frame_name: "flange" }
                  }
                }
                target_frame { by_name { object { object_name: "root" } } }
                target_frame_offset {
                  position { x: 0.175127 y: 0.533182 z: 0.822006 }
                  orientation {
                    x: -0.70796712066661194
                    y: 0.0029309704995574928
                    z: 0.706235120371408
                    w: 0.0024331504147085481
                  }
                }
              }
            }
            motion_type: LINEAR
            cartesian_limits {
              max_rotational_velocity: 1
              max_translational_velocity: 0.6
              max_rotational_acceleration: 5
              max_translational_acceleration: 2
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
          start_configuration {
            joints: [
              0.0374165,
              -1.97835,
              2.08278,
              0.344806,
              -0.110898,
              -0.34285
            ]
          }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(auto result, PlanTrajectory(request));
  ASSERT_TRUE(result.has_discretized());
  ASSERT_GE(result.discretized().state_size(), 2);
  // Linear move does some trajectory optimization. Small deviations in the
  // start configuration are expected.
  EXPECT_THAT(result.discretized().state().at(0).position(),
              Pointwise(DoubleNear(1e-6), {0.0374165, -1.97835, 2.08278,
                                           0.344806, -0.110898, -0.34285}));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       GeneratedTrajectorySatisfiesPathConstraints) {
  constexpr double k2ndJointPositionStart = 0.4;
  constexpr double k5thJointPositionStart = 0.673298;
  constexpr double k2ndJointPositionEnd = 0.48;
  constexpr double k5thJointPositionEnd = 0.83;
  const double kJointPositionSumLimit =
      std::max(std::fabs(k2ndJointPositionStart + k5thJointPositionStart),
               std::fabs(k2ndJointPositionEnd + k5thJointPositionEnd));
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest valid_request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              joint_position {
                joints: [ -0.9, 0.0, -0.1194, -0.705302, 0.0, -1.554707 ]
              }
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
          start_configuration {
            joints: [ -0.10, 0.0, -1.36, -0.77, 0.0, -1.0 ]
          }
        }
      )pb");
  auto valid_target_joint_positions =
      valid_request.mutable_motion_specification()
          ->mutable_motion_segments(0)
          ->target()
          .joint_position();
  valid_target_joint_positions.mutable_joints()->at(1) = k2ndJointPositionStart;
  valid_target_joint_positions.mutable_joints()->at(4) = k5thJointPositionStart;
  auto valid_start_configuration = valid_request.mutable_robot_specification()
                                       ->mutable_start_configuration();
  valid_start_configuration->mutable_joints()->at(1) = k2ndJointPositionEnd;
  valid_start_configuration->mutable_joints()->at(4) = k5thJointPositionEnd;
  *valid_request.mutable_motion_specification()
       ->mutable_motion_segments(0)
       ->mutable_path_constraints()
       ->mutable_joint_position_sum_limit() =
      CreateJointPositionSumLimitConstraintFor6DofRobot(kJointPositionSumLimit,
                                                        "agilus_04");
  ASSERT_OK_AND_ASSIGN(
      bool path_constraints_satisfied,
      PlanSingleSegmentConstrainedTrajectoryAndCheckPathConstraintsSatisfaction(
          valid_request));
  EXPECT_TRUE(path_constraints_satisfied);
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       FailedToGenerateTrajectoryThatSatisfiesPathConstraints) {
  constexpr double k2ndJointPositionStart = 0.4;
  constexpr double k5thJointPositionStart = 0.673298;
  constexpr double k2ndJointPositionEnd = 0.48;
  constexpr double k5thJointPositionEnd = 0.83;
  const double kJointPositionSumLimit =
      std::max(std::fabs(k2ndJointPositionStart + k5thJointPositionStart),
               std::fabs(k2ndJointPositionEnd + k5thJointPositionEnd));
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest invalid_request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              joint_position {
                joints: [ -0.9, 0.0, -0.1194, -0.705302, 0.0, -1.554707 ]
              }
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
          start_configuration {
            joints: [ -0.10, 0.0, -1.36, -0.77, 0.0, -1.0 ]
          }
        }
      )pb");
  auto target_joint_positions = invalid_request.mutable_motion_specification()
                                    ->mutable_motion_segments(0)
                                    ->target()
                                    .joint_position();
  target_joint_positions.mutable_joints()->at(1) = k2ndJointPositionStart;
  target_joint_positions.mutable_joints()->at(4) = k5thJointPositionStart;
  auto invalid_start_configuration =
      invalid_request.mutable_robot_specification()
          ->mutable_start_configuration();
  invalid_start_configuration->mutable_joints()->at(1) = k2ndJointPositionEnd;
  invalid_start_configuration->mutable_joints()->at(4) = k5thJointPositionEnd;
  *invalid_request.mutable_motion_specification()
       ->mutable_motion_segments(0)
       ->mutable_path_constraints()
       ->mutable_joint_position_sum_limit() =
      CreateJointPositionSumLimitConstraintFor6DofRobot(
          kJointPositionSumLimit - 0.1, "agilus_04");
  EXPECT_THAT(
      PlanSingleSegmentConstrainedTrajectoryAndCheckPathConstraintsSatisfaction(
          invalid_request),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid initial joint configuration.")));
}

// Test that the motion planner service returns an error when combining linear
// move and joint/planned move segments within one request. Not enabling mixed
// motion type request is switched off by default.
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       WorksForCombinedMotionTypeRequestInDefaultSetting) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              cartesian_pose {
                moving_frame {
                  by_name {
                    frame { object_name: "agilus_04" frame_name: "flange" }
                  }
                }
                target_frame { by_name { object { object_name: "root" } } }
                target_frame_offset {
                  position { x: 0.175127 y: 0.533182 z: 0.822006 }
                  orientation {
                    x: -0.70796712066661194
                    y: 0.0029309704995574928
                    z: 0.706235120371408
                    w: 0.0024331504147085481
                  }
                }
              }
            }
            motion_type: LINEAR
            cartesian_limits {
              max_rotational_velocity: 1
              max_translational_velocity: 0.6
              max_rotational_acceleration: 5
              max_translational_acceleration: 2
            }
          }
          motion_segments {
            target {
              cartesian_pose {
                moving_frame {
                  by_name {
                    frame { object_name: "agilus_04" frame_name: "flange" }
                  }
                }
                target_frame { by_name { object { object_name: "root" } } }
                target_frame_offset {
                  position { x: 0.15 y: 0.533182 z: 0.80 }
                  orientation {
                    x: -0.70796712066661194
                    y: 0.0029309704995574928
                    z: 0.706235120371408
                    w: 0.0024331504147085481
                  }
                }
              }
            }
            cartesian_limits {
              max_rotational_velocity: 1
              max_translational_velocity: 0.6
              max_rotational_acceleration: 5
              max_translational_acceleration: 2
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
          start_configuration {
            joints: [
              0.0374165,
              -1.97835,
              2.08278,
              0.344806,
              -0.110898,
              -0.34285
            ]
          }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(auto result, PlanTrajectory(request));
  ASSERT_TRUE(result.has_discretized());
  ASSERT_GE(result.discretized().state_size(), 2);
  // Linear move does some trajectory optimization. Small deviations in the
  // start configuration are expected.
  EXPECT_THAT(result.discretized().state().at(0).position(),
              Pointwise(DoubleNear(1e-6), {0.0374165, -1.97835, 2.08278,
                                           0.344806, -0.110898, -0.34285}));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       ValidTrajectoryWithStartSpecified) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              joint_position {
                joints: [ -0.9, 0.4, -0.1194, -0.705302, 0.673298, -1.554707 ]
              }
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
          start_configuration {
            joints: [ -0.10, 0.48, -1.36, -0.77, 0.83, -1.0 ]
          }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(auto result, PlanTrajectory(request));
  ASSERT_TRUE(result.has_discretized());
  EXPECT_EQ(result.swept_volumes_size(), 0);
  ASSERT_GE(result.discretized().state_size(), 2);
  // Check that the start and end are as expected.
  EXPECT_THAT(result.discretized().state().at(0).position(),
              Pointwise(DoubleEq(), {-0.10, 0.48, -1.36, -0.77, 0.83, -1.0}));
  EXPECT_THAT(result.discretized()
                  .state()
                  .at(result.discretized().state_size() - 1)
                  .position(),
              Pointwise(DoubleNear(1e-6), {-0.900000, 0.4, -0.1194, -0.705302,
                                           0.673298, -1.554707}));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PlanTrajectoryCacheHitForSimilarStartingRobotConf) {
  // Since we have tested hasher and comparator for the cache, here we will
  // only test cache hit w.r.t a similar starting robot configuration.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest original_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *original_request.mutable_robot_specification()
       ->mutable_start_configuration() = ParseTextProtoOrDie(
      R"pb(
        joints: [ -0.10, 0.48, -1.36, -0.77, 0.83, -1.0 ]
      )pb");
  const auto result_of_original_request = PlanTrajectory(original_request);
  ASSERT_OK(result_of_original_request.status());
  // Check that the start and end are as expected.
  EXPECT_THAT(
      result_of_original_request->discretized().state().at(0).position(),
      Pointwise(DoubleEq(), original_request.robot_specification()
                                .start_configuration()
                                .joints()));
  EXPECT_THAT(
      result_of_original_request->discretized()
          .state()
          .at(result_of_original_request->discretized().state_size() - 1)
          .position(),
      Pointwise(DoubleNear(1e-6), original_request.motion_specification()
                                      .motion_segments()
                                      .at(0)
                                      .target()
                                      .joint_position()
                                      .joints()));

  // Plan again with a slightly different (within tolerance) starting robot
  // configuration.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest new_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *new_request.mutable_robot_specification()->mutable_start_configuration() =
      ParseTextProtoOrDie(
          R"pb(
            joints: [ -0.1009, 0.48, -1.36, -0.77, 0.83, -1.0 ]
          )pb");
  const auto result_of_new_request = PlanTrajectory(new_request);
  ASSERT_OK(result_of_new_request.status());
  // Check that the start and end are as expected.
  // They should be the same as the last result.
  EXPECT_THAT(result_of_new_request->discretized().state().at(0).position(),
              Pointwise(DoubleEq(), original_request.robot_specification()
                                        .start_configuration()
                                        .joints()));
  EXPECT_THAT(result_of_new_request->discretized()
                  .state()
                  .at(result_of_new_request->discretized().state_size() - 1)
                  .position(),
              Pointwise(DoubleNear(1e-6), new_request.motion_specification()
                                              .motion_segments()
                                              .at(0)
                                              .target()
                                              .joint_position()
                                              .joints()));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PlanTrajectoryCacheMissForDifferentStartingRobotConf) {
  // Since we have tested hasher and comparator for the cache, here we will
  // only test cache miss w.r.t a different starting robot configuration.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest original_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *original_request.mutable_robot_specification()
       ->mutable_start_configuration() = ParseTextProtoOrDie(
      R"pb(
        joints: [ -0.10, 0.48, -1.36, -0.77, 0.83, -1.0 ]
      )pb");
  const auto result_of_original_request = PlanTrajectory(original_request);
  ASSERT_OK(result_of_original_request.status());
  // Check that the start and end are as expected.
  EXPECT_THAT(
      result_of_original_request->discretized().state().at(0).position(),
      Pointwise(DoubleEq(), original_request.robot_specification()
                                .start_configuration()
                                .joints()));
  EXPECT_THAT(
      result_of_original_request->discretized()
          .state()
          .at(result_of_original_request->discretized().state_size() - 1)
          .position(),
      Pointwise(DoubleNear(1e-6), original_request.motion_specification()
                                      .motion_segments()
                                      .at(0)
                                      .target()
                                      .joint_position()
                                      .joints()));

  // Plan again with a slightly different (not within tolerance of exact or
  // fuzzy match) starting robot configuration.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest new_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *new_request.mutable_robot_specification()->mutable_start_configuration() =
      ParseTextProtoOrDie(
          R"pb(
            joints: [ -0.1021, 0.48, -1.36, -0.77, 0.83, -1.0 ]
          )pb");
  const auto result_of_new_request = PlanTrajectory(new_request);
  ASSERT_OK(result_of_new_request.status());
  // Check that the start and end are as expected.
  // They should not be the same as the last result.
  EXPECT_THAT(
      result_of_new_request->discretized().state().at(0).position(),
      Pointwise(
          DoubleEq(),
          new_request.robot_specification().start_configuration().joints()));
  EXPECT_THAT(result_of_new_request->discretized()
                  .state()
                  .at(result_of_new_request->discretized().state_size() - 1)
                  .position(),
              Pointwise(DoubleNear(1e-6), new_request.motion_specification()
                                              .motion_segments()
                                              .at(0)
                                              .target()
                                              .joint_position()
                                              .joints()));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PlanTrajectoryCacheHitFuzzyForDifferentStartingRobotConf) {
  // Since we have tested hasher and comparator for the cache, here we will
  // only test cache miss w.r.t a different starting robot configuration.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest original_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *original_request.mutable_robot_specification()
       ->mutable_start_configuration() = ParseTextProtoOrDie(
      R"pb(
        joints: [ -0.10, 0.48, -1.36, -0.77, 0.83, -1.0 ]
      )pb");
  const auto result_of_original_request = PlanTrajectory(original_request);
  ASSERT_OK(result_of_original_request.status());
  // Check that the start and end are as expected.
  EXPECT_THAT(
      result_of_original_request->discretized().state().at(0).position(),
      Pointwise(DoubleEq(), original_request.robot_specification()
                                .start_configuration()
                                .joints()));
  EXPECT_THAT(
      result_of_original_request->discretized()
          .state()
          .at(result_of_original_request->discretized().state_size() - 1)
          .position(),
      Pointwise(DoubleNear(1e-6), original_request.motion_specification()
                                      .motion_segments()
                                      .at(0)
                                      .target()
                                      .joint_position()
                                      .joints()));

  // Plan again with a slightly different (not within tolerance of exact match
  // but within fuzzy match) starting robot configuration.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest new_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *new_request.mutable_robot_specification()->mutable_start_configuration() =
      ParseTextProtoOrDie(
          R"pb(
            joints: [ -0.1011, 0.48, -1.36, -0.77, 0.83, -1.0 ]
          )pb");
  const auto result_of_new_request = PlanTrajectory(new_request);
  ASSERT_OK(result_of_new_request.status());
  // Check that the start and end are as expected.
  // They should be the same as the last result.
  EXPECT_THAT(result_of_new_request->discretized().state().at(0).position(),
              Pointwise(DoubleEq(), original_request.robot_specification()
                                        .start_configuration()
                                        .joints()));
  EXPECT_THAT(result_of_new_request->discretized()
                  .state()
                  .at(result_of_new_request->discretized().state_size() - 1)
                  .position(),
              Pointwise(DoubleNear(1e-6), new_request.motion_specification()
                                              .motion_segments()
                                              .at(0)
                                              .target()
                                              .joint_position()
                                              .joints()));

  // Plan again with a slightly different (not within tolerance of exact match
  // but within fuzzy match) starting robot configuration. But disable fuzzy
  // match.
  new_request.mutable_motion_planner_config()->set_skip_fuzzy_cache_check(true);
  const auto result_of_new_request_with_fuzzy_disabled =
      PlanTrajectory(new_request);
  ASSERT_OK(result_of_new_request_with_fuzzy_disabled.status());
  // Check that the start is different.
  EXPECT_THAT(result_of_new_request_with_fuzzy_disabled->discretized()
                  .state()
                  .at(0)
                  .position(),
              Not(Pointwise(DoubleEq(), original_request.robot_specification()
                                            .start_configuration()
                                            .joints())));
}
TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PlanTrajectoryCacheFuzzyMissForDifferentRelatedNodePoses) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest original_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *original_request.mutable_motion_specification()
       ->mutable_motion_segments()
       ->at(0)
       .mutable_target() = ParseTextProtoOrDie(R"pb(
    position_equality {
      moving_frame { id: "ofid_2" }
      target_frame { id: "root" }
      moving_frame_offset { z: 0.05 }
    }
  )pb");
  const auto result_of_original_request = PlanTrajectory(original_request);
  ASSERT_OK(result_of_original_request.status());

  // Move the target object for a small distance (less than the threshold).
  world::ObjectWorldClient object_world_client("world",
                                               object_world_service_stub_);
  ASSERT_OK_AND_ASSIGN(world::WorldObject root,
                       object_world_client.GetRootObject());
  ASSERT_OK_AND_ASSIGN(
      world::WorldObject my_object,
      object_world_client.GetObject(ObjectWorldResourceId(("ofid_2"))));
  ASSERT_OK_AND_ASSIGN(auto root_t_this,
                       object_world_client.GetTransform(root, my_object));
  ASSERT_OK(object_world_client.UpdateTransform(
      root, my_object, my_object,
      root_t_this * toPose3d("0.0009 0 0 1 0 0 0")));

  intrinsic_proto::motion_planning::v1::MotionPlanningRequest
      new_request_small_diff = UpdateJerkLimits(
          motion_planning_request_, GetParam().set_infinite_jerk_limits);
  *new_request_small_diff.mutable_motion_specification()
       ->mutable_motion_segments()
       ->at(0)
       .mutable_target() = ParseTextProtoOrDie(R"pb(
    position_equality {
      moving_frame { id: "ofid_2" }
      target_frame { id: "root" }
      moving_frame_offset { z: 0.05 }
    }
  )pb");
  const auto result_of_new_request_small_diff =
      PlanTrajectory(new_request_small_diff);
  ASSERT_OK(result_of_new_request_small_diff.status());
  EXPECT_THAT(
      result_of_original_request->discretized()
          .state()
          .at(result_of_original_request->discretized().state_size() - 1)
          .position(),
      Pointwise(
          DoubleNear(1e-6),
          result_of_new_request_small_diff->discretized()
              .state()
              .at(result_of_new_request_small_diff->discretized().state_size() -
                  1)
              .position()));

  // Move the target object for a large distance (more than the threshold).
  ASSERT_OK_AND_ASSIGN(root_t_this,
                       object_world_client.GetTransform(root, my_object));
  ASSERT_OK(object_world_client.UpdateTransform(
      root, my_object, my_object, root_t_this * toPose3d("0.002 0 0 1 0 0 0")));

  intrinsic_proto::motion_planning::v1::MotionPlanningRequest
      new_request_large_diff = UpdateJerkLimits(
          motion_planning_request_, GetParam().set_infinite_jerk_limits);
  *new_request_large_diff.mutable_motion_specification()
       ->mutable_motion_segments()
       ->at(0)
       .mutable_target() = ParseTextProtoOrDie(R"pb(
    position_equality {
      moving_frame { id: "ofid_2" }
      target_frame { id: "root" }
      moving_frame_offset { z: 0.05 }
    }
  )pb");
  const auto result_of_new_request_large_diff =
      PlanTrajectory(new_request_large_diff);
  ASSERT_OK(result_of_new_request_large_diff.status());
  EXPECT_THAT(
      result_of_original_request->discretized()
          .state()
          .at(result_of_original_request->discretized().state_size() - 1)
          .position(),
      Not(Pointwise(
          DoubleNear(1e-6),
          result_of_new_request_large_diff->discretized()
              .state()
              .at(result_of_new_request_large_diff->discretized().state_size() -
                  1)
              .position())));
}

constexpr double kDiscretizedPositionTolerance = 1.0e-6;

// Returns the joint positions of the initial state in `response`.
const google::protobuf::RepeatedField<double>&
GetInitialDiscretizedJointPositions(
    const TrajectoryPlanningResponse& response) {
  return response.discretized().state().at(0).position();
}

// Returns the joint positions of the final state in `response`.
const google::protobuf::RepeatedField<double>&
GetFinalDiscretizedJointPositions(const TrajectoryPlanningResponse& response) {
  const auto& discretized_states = response.discretized().state();
  return discretized_states.at(discretized_states.size() - 1).position();
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PlanTrajectoryCacheMissForDifferentRelativeTargetReferenceFrame) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest original_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *original_request.mutable_robot_specification()
       ->mutable_start_configuration() = ParseTextProtoOrDie(R"pb(
    joints: [ 0.0, -2.0, 2.0, 0.0, 0.0, 0.0 ]
  )pb");
  *original_request.mutable_motion_specification()
       ->mutable_motion_segments()
       ->at(0)
       .mutable_target() = ParseTextProtoOrDie(R"pb(
    relative_position_equality {
      moving_frame {
        by_name { frame { object_name: "agilus_04" frame_name: "flange" } }
      }
      reference_frame { id: "ofid_2" }
      relative_position { x: 0.05 y: 0.05 z: 0.05 }
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(
      const intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse
          original_response,
      PlanTrajectory(original_request));

  world::ObjectWorldClient object_world_client("world",
                                               object_world_service_stub_);
  ASSERT_OK_AND_ASSIGN(const world::WorldObject root,
                       object_world_client.GetRootObject());
  ASSERT_OK_AND_ASSIGN(
      const world::WorldObject reference_object,
      object_world_client.GetObject(ObjectWorldResourceId("ofid_2")));
  ASSERT_OK_AND_ASSIGN(
      const Pose3d root_to_reference_object_pose,
      object_world_client.GetTransform(root, reference_object));

  // Move the reference object by 0.2 mm (within exact-match cache tolerance)
  // and perturb the start configuration by 0.0002 rad (also within exact-match
  // tolerance) so a cache hit returns `original_request`'s start and end
  // states.
  ASSERT_OK(object_world_client.UpdateTransform(
      root, reference_object, reference_object,
      root_to_reference_object_pose * toPose3d("0.0002 0 0 1 0 0 0")));

  intrinsic_proto::motion_planning::v1::MotionPlanningRequest
      follow_up_request = original_request;
  *follow_up_request.mutable_robot_specification()
       ->mutable_start_configuration() = ParseTextProtoOrDie(R"pb(
    joints: [ 0.0002, -2.0, 2.0, 0.0, 0.0, 0.0 ]
  )pb");
  ASSERT_OK_AND_ASSIGN(
      const intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse
          response_small_diff,
      PlanTrajectory(follow_up_request));
  EXPECT_THAT(GetInitialDiscretizedJointPositions(response_small_diff),
              Pointwise(DoubleEq(), original_request.robot_specification()
                                        .start_configuration()
                                        .joints()));
  EXPECT_THAT(
      GetFinalDiscretizedJointPositions(original_response),
      Pointwise(DoubleNear(kDiscretizedPositionTolerance),
                GetFinalDiscretizedJointPositions(response_small_diff)));

  // Rotate the reference object by 30 degrees (exceeding cache tolerance),
  // which must trigger a cache miss and replan from `follow_up_request`'s
  // start configuration to the rotated target.
  ASSERT_OK(object_world_client.UpdateTransform(
      root, reference_object, reference_object,
      root_to_reference_object_pose * toPose3d("0 0 0 0.9659 0 0 0.2588")));

  ASSERT_OK_AND_ASSIGN(
      const intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse
          response_large_diff,
      PlanTrajectory(follow_up_request));
  EXPECT_THAT(GetInitialDiscretizedJointPositions(response_large_diff),
              Pointwise(DoubleEq(), follow_up_request.robot_specification()
                                        .start_configuration()
                                        .joints()));
  EXPECT_THAT(
      GetFinalDiscretizedJointPositions(original_response),
      Not(Pointwise(DoubleNear(kDiscretizedPositionTolerance),
                    GetFinalDiscretizedJointPositions(response_large_diff))));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PlanTrajectoryCacheMissForMovedPointAtPathConstraint) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest original_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *original_request.mutable_robot_specification()
       ->mutable_start_configuration() = ParseTextProtoOrDie(R"pb(
    joints: [ 0.0, -2.0, 2.0, 0.0, 0.0, 0.0 ]
  )pb");
  *original_request.mutable_motion_specification()
       ->mutable_motion_segments()
       ->at(0)
       .mutable_path_constraints() = ParseTextProtoOrDie(R"pb(
    point_at {
      moving_frame { id: "ofid_2" }
      target_frame { id: "root" }
      moving_axis { z: 1.0 }
      target_frame_offset { x: 0.5 y: 0.5 z: 0.2 }
      tolerance: 3.0
    }
  )pb");

  ASSERT_OK_AND_ASSIGN(
      const intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse
          original_response,
      PlanTrajectory(original_request));
  EXPECT_THAT(GetInitialDiscretizedJointPositions(original_response),
              Pointwise(DoubleEq(), original_request.robot_specification()
                                        .start_configuration()
                                        .joints()));

  world::ObjectWorldClient object_world_client("world",
                                               object_world_service_stub_);
  ASSERT_OK_AND_ASSIGN(const world::WorldObject root,
                       object_world_client.GetRootObject());
  ASSERT_OK_AND_ASSIGN(
      const world::WorldObject constrained_object,
      object_world_client.GetObject(ObjectWorldResourceId("ofid_2")));
  ASSERT_OK_AND_ASSIGN(
      const Pose3d root_to_constrained_object_pose,
      object_world_client.GetTransform(root, constrained_object));

  // Move the `point_at` constrained frame by 0.5 mm (within exact-match cache
  // tolerance) and perturb the start configuration by 0.0005 rad (also within
  // exact-match tolerance). A cache hit returns `original_response` starting at
  // `original_request`'s start configuration.
  ASSERT_OK(object_world_client.UpdateTransform(
      root, constrained_object, constrained_object,
      root_to_constrained_object_pose * toPose3d("0.0005 0 0 1 0 0 0")));

  intrinsic_proto::motion_planning::v1::MotionPlanningRequest
      follow_up_request = original_request;
  *follow_up_request.mutable_robot_specification()
       ->mutable_start_configuration() = ParseTextProtoOrDie(R"pb(
    joints: [ 0.0005, -2.0, 2.0, 0.0, 0.0, 0.0 ]
  )pb");
  ASSERT_OK_AND_ASSIGN(
      const intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse
          response_small_diff,
      PlanTrajectory(follow_up_request));
  EXPECT_THAT(GetInitialDiscretizedJointPositions(response_small_diff),
              Pointwise(DoubleEq(), original_request.robot_specification()
                                        .start_configuration()
                                        .joints()));

  // Move the `point_at` constrained frame in the world by 10 cm. The cache
  // must miss and replan from `follow_up_request`'s start configuration instead
  // of returning a false cache hit with `original_request`'s trajectory.
  ASSERT_OK(object_world_client.UpdateTransform(
      root, constrained_object, constrained_object,
      root_to_constrained_object_pose * toPose3d("0.1 0 0 1 0 0 0")));

  ASSERT_OK_AND_ASSIGN(const TrajectoryPlanningResponse response_large_diff,
                       PlanTrajectory(follow_up_request));
  EXPECT_THAT(GetInitialDiscretizedJointPositions(response_large_diff),
              Pointwise(DoubleEq(), follow_up_request.robot_specification()
                                        .start_configuration()
                                        .joints()));
  EXPECT_THAT(GetInitialDiscretizedJointPositions(response_large_diff),
              Not(Pointwise(DoubleEq(), original_request.robot_specification()
                                            .start_configuration()
                                            .joints())));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PlanTrajectoryCacheFuzzyMissForPathWithCollision) {
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest original_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  const auto result_of_original_request = PlanTrajectory(original_request);
  ASSERT_OK(result_of_original_request.status());
  // Check that the end is as expected.
  EXPECT_THAT(
      result_of_original_request->discretized()
          .state()
          .at(result_of_original_request->discretized().state_size() - 1)
          .position(),
      Pointwise(DoubleNear(1e-6), original_request.motion_specification()
                                      .motion_segments()
                                      .at(0)
                                      .target()
                                      .joint_position()
                                      .joints()));

  // Plan again with a small collision margin. We should see a cache hit.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest
      new_request_small_margin = UpdateJerkLimits(
          motion_planning_request_, GetParam().set_infinite_jerk_limits);
  *new_request_small_margin.mutable_motion_specification()
       ->mutable_motion_segments()
       ->at(0)
       .mutable_collision_settings() = ParseTextProtoOrDie(R"pb(
    minimum_margin: 0.0001
  )pb");
  const auto result_of_new_request_small_margin =
      PlanTrajectory(new_request_small_margin);
  ASSERT_OK(result_of_new_request_small_margin.status());
  // Check that the end is as expected.
  EXPECT_THAT(
      result_of_new_request_small_margin->discretized()
          .state()
          .at(result_of_new_request_small_margin->discretized().state_size() -
              1)
          .position(),
      Pointwise(
          DoubleNear(1e-6),
          result_of_original_request->discretized()
              .state()
              .at(result_of_original_request->discretized().state_size() - 1)
              .position()));

  // Plan again with a large collision margin. No cache hit.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest
      new_request_large_margin = UpdateJerkLimits(
          motion_planning_request_, GetParam().set_infinite_jerk_limits);
  *new_request_large_margin.mutable_motion_specification()
       ->mutable_motion_segments()
       ->at(0)
       .mutable_collision_settings() = ParseTextProtoOrDie(R"pb(
    minimum_margin: 100.0
  )pb");
  EXPECT_THAT(
      PlanTrajectory(new_request_large_margin),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("IK could not find a collision free configuration")));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest,
       PlanTrajectoryDoesNotCacheFailure) {
  // TODO(b/293926897)
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest request =
      motion_planning_request_;
  // The start configuration has a joint that is out of limit. Should see a
  // failure.
  *request.mutable_robot_specification()->mutable_start_configuration() =
      ParseTextProtoOrDie(
          R"pb(
            joints: [ -0.10, 0.48, -1.36, -0.77, -2.09441, -1.0 ]
          )pb");
  auto result = PlanTrajectory(request);
  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("is not within joint limits")));
}

TEST_P(MotionPlannerServiceWithoutLoggerTest, ClearTrajectoryCacheWorks) {
  // Make a new request
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest original_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *original_request.mutable_robot_specification()
       ->mutable_start_configuration() = ParseTextProtoOrDie(
      R"pb(
        joints: [ -0.10, 0.48, -1.36, -0.77, 0.83, -1.0 ]
      )pb");
  const auto result_of_original_request = PlanTrajectory(original_request);
  ASSERT_OK(result_of_original_request.status());
  // Check that the start and end are as expected.
  EXPECT_THAT(
      result_of_original_request->discretized().state().at(0).position(),
      Pointwise(DoubleEq(), original_request.robot_specification()
                                .start_configuration()
                                .joints()));
  EXPECT_THAT(
      result_of_original_request->discretized()
          .state()
          .at(result_of_original_request->discretized().state_size() - 1)
          .position(),
      Pointwise(DoubleNear(1e-6), original_request.motion_specification()
                                      .motion_segments()
                                      .at(0)
                                      .target()
                                      .joint_position()
                                      .joints()));

  // Plan again with a slightly different (within tolerance) starting robot
  // configuration.
  intrinsic_proto::motion_planning::v1::MotionPlanningRequest new_request =
      UpdateJerkLimits(motion_planning_request_,
                       GetParam().set_infinite_jerk_limits);
  *new_request.mutable_robot_specification()->mutable_start_configuration() =
      ParseTextProtoOrDie(
          R"pb(
            joints: [ -0.0991, 0.48, -1.36, -0.77, 0.83, -1.0 ]
          )pb");
  const auto result_of_new_request = PlanTrajectory(new_request);
  ASSERT_OK(result_of_new_request.status());
  // Check that the start and end are as expected.
  // They should be the same as the last result.
  EXPECT_THAT(result_of_new_request->discretized().state().at(0).position(),
              Pointwise(DoubleEq(), original_request.robot_specification()
                                        .start_configuration()
                                        .joints()));
  EXPECT_THAT(
      result_of_new_request->discretized()
          .state()
          .at(result_of_new_request->discretized().state_size() - 1)
          .position(),
      Pointwise(DoubleNear(1e-6), original_request.motion_specification()
                                      .motion_segments()
                                      .at(0)
                                      .target()
                                      .joint_position()
                                      .joints()));

  // Clear the cache
  const Empty clear_cache_request;
  auto clear_cache_result = ClearCache(clear_cache_request);
  ASSERT_OK(clear_cache_result.status());

  // Plan again with a slightly different (within tolerance) starting robot
  // configuration. It should not generate a cache hit.
  auto result3 = PlanTrajectory(new_request);
  ASSERT_OK(result3.status());
  // Check that the start and end are as expected.
  // They should not be the same as the first result.
  EXPECT_THAT(
      result3->discretized().state().at(0).position(),
      Pointwise(
          DoubleEq(),
          new_request.robot_specification().start_configuration().joints()));
  EXPECT_THAT(result3->discretized()
                  .state()
                  .at(result3->discretized().state_size() - 1)
                  .position(),
              Pointwise(DoubleNear(1e-6), new_request.motion_specification()
                                              .motion_segments()
                                              .at(0)
                                              .target()
                                              .joint_position()
                                              .joints()));
}
INSTANTIATE_TEST_SUITE_P(
    MotionPlannerServiceWithoutLoggerTests,
    MotionPlannerServiceWithoutLoggerTest,
    ::testing::ValuesIn(GetMotionPlannerServiceTestParams()),
    [](const ::testing::TestParamInfo<
        MotionPlannerServiceWithoutLoggerTest::ParamType>& info) {
      return info.param.test_name;
    });

}  // namespace

}  // namespace intrinsic
