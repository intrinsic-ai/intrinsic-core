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
#include "intrinsic/motion_planning/service/motion_planner_cache.h"
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
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
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
  proto_config.set_joint_tolerance_in_rad(0.005);
  MotionPlannerServiceConfig config;
  FromProto(proto_config, config);

  EXPECT_TRUE(config.flags.enable_path_refinement_validation_step);
  EXPECT_TRUE(config.enable_motion_planner_service_caching_fuzzy_check);
  EXPECT_DOUBLE_EQ(
      config.max_diff_in_rad_for_starting_robot_configuration_threshold, 0.005);
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

  // Move the reference object by 0.5 mm (within exact-match cache tolerance)
  // and perturb the start configuration by 0.0005 rad (also within exact-match
  // tolerance) so a cache hit returns `original_request`'s start and end
  // states.
  ASSERT_OK(object_world_client.UpdateTransform(
      root, reference_object, reference_object,
      root_to_reference_object_pose * toPose3d("0.0005 0 0 1 0 0 0")));

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
