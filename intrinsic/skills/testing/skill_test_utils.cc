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

#include "intrinsic/skills/testing/skill_test_utils.h"

#include <atomic>
#include <memory>
#include <utility>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/message.h"
#include "grpc/grpc.h"
#include "grpcpp/impl/service_type.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/assets/proto/v1/grpc_connection.pb.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/geometry/storage/in_memory_storage.h"  
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service_mock.grpc.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/execute_request.h"
#include "intrinsic/skills/cc/skill_canceller.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_logging_context.h"
#include "intrinsic/skills/internal/execute_context_impl.h"
#include "intrinsic/skills/internal/get_footprint_context_impl.h"
#include "intrinsic/skills/internal/preview_context_impl.h"
#include "intrinsic/skills/proto/skill_manifest.pb.h"
#include "intrinsic/util/file_helpers.h"  
#include "intrinsic/util/path_resolver/path_resolver.h"  
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service_mock.grpc.pb.h"

namespace intrinsic {
namespace skills {

namespace {

std::string GenerateDefaultTestContextId() {
  static std::atomic<uint64_t> counter{0};
  return absl::StrCat("test-context-", ++counter);
}

}  // namespace

using intrinsic_proto::motion_planning::v1::MockMotionPlannerServiceStub;
using intrinsic_proto::motion_planning::v1::MotionPlannerService;
using intrinsic_proto::world::MockObjectWorldServiceStub;
using intrinsic_proto::world::ObjectWorldService;

absl::Status ExecuteSkill(SkillExecuteInterface& skill,
                          const ExecuteRequest& request,
                          ExecuteContext& context) {
  return skill.Execute(request, context).status();
}

absl::Status PreviewSkill(SkillExecuteInterface& skill,
                          const PreviewRequest& request,
                          PreviewContext& context) {
  return skill.Preview(request, context).status();
}

SkillTestFactory::SkillTestFactory()

