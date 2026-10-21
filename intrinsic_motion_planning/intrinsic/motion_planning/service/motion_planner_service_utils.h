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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_UTILS_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_UTILS_H_

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/logging/structured_logging_client.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// Selectively updates the fields of `flags` from `proto_flags`. Only fields
// that are explicitly set (present) in `proto_flags` are overwritten, leaving
// all unset fields in `flags` untouched.
void FromProto(
    const intrinsic_proto::motion_planning::v1::MotionPlannerFlags& proto_flags,
    MotionPlannerFlags& flags);

// Converts C++ MotionPlannerFlags to proto MotionPlannerFlags.
intrinsic_proto::motion_planning::v1::MotionPlannerFlags ToProto(
    const MotionPlannerFlags& flags);

absl::StatusOr<JointTrajectoryPVA> PlanTrajectoryImpl(
    const World& initial_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request,
    const MotionPlannerFlags& flags = {});

absl::StatusOr<MotionPlanner::PlanTrajectoryResult>
PlanTrajectoryImplMotionPlannerResponse(
    const World& initial_world,
    const intrinsic_proto::motion_planning::v1::MotionPlanningRequest& request,
    const MotionPlannerFlags& flags = {});

// Creates a `MotionPlanningRequest` from a `point_path` (i.e. a vector of
// target joint configurations) and `motion_types`. The size of `point_path` and
// `motion_types` must be the same, otherwise a `kInvalidArgument` error is
// returned. Moreover, the size of `point_path` must be greater than 0,
// otherwise a `kInvalidArgument` error is returned. The `motion_types` are used
// to determine the motion type of each `MotionSegment`. The `robot_name` is
// used to set the robot specification in the request.
absl::StatusOr<intrinsic_proto::motion_planning::v1::MotionPlanningRequest>
CreateMotionPlanningRequestFromPointPath(
    absl::Span<const eigenmath::VectorXd> point_path,
    absl::Span<
        const intrinsic_proto::motion_planning::v1::MotionSegment::MotionType>
        motion_types,
    absl::string_view robot_name);

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::world::internal::WorldService::Stub>>
CreateWorldServiceStub(const std::string& world_service_address,
                       const absl::Duration& grpc_connect_timeout);

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::world::ObjectWorldService::Stub>>
CreateObjectWorldServiceStub(const std::string& world_service_address,
                             const absl::Duration& grpc_connect_timeout);

absl::StatusOr<
    std::unique_ptr<intrinsic_proto::geometry::GeometryService::Stub>>
CreateGeometryServiceStub(const std::string& geometry_service_address,
                          const absl::Duration& grpc_connect_timeout);

struct StructuredLoggingClientAndStub {
  std::unique_ptr<intrinsic_proto::data_logger::DataLogger::StubInterface> stub;
  std::unique_ptr<StructuredLoggingClient> client;
};

StructuredLoggingClientAndStub CreateStructuredLoggingClient(
    const std::string& data_logger_service_address,
    const absl::Duration& grpc_connect_timeout);

StructuredLoggingClientAndStub CreateStructuredLoggingClient(
    std::shared_ptr<grpc::Channel> channel);

absl::Status AttemptToPreCacheTheWorldData(
    intrinsic_proto::world::ObjectWorldService::StubInterface&
        object_world_service,
    GeometryLibrary& geometry_library);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_UTILS_H_
