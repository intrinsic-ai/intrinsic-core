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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_PROXY_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_PROXY_H_

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"

namespace intrinsic {

// A gRPC server that forwards motion planning requests to
// `MotionPlannerService`.
class MotionPlannerServiceProxy final
    : public intrinsic_proto::motion_planning::v1::MotionPlannerService::
          Service {
 public:
  static absl::StatusOr<std::unique_ptr<MotionPlannerServiceProxy>> Create(
      std::unique_ptr<intrinsic_proto::motion_planning::v1::
                          MotionPlannerService::StubInterface>
          mps_stub,
      std::string_view installed_mps_asset_version);

  ::grpc::Status PlanTrajectory(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest*
          request,
      intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse*
          response) override;

  ::grpc::Status PlanPath(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::MotionPlanningRequest*
          request,
      intrinsic_proto::motion_planning::v1::PathPlanningResponse* response)
      override;

  ::grpc::Status ComputeIk(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::IkRequest* request,
      intrinsic_proto::motion_planning::v1::IkResponse* response) override;

  ::grpc::Status ComputeFk(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::FkRequest* request,
      intrinsic_proto::motion_planning::v1::FkResponse* response) override;

  ::grpc::Status CheckCollisions(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::CheckCollisionsRequest*
          request,
      intrinsic_proto::motion_planning::v1::CheckCollisionsResponse* response)
      override;

  ::grpc::Status BatchCheck(
      ::grpc::ServerContext* context,
      const intrinsic_proto::motion_planning::v1::BatchCheckRequest* request,
      intrinsic_proto::motion_planning::v1::BatchCheckResponse* response)
      override;
  ::grpc::Status ClearCache(::grpc::ServerContext* context,
                            const google::protobuf::Empty* request,
                            google::protobuf::Empty* response) override;
 private:
  explicit MotionPlannerServiceProxy(
      std::unique_ptr<intrinsic_proto::motion_planning::v1::
                          MotionPlannerService::StubInterface>
          mps_stub,
      std::string_view installed_mps_asset_version)
      : mps_stub_(std::move(mps_stub)),
        installed_mps_asset_version_(installed_mps_asset_version) {}

  std::unique_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::StubInterface>
      mps_stub_;

  const std::string installed_mps_asset_version_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_PROXY_H_
