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

#include "intrinsic/motion_planning/service/motion_planner_service_in_process.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "grpc/grpc.h"
#include "grpc/grpc_security_constants.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server_builder.h"
#include "grpcpp/support/channel_arguments.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/service/motion_planner_service.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/service/motion_planner_service_test_fixture.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/test/world_service_fake.h"

namespace intrinsic {

MotionPlannerServiceInProcess::MotionPlannerServiceInProcess() = default;
MotionPlannerServiceInProcess::~MotionPlannerServiceInProcess() = default;

std::unique_ptr<MotionPlannerServiceInProcess>
MotionPlannerServiceInProcess::Create(FakeWorldService* fake_world_service) {
  std::unique_ptr<intrinsic_proto::world::internal::WorldService::StubInterface>
      world_service_stub;
  std::unique_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
      object_world_service_stub;
  if (auto address = fake_world_service->GetAddress(); address.ok()) {
    auto channel = ::grpc::CreateChannel(
        *address, grpc::experimental::LocalCredentials(LOCAL_TCP));
    world_service_stub =
        intrinsic_proto::world::internal::WorldService::NewStub(channel);
    object_world_service_stub =
        intrinsic_proto::world::ObjectWorldService::NewStub(channel);
  } else {
    world_service_stub = fake_world_service->NewStub();
    object_world_service_stub = fake_world_service->NewObjectStub();
  }
  auto mps = MotionPlannerServiceInProcess::Create(
      world_service_stub.get(), object_world_service_stub.get(),
      fake_world_service->GetGeometryLibrary());
  mps->world_service_stub_ = std::move(world_service_stub);
  mps->object_world_service_stub_ = std::move(object_world_service_stub);
  return mps;
}

std::unique_ptr<MotionPlannerServiceInProcess>
MotionPlannerServiceInProcess::Create(
    // Unused since `MotionPlannerService` no longer talks to the legacy world
    // service. Retained so the many existing callers keep compiling.
    intrinsic_proto::world::internal::WorldService::StubInterface*,
    intrinsic_proto::world::ObjectWorldService::StubInterface*
        object_world_service,
    std::shared_ptr<GeometryLibrary> geometry_library,
    const MotionPlannerFlags& flags) {
  auto service_in_process = std::make_unique<MotionPlannerServiceInProcess>();
  service_in_process->geometry_library_ = geometry_library;

  absl::StatusOr<std::unique_ptr<MotionPlannerService>>
      motion_planner_service_status = MotionPlannerService::Create(
          object_world_service, geometry_library.get(),
          /*mps_asset_id_name=*/kMotionPlannerServiceName,
          /*plan_trajectory_cache=*/nullptr,
          /*plan_trajectory_nonvolatile_cache=*/nullptr,
          /*mps_config=*/
          MotionPlannerServiceConfig{
              .workcell_name = std::string(kTestWorkcellName),
              .organization_id = std::string(kTestOrgId),
              .flags = flags});
  CHECK_OK(motion_planner_service_status);
  service_in_process->service_ =
      std::move(motion_planner_service_status.value());

  grpc::ServerBuilder builder;

  builder.RegisterService(service_in_process->service_.get());
  int port = 0;
  service_in_process->address_ = absl::StrCat("dns:///localhost:", 0);
  builder.AddListeningPort(
      service_in_process->address_,
      grpc::experimental::LocalServerCredentials(LOCAL_TCP), &port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  service_in_process->server_ = builder.BuildAndStart();
  // Save the picked port.
  service_in_process->address_ = absl::StrCat("dns:///localhost:", port);
  return service_in_process;
}

std::shared_ptr<
    intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>
MotionPlannerServiceInProcess::GetStub() const {
  return std::make_shared<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>(
      server_->InProcessChannel(grpc::ChannelArguments()));
}

std::unique_ptr<
    intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>
MotionPlannerServiceInProcess::GetUniqueStub() const {
  return std::make_unique<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>(
      server_->InProcessChannel(grpc::ChannelArguments()));
}

}  // namespace intrinsic
