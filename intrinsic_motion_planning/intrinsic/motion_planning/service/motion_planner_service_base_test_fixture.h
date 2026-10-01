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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_BASE_TEST_FIXTURE_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_BASE_TEST_FIXTURE_H_

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/service/in_memory_geometry_service.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/logging/structured_logging_client.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/proto/v1/compute_ik.pb.h"
#include "intrinsic/motion_planning/proto/v1/geometric_constraints.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_config.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/service/motion_planner_service.h"
#include "intrinsic/motion_planning/service/motion_planner_service_proxy.h"
#include "intrinsic/motion_planning/service/motion_planner_service_test_fixture.h"
#include "intrinsic/storage/content_addressable_storage/testing/in_memory_cas_service.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Default cache size for MotionPlannerService.
inline constexpr int kDefaultCacheSize = 199;

inline constexpr std::string_view kTestMotionPlannerServiceAssetVersion =
    "0.20260427.1-RC17";
inline constexpr std::string_view kOtherMotionPlannerServiceAssetVersion =
    "1.20270427.1-RC31";

inline const std::initializer_list<std::string> kSaveLoadMotionFieldIgnoreList =
    {"swept_volumes.geometry",
     "lock_motion_id"};
inline const std::initializer_list<std::string>
    kSaveLoadMotionRepeatedFieldOrderingIgnoreList = {
        "lock_motion_id"};

// If the flag `set_infinite_jerk_limits` is true, this function sets the jerk
// limits to infinite for all motion segments in the motion planning request.
intrinsic_proto::motion_planning::v1::MotionPlanningRequest UpdateJerkLimits(
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
        motion_planning_request,
    bool set_infinite_jerk_limits);

absl::Status SetCartesianLimitsInWorld(World& world,
                                       std::string_view robot_name,
                                       const CartesianLimits& cart_limits);

// Loads the standard `4_robots_cube_world` test world and configures default
// Cartesian limits for "agilus-04" and "iris622".
absl::StatusOr<WorldHashMap<std::string, World>> CreateDefault4RobotsCubeWorld(
    bool set_infinite_jerk_limits = true);

class MotionPlannerServiceBaseTest : public MotionPlannerServiceBaseTestHelper {
 public:
  // Initializes a motion planner service and (test versions of) its
  // dependencies:
  //
  // * A FakeWorldService with the Worlds in `worlds_by_name`
  // * A PlanTrajectoryCache
  // * Optionally (if `use_nonvolatile_cache` is true), a
  //   MotionPlannerNonvolatileCache that is backed by an InMemoryCasService
  // * Finally, a MotionPlannerService that uses all of the above, with the
  //   given `flags`
  //
  // Access these services either via the helper methods below, or directly via
  // the various protected members.
  void Init(bool use_nonvolatile_cache, MotionPlannerFlags extra_flags,
            WorldHashMap<std::string, World> worlds_by_name,
            const StructuredLoggingClient* logger_client = nullptr);

  // The number N for the first N characters that must match in the error
  // message.
  static constexpr size_t kNumNFirstCharactersThatMustMatchInErrorMessage = 40;

  absl::StatusOr<
      intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse>
  PlanTrajectory(
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
          request,
      const std::initializer_list<std::string>& field_ignore_list =
          {
              "swept_volumes.geometry"
          },
      const std::initializer_list<std::string>&
          repeated_field_ordering_ignore_list = {
          });

  absl::StatusOr<intrinsic_proto::motion_planning::v1::PathPlanningResponse>
  PlanPath(const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
               request,
           const std::initializer_list<std::string>& field_ignore_list =
               {
                   "swept_volumes.geometry"
               },
           const std::initializer_list<std::string>&
               repeated_field_ordering_ignore_list = {
               });
  absl::StatusOr<intrinsic_proto::motion_planning::v1::IkResponse> ComputeIk(
      const intrinsic_proto::motion_planning::v1::IkRequest& request,
      bool compare_against_mps_proxy = true);

  absl::StatusOr<intrinsic_proto::motion_planning::v1::FkResponse> ComputeFk(
      const intrinsic_proto::motion_planning::v1::FkRequest& request,
      bool compare_against_mps_proxy = true);

