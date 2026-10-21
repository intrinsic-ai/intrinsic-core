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

#include "intrinsic/skills/internal/skill_init.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/descriptor.pb.h"
#include "grpc/grpc.h"
#include "grpcpp/channel.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"  
#include "intrinsic/geometry/storage/geometry_service_storage.h"  
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/skills/internal/skill_repository.h"
#include "intrinsic/skills/internal/skill_service_impl.h"
#include "intrinsic/skills/proto/skill_service_config.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"

namespace intrinsic::skills {
namespace {

using ObjectWorldService = ::intrinsic_proto::world::ObjectWorldService;
using MotionPlannerService =
    ::intrinsic_proto::motion_planning::v1::MotionPlannerService;


absl::StatusOr<
    std::unique_ptr<intrinsic_proto::geometry::GeometryService::Stub>>
CreateGeometryServiceStub(absl::string_view geometry_service_address,
                          absl::Duration connection_timeout) {
  INTR_ASSIGN_OR_RETURN(
      const std::shared_ptr<grpc::Channel> channel,
      connect::CreateClientChannel(
          geometry_service_address, absl::Now() + connection_timeout,
          connect::UnlimitedMessageSizeGrpcChannelArgs()));
  return intrinsic_proto::geometry::GeometryService::NewStub(channel);
}


absl::StatusOr<std::shared_ptr<
    intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>>
CreateMotionPlannerServiceStub(absl::string_view motion_planner_service_address,
                               absl::Duration connection_timeout) {
  INTR_ASSIGN_OR_RETURN(
      const std::shared_ptr<grpc::Channel> channel,
      connect::CreateClientChannel(motion_planner_service_address,
                                   absl::Now() + connection_timeout));
  return intrinsic_proto::motion_planning::v1::MotionPlannerService::NewStub(
      channel);
}

}  // namespace

absl::Status SkillInit(
    const intrinsic_proto::skills::SkillServiceConfig& service_config,
    absl::string_view data_logger_grpc_service_address,
    absl::string_view world_service_address,
    absl::string_view geometry_service_address,
    absl::string_view motion_planner_service_address,
    int32_t skill_service_port, absl::Duration connection_timeout,
    SkillRepository& skill_repository) {
  // Start DataLogger if the endpoint is configured by flags. Do not fail if
  // the logger is unavailable.
  if (!data_logger_grpc_service_address.empty()) {
    if (auto s = intrinsic::data_logger::StartUpIntrinsicLoggerViaGrpc(
            data_logger_grpc_service_address, connection_timeout);
        !s.ok()) {
      LOG(ERROR) << "Failed to connect to data logger: " << s;
    }
  }

  // Set up world service.
  INTR_ASSIGN_OR_RETURN(
      const std::shared_ptr<grpc::Channel> world_service_channel,
      connect::CreateClientChannel(world_service_address,
                                   absl::Now() + connection_timeout));


  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic_proto::geometry::GeometryService::StubInterface>
          geometry_service,
      CreateGeometryServiceStub(geometry_service_address, connection_timeout));
  auto geometry_library =
      GetGeometryServiceGeometryLibrary(geometry_service.get());


  std::shared_ptr<ObjectWorldService::StubInterface> object_world_service =
      ObjectWorldService::NewStub(world_service_channel);

  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<MotionPlannerService::StubInterface>
          motion_planner_service,
      CreateMotionPlannerServiceStub(motion_planner_service_address,
                                     connection_timeout));

  SkillProjectorServiceImpl project_service(
      skill_repository,
      *geometry_library,  // NOLINT  
      object_world_service, motion_planner_service);
  SkillExecutorServiceImpl execute_service(
      skill_repository,
      *geometry_library,  // NOLINT  
      world_service_channel, object_world_service, motion_planner_service);

  std::string server_address = absl::StrCat("0.0.0.0:", skill_service_port);

  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();  // NOLINT (insecure)
  builder.AddListeningPort(server_address, creds);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(&project_service);
  builder.RegisterService(&execute_service);

  if (!service_config.has_skill_description()) {
    LOG(FATAL) << "skill_description was not present in the SkillServiceConfig";
  }
  intrinsic_proto::skills::Skill skill_description =
      service_config.skill_description();

  auto skill_information_service =
      std::make_unique<SkillInformationServiceImpl>(skill_description);
  builder.RegisterService(skill_information_service.get());
  std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
  if (server == nullptr) {
    LOG(FATAL) << "Cannot create skill service " << server_address;
  }

  std::vector<std::string> skill_names = skill_repository.GetSkillAliases();
  absl::c_sort(skill_names);

  LOG(INFO) << "Available skills: ";
  for (const auto& name : skill_names) {
    LOG(INFO) << "\t" << name;
  }

  LOG(INFO) << "--------------------------------";
  LOG(INFO) << "-- Skill service listening on " << server_address;
  LOG(INFO) << "--------------------------------";

  server->Wait();
  return absl::OkStatus();
}

}  // namespace intrinsic::skills