    : geometry_library_(GetMapGeometryLibrary())

{}


SkillTestFactory::SkillTestFactory(
    std::shared_ptr<::intrinsic::GeometryLibrary> geometry_library)
    : geometry_library_(std::move(geometry_library)) {
  CHECK(geometry_library_ != nullptr)
      << "SkillTestFactory constructed with a null GeometryLibrary.";
}


ExecuteRequest SkillTestFactory::MakeExecuteRequest(
    const ::google::protobuf::Message& params,
    ::google::protobuf::Message* param_defaults) {
  // clang-format off
  return ExecuteRequest(
      params, param_defaults);
  // clang-format on
}

PreviewRequest SkillTestFactory::MakePreviewRequest(
    const ::google::protobuf::Message& params,
    ::google::protobuf::Message* param_defaults) {
  // clang-format off
  return PreviewRequest(
      params, param_defaults);
  // clang-format on
}

GetFootprintRequest SkillTestFactory::MakeGetFootprintRequest(
    const ::google::protobuf::Message& params,
    ::google::protobuf::Message* param_defaults) {
  // clang-format off
  return GetFootprintRequest(
      params, param_defaults);
  // clang-format on
}

std::shared_ptr<SkillCanceller> MakeSkillCanceller(SkillCanceller* canceller) {
  std::shared_ptr<SkillCanceller> shared_canceller;
  if (canceller) {
    // Caller owns pointer, so don't delete it!
    shared_canceller =
        std::shared_ptr<SkillCanceller>(canceller, [](auto _) {});
  } else {
    // Default timeout matches intrinsic/skills/internal/runtime_data.h
    shared_canceller =
        std::make_shared<SkillCancellationManager>(absl::Seconds(30));
  }
  return shared_canceller;
}

template <typename ServiceT, typename MockT>
std::shared_ptr<ServiceT> MaybeMock(const std::shared_ptr<ServiceT>& stub) {
  if (stub) {
    return stub;
  } else {
    return std::make_shared<MockT>();
  }
}

std::unique_ptr<ExecuteContext> SkillTestFactory::MakeExecuteContext(
    const ExecuteContextInitializer& initializer) {
  std::shared_ptr<SkillCanceller> canceller =
      MakeSkillCanceller(initializer.canceller);

  auto motion_planner_service = MaybeMock<MotionPlannerService::StubInterface,
                                          MockMotionPlannerServiceStub>(
      initializer.motion_planner_service);

  auto object_world_service =
      MaybeMock<ObjectWorldService::StubInterface, MockObjectWorldServiceStub>(
          initializer.object_world_service);

  // clang-format off
  return std::make_unique<ExecuteContextImpl>(
      canceller, initializer.equipment_pack.value_or(EquipmentPack()),
      initializer.logging_context.value_or(SkillLoggingContext()),
      motion_planning::MotionPlannerClient(initializer.world_id,
                                           motion_planner_service),
      world::ObjectWorldClient(initializer.world_id,
                               object_world_service),
      *geometry_library_,                 
      nullptr,
      initializer.context_id.value_or(GenerateDefaultTestContextId()));
  // clang-format on
}

std::unique_ptr<PreviewContext> SkillTestFactory::MakePreviewContext(
    const PreviewContextInitializer& initializer) {
  std::shared_ptr<SkillCanceller> canceller =
      MakeSkillCanceller(initializer.canceller);

  auto motion_planner_service = MaybeMock<MotionPlannerService::StubInterface,
                                          MockMotionPlannerServiceStub>(
      initializer.motion_planner_service);

  auto object_world_service =
      MaybeMock<ObjectWorldService::StubInterface, MockObjectWorldServiceStub>(
          initializer.object_world_service);

  // clang-format off
  return std::make_unique<PreviewContextImpl>(
      canceller, initializer.equipment_pack.value_or(EquipmentPack()),
      initializer.logging_context.value_or(SkillLoggingContext()),
      motion_planning::MotionPlannerClient(initializer.world_id,
                                           motion_planner_service),
      world::ObjectWorldClient(initializer.world_id,
                               object_world_service)

      , *geometry_library_

      , initializer.context_id.value_or(GenerateDefaultTestContextId())
  );
  // clang-format on
}

std::unique_ptr<GetFootprintContext> SkillTestFactory::MakeGetFootprintContext(
    const GetFootprintContextInitializer& initializer) {
  auto motion_planner_service = MaybeMock<MotionPlannerService::StubInterface,
                                          MockMotionPlannerServiceStub>(
      initializer.motion_planner_service);

  auto object_world_service =
      MaybeMock<ObjectWorldService::StubInterface, MockObjectWorldServiceStub>(
          initializer.object_world_service);

  // clang-format off
  return std::make_unique<GetFootprintContextImpl>(
      initializer.equipment_pack.value_or(EquipmentPack()),
      motion_planning::MotionPlannerClient(initializer.world_id,
                                          motion_planner_service),
      world::ObjectWorldClient(initializer.world_id,
                              object_world_service)

      , *geometry_library_

      , initializer.context_id.value_or(GenerateDefaultTestContextId())
  );
  // clang-format on
}

intrinsic_proto::resources::ResourceHandle SkillTestFactory::RunService(
    grpc::Service* service) {
  int port = 0;
  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();  // NOLINT (insecure)
  builder.AddListeningPort("localhost:0", creds, &port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(service);
  // Keep the server running until the factory is destroyed.
  std::unique_ptr<::grpc::Server> server = builder.BuildAndStart();
  CHECK_NE(server, nullptr);
  servers_.push_back(std::move(server));

  intrinsic_proto::resources::ResourceHandle handle;
  handle.mutable_connection_info()->mutable_grpc()->set_address(
      absl::StrCat("localhost:", port));
  return handle;
}

intrinsic_proto::assets::v1::ResolvedDependency::Interface
SkillTestFactory::RunService(grpc::Service* service,
                             absl::string_view instance_name) {
  return RunService(service, instance_name, 0);
}

intrinsic_proto::assets::v1::ResolvedDependency::Interface
SkillTestFactory::RunService(grpc::Service* service,
                             absl::string_view instance_name, int port) {
  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> creds =
      grpc::InsecureServerCredentials();  // NOLINT (insecure)
  builder.AddListeningPort("localhost:0", creds, &port);
  builder.AddChannelArgument(GRPC_ARG_ALLOW_REUSEPORT, 0);
  builder.RegisterService(service);
  // Keep the server running until the factory is destroyed.
  std::unique_ptr<::grpc::Server> server = builder.BuildAndStart();
  CHECK_NE(server, nullptr);
  servers_.push_back(std::move(server));

  intrinsic_proto::assets::v1::ResolvedDependency::Interface interface;
  interface.mutable_grpc()->mutable_connection()->set_address(
      absl::StrCat("localhost:", port));
  auto metadata =
      interface.mutable_grpc()->mutable_connection()->add_metadata();
  metadata->set_key("x-resource-instance-name");
  metadata->set_value(instance_name);
  return interface;
}


intrinsic_proto::skills::SkillManifest GetManifestOrDie(
    const char* manifest_path) {
  auto manifest = ReadBinaryProto<intrinsic_proto::skills::SkillManifest>(
      PathResolver::ResolveRunfilesPathForTest(manifest_path));
  CHECK_OK(manifest.status());
  return *manifest;
}


}  // namespace skills
}  // namespace intrinsic