  absl::StatusOr<intrinsic_proto::motion_planning::v1::CheckCollisionsResponse>
  CheckCollisions(
      const intrinsic_proto::motion_planning::v1::CheckCollisionsRequest&
          request,
      bool compare_against_mps_proxy = true);
  absl::StatusOr<google::protobuf::Empty> ClearCache(
      const google::protobuf::Empty& request,
      bool compare_against_mps_proxy = true);

  absl::StatusOr<
      intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse>
  PlanTrajectoryWithMPSProxy(
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
          request);
  absl::StatusOr<
      intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse>
  PlanTrajectoryWithMPSWithAssetVersion(
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
          request);
  absl::StatusOr<World> GetWorld(absl::string_view world_id);

  intrinsic_proto::motion_planning::v1::JointPositionSumLimit
  CreateJointPositionSumLimitConstraintFor6DofRobot(
      double joint_sum_limit, absl::string_view robot_name);

  absl::StatusOr<bool>
  PlanSingleSegmentConstrainedTrajectoryAndCheckPathConstraintsSatisfaction(
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest&
          request);

 protected:
  std::unique_ptr<InMemoryGeometryService> geometry_service_;
  std::unique_ptr<InMemoryCasService> cas_service_;
  std::unique_ptr<grpc::Server> geometry_and_cas_server_;
  std::unique_ptr<intrinsic_proto::geometry::GeometryService::Stub>
      geometry_service_stub_;
  std::unique_ptr<intrinsic_proto::content_addressable_storage::v1::
                      ContentAddressableStorageService::Stub>
      cas_service_stub_;
  std::unique_ptr<FakeWorldService> fake_world_service_;
  std::shared_ptr<GeometryLibrary> fake_world_service_geo_library_;
  std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
      object_world_service_stub_;
  std::unique_ptr<MotionPlannerService> motion_planner_service_;
  std::unique_ptr<MotionPlannerServiceProxy> motion_planner_service_proxy_;
  std::unique_ptr<MotionPlannerService>
      motion_planner_service_with_asset_version_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<grpc::Server> mps_proxy_server_;
  std::unique_ptr<grpc::Server> mps_with_asset_version_server_;
  std::unique_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>
      motion_planner_service_stub_;
  std::unique_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>
      motion_planner_service_proxy_stub_;
  std::unique_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>
      motion_planner_service_with_asset_version_stub_;

  const intrinsic_proto::motion_planning::v1::MotionPlanningRequest
      motion_planning_request_ = ParseTextProtoOrDie(R"pb(
        world_id: "world"
        motion_specification {
          motion_segments {
            target {
              joint_position {
                joints: [ -0.9, 0.4, -0.1194, -0.705302, 0.673298, -1.554707 ]
              }
            }
            joint_limits {
              max_jerk { values: [ 500.0, 500.0, 500.0, 500.0, 500.0, 500.0 ] }
            }
          }
        }
        robot_specification {
          robot_reference { object_id { by_name { object_name: "agilus_04" } } }
        }
      )pb");
};

using MotionPlannerBaseTest = MotionPlannerServiceBaseTest;

// The flag `set_infinite_jerk_limits` allows to set a finite/infinite value for
// joint jerk limits so that it triggers the use of an acceleration-limited or
// a jerk-limited TOPP solver. If `set_infinite_jerk_limits` is true, the
// acceleration-limited TOPP solver should be used and planning should work
// without a jerk-limited parameterization client. Otherwise, a parameterization
// service is started.
class MotionPlannerServiceWithoutLoggerTest
    : public MotionPlannerServiceBaseTest,
      public ::testing::TestWithParam<MotionPlannerServiceTestParams> {
 public:
  void SetUp() override { SetUpWithTestParams(GetParam()); }

 protected:
  // Sets up the test fixture using the provided `test_params`:
  // - Loads the default `4_robots_cube_world` world with configured Cartesian
  //   limits and merges any additional worlds in `extra_worlds`.
  // - Configures motion planner flags according to `test_params`.
  // - Sets up the parameterization service if jerk limits are finite.
  // - Initializes the in-memory test services and motion planner service.
  void SetUpWithTestParams(const MotionPlannerServiceTestParams& test_params,
                           WorldHashMap<std::string, World> extra_worlds = {});
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_BASE_TEST_FIXTURE_H_
