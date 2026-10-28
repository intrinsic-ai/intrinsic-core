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

#include "intrinsic/motion_planning/service/motion_planner_service_proxy.h"

#include <memory>
#include <string>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.pb.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace {

using ::google::protobuf::Empty;
using ::grpc::ClientContext;
using ::grpc::ServerContext;
using ::intrinsic_proto::motion_planning::v1::BatchCheckRequest;
using ::intrinsic_proto::motion_planning::v1::BatchCheckResponse;
using ::intrinsic_proto::motion_planning::v1::CheckCollisionsRequest;
using ::intrinsic_proto::motion_planning::v1::CheckCollisionsResponse;
using ::intrinsic_proto::motion_planning::v1::FkRequest;
using ::intrinsic_proto::motion_planning::v1::FkResponse;
using ::intrinsic_proto::motion_planning::v1::IkRequest;
using ::intrinsic_proto::motion_planning::v1::IkResponse;
using ::intrinsic_proto::motion_planning::v1::MotionPlanningRequest;
using ::intrinsic_proto::motion_planning::v1::PathPlanningResponse;
using ::intrinsic_proto::motion_planning::v1::TrajectoryPlanningResponse;
// Helper function to copy metadata from server context to client context.
void PropagateMetadata(const ServerContext& server_context,
                       ClientContext& client_context) {
  for (const auto& [key, value] : server_context.client_metadata()) {
    client_context.AddMetadata(std::string(key.data(), key.size()),
                               std::string(value.data(), value.size()));
  }
}

}  // namespace

/*static*/
absl::StatusOr<std::unique_ptr<MotionPlannerServiceProxy>>
MotionPlannerServiceProxy::Create(
    std::unique_ptr<intrinsic_proto::motion_planning::v1::MotionPlannerService::
                        StubInterface>
        mps_stub,
    std::string_view installed_mps_asset_version) {
  if (mps_stub == nullptr) {
    return absl::InvalidArgumentError("mps_stub cannot be nullptr.");
  }
  if (installed_mps_asset_version.empty()) {
    return absl::InternalError(
        "Got empty string as the installed_mps_asset_version.");
  }
  return absl::WrapUnique(new MotionPlannerServiceProxy(
      std::move(mps_stub), installed_mps_asset_version));
}

#define FORWARD_RPC(MethodName, RequestType, ResponseType)             \
  ::grpc::Status MotionPlannerServiceProxy::MethodName(                \
      ServerContext* server_context, const RequestType* request,       \
      ResponseType* response) {                                        \
    ClientContext client_context;                                      \
    PropagateMetadata(*server_context, client_context);                \
    client_context.AddMetadata(                                        \
        std::string(kMotionPlannerServiceAssetVersionKey),             \
        installed_mps_asset_version_);                                 \
    return mps_stub_->MethodName(&client_context, *request, response); \
  }

FORWARD_RPC(PlanTrajectory, MotionPlanningRequest, TrajectoryPlanningResponse);
FORWARD_RPC(PlanPath, MotionPlanningRequest, PathPlanningResponse);
FORWARD_RPC(ComputeIk, IkRequest, IkResponse);
FORWARD_RPC(ComputeFk, FkRequest, FkResponse);
FORWARD_RPC(CheckCollisions, CheckCollisionsRequest, CheckCollisionsResponse);
FORWARD_RPC(BatchCheck, BatchCheckRequest, BatchCheckResponse);
FORWARD_RPC(ClearCache, Empty, Empty);
#undef FORWARD_RPC

}  // namespace intrinsic
