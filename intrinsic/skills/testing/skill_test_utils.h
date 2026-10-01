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

#ifndef INTRINSIC_SKILLS_TESTING_SKILL_TEST_UTILS_H_
#define INTRINSIC_SKILLS_TESTING_SKILL_TEST_UTILS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "grpcpp/impl/service_type.h"
#include "grpcpp/server.h"
#include "internal/testing.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/geometry/storage/geometry_library.h"  
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_canceller.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_logging_context.h"
#include "intrinsic/skills/proto/skill_manifest.pb.h"  
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"

namespace intrinsic {
namespace skills {

// Calls a skill's Execute() method and optionally assigns its output to the
// specified result parameter.
template <typename TResult>
absl::Status ExecuteSkill(SkillExecuteInterface& skill,
                          const ExecuteRequest& request,
                          ExecuteContext& context, TResult* result) {
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<::google::protobuf::Message> result_msg,
                        skill.Execute(request, context));

  if (result_msg->GetDescriptor()->full_name() !=
      TResult::descriptor()->full_name()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Skill returned result of type %s, but caller wants %s.",
        result_msg->GetDescriptor()->full_name(),
        TResult::descriptor()->full_name()));
  }

  // TODO(b/308635152): Not needed once we use templated methods.
  // We serialize->deserialize here rather than casting to prevent undefined
  // behavior in case the user specifies an incorrect result type. The process
  // is inefficient, but hopefully that doesn't matter for testing.
  if (result != nullptr &&
      !result->ParseFromString(result_msg->SerializeAsString())) {
    return absl::InternalError(
        "Could not parse result message as target type.");
  }

  return absl::OkStatus();
}
absl::Status ExecuteSkill(SkillExecuteInterface& skill,
                          const ExecuteRequest& request,
                          ExecuteContext& context);

template <typename TResult>
absl::StatusOr<TResult> ExecuteSkill(SkillExecuteInterface& skill,
                                     const ExecuteRequest& request,
                                     ExecuteContext& context) {
  TResult result;
  INTR_RETURN_IF_ERROR(ExecuteSkill(skill, request, context, &result));
  return result;
}

// Calls a skill's Preview() method and optionally assigns its output to the
// specified result parameter.
template <typename TResult>
absl::Status PreviewSkill(SkillExecuteInterface& skill,
                          const PreviewRequest& request,
                          PreviewContext& context, TResult* result) {
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<::google::protobuf::Message> result_msg,
                        skill.Preview(request, context));

  if (result_msg->GetDescriptor()->full_name() !=
      TResult::descriptor()->full_name()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Skill returned result of type %s, but caller wants %s.",
        result_msg->GetDescriptor()->full_name(),
        TResult::descriptor()->full_name()));
  }

  // TODO(b/308635152): Not needed once we use templated methods.
  // We serialize->deserialize here rather than casting to prevent undefined
  // behavior in case the user specifies an incorrect result type. The process
  // is inefficient, but hopefully that doesn't matter for testing.
  if (result != nullptr &&
      !result->ParseFromString(result_msg->SerializeAsString())) {
    return absl::InternalError(
        "Could not parse result message as target type.");
  }

  return absl::OkStatus();
}
absl::Status PreviewSkill(SkillExecuteInterface& skill,
                          const PreviewRequest& request,
                          PreviewContext& context);

template <typename TResult>
absl::StatusOr<TResult> PreviewSkill(SkillExecuteInterface& skill,
                                     const PreviewRequest& request,
                                     PreviewContext& context) {
  TResult result;
  INTR_RETURN_IF_ERROR(PreviewSkill(skill, request, context, &result));
  return result;
}

// Creates objects needed to unit test skills.
//
// `intrinsic::skills::SkillInterface` defines methods for skills to implement.
// These methods accept request and context parameters.
// `SkillTestFactory` provides methods to make it easier to exercise these
// methods in unit tests.
//
// Example: Test Execute method on a skill that has no dependencies in its
//          manifest.
//
//    TEST(MySkillTest, Execute) {
//      auto skill_test_factory = SkillTestFactory();
//      auto skill = MySkill::CreateSkill();
//      MySkillParams params;
//
//      ExecuteRequest request = skill_test_factory.MakeExecuteRequest(params);
//      std::unique_ptr<ExecuteContext> context =
//        skill_test_factory.MakeExecuteContext({});
//      ASSERT_OK(skill->Execute(request, context));
//    }
//
// Example: Test that an Execute method supports cancellation.
//
//    TEST(MySkillTest, ExecuteSupportsCancellation) {
//      auto skill_test_factory = SkillTestFactory();
//      auto skill = MySkill::CreateSkill();
//      MySkillParams params;
//
//      ExecuteRequest request = skill_test_factory.MakeExecuteRequest(params);
//      SkillCancellationManager canceller(absl::Seconds(10));
//      std::unique_ptr<ExecuteContext> context =
//        skill_test_factory.MakeExecuteContext({.canceller = &canceller});
//
//      Thread cancel_skill([&canceller]() {
//        ASSERT_OK(canceller.WaitForReady());
//        ASSERT_OK(canceller.Cancel());
//      });
//
//      auto result = skill->Execute(request, *context);
//      cancel_skill.join();
//
//      EXPECT_TRUE(absl::IsCancelled(result.status()));
//    }
//
// Example: Test Execute method on a skill that depends on one service, and you
//          have access to either a real or fake implementation of that service.
//
//    TEST(MySkillTest, Execute) {
//      auto skill_test_factory = SkillTestFactory();
//      auto skill = MySkill::CreateSkill();
//      MySkillParams params;
//      SomeServiceImpl some_service;
//      auto resource_handle = skill_test_factory.RunService(&some_service);
//      EquipmentPack equipment;
//      ASSERT_OK(equipment.Add("some_slot", resource_handle));
//
//      ExecuteRequest request = skill_test_factory.MakeExecuteRequest(params);
//      std::unique_ptr<ExecuteContext> context =
//        skill_test_factory.MakeExecuteContext({.equipment_pack = equipment});
//      ASSERT_OK(skill->Execute(request, context));
//    }
//
// Example: Test Preview method on a skill that has no dependencies in its
//          manifest.
//
//    TEST(MySkillTest, Preview) {
//      auto skill_test_factory = SkillTestFactory();
//      auto skill = MySkill::CreateSkill();
//      MySkillParams params;
//
//      PreviewRequest request = skill_test_factory.MakePreviewRequest(params);
//      std::unique_ptr<PreviewContext> context =
//        skill_test_factory.MakePreviewContext({});
//      ASSERT_OK(skill->Preview(request, context));
//    }
//
// Example: Test GetFootprint method on a skill that has no dependencies in its
//          manifest.
//
//    TEST(MySkillTest, GetFootprint) {
//      auto skill_test_factory = SkillTestFactory();
//      auto skill = MySkill::CreateSkill();
//      MySkillParams params;
//
//      GetFootprintRequest request =
//      skill_test_factory.MakeGetFootprintRequest(params);
//      std::unique_ptr<GetFootprintContext> context =
//        skill_test_factory.MakeGetFootprintContext({});
//      ASSERT_OK(skill->GetFootprint(request, context));
//    }
//
class SkillTestFactory final {
 public:
  SkillTestFactory();

  explicit SkillTestFactory(
      std::shared_ptr<::intrinsic::GeometryLibrary> geometry_library);

  ~SkillTestFactory() = default;

  // Enable move construction and assignment.
  SkillTestFactory(const SkillTestFactory&) = delete;
  SkillTestFactory& operator=(const SkillTestFactory&) = delete;
  SkillTestFactory(SkillTestFactory&&) = default;
  SkillTestFactory& operator=(SkillTestFactory&&) = default;

  // Runs a service that allows connections from localhost and returns a
  // ResourceHandle that can be used to connect to the service.
  // This call does not take ownership of the service, but it does create and
  // keep ownership of a gRPC server. The gRPC server is destroyed when the
  // SkillTestFactory is destroyed.
  intrinsic_proto::resources::ResourceHandle RunService(grpc::Service* service);

  // Runs a service that allows connections from localhost and returns a
  // ResolvedDependency::Interface that can be used to connect to the service.
  // This call does not take ownership of the service, but it does create and
  // keep ownership of a gRPC server. The gRPC server is destroyed when the
  // SkillTestFactory is destroyed.
  intrinsic_proto::assets::v1::ResolvedDependency::Interface RunService(
      grpc::Service* service, absl::string_view instance_name);

  intrinsic_proto::assets::v1::ResolvedDependency::Interface RunService(
      grpc::Service* service, absl::string_view instance_name, int port);

  // Creates an `ExecuteRequest` for testing a skill's Execute() method.
  //
  // See `ExecuteRequest` for the meanings of the arguments to this function.
  ExecuteRequest MakeExecuteRequest(
      const ::google::protobuf::Message& params,
      ::google::protobuf::Message* param_defaults = nullptr);

  // Creates a `PreviewRequest` for testing a skill's Preview() method.
  //
  // See `PreviewRequest` for the meanings of the arguments to this function.
  PreviewRequest MakePreviewRequest(
      const ::google::protobuf::Message& params,
      ::google::protobuf::Message* param_defaults = nullptr);

  // Creates a `GetFootprintRequest` for testing a skill's GetFootprint()
  // method.
  //
  // See `GetFootprintRequest` for the meanings of the arguments to this
  // function.
  GetFootprintRequest MakeGetFootprintRequest(
      const ::google::protobuf::Message& params,
      ::google::protobuf::Message* param_defaults = nullptr);

  // Initializes an `ExecuteContext` for testing a skill's Execute() method.
  //
  // All fields are optional.
  //
  // `canceller` holds a SkillCanceller instance that can be used to cancel a
  // call to a skill. You probably want to pass a `SkillCancellationManager`
  // instance. The struct does not retain ownership of the canceller.
  // If provided, the caller must ensure that the instance remains valid for the
  // lifetime of the `ExecuteContext` instance.
  //
  // `equipment_pack` holds a set of equipment containing resource handles that
  // can be used to connect to services needed by the skill. See `RunService`
  // for a convenient way to create service instances and resource handles.
  //
  // `logging_context` holds a SkillLoggingContext instance that can be used to
  // log information about the skill.
  //
  // `world_id` holds the id of the world that the skill should use.
  //
  // `motion_planner_service` holds a stub to a `MotionPlannerService` instance
  // that the skill can use. If not provided, `MakeExecuteContext` will create
  // a mock instance.
  //
  // `object_world_service` holds a stub to an `ObjectWorldService` that the
  // skill can use. If not provided, `MakeExecuteContext` will create a mock
  // instance.
  struct ExecuteContextInitializer {
    // If provided the canceller instance remains owned by the caller.
    SkillCanceller* canceller = nullptr;
    std::optional<EquipmentPack> equipment_pack;
    std::optional<SkillLoggingContext> logging_context;
    std::string world_id = "fake_world";
    std::shared_ptr<intrinsic_proto::motion_planning::v1::MotionPlannerService::
                        StubInterface>
        motion_planner_service;
    std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
        object_world_service;
    std::optional<std::string> context_id = std::nullopt;
  };

  // Creates an `ExecuteContext` for testing a skill's Execute() method.
  //
  // See `ExecuteContextInitializer` to learn how to pass arguments to this
  // function.
  std::unique_ptr<ExecuteContext> MakeExecuteContext(
      const ExecuteContextInitializer& initializer);

  // Initializes an `PreviewContext` for testing a skill's Preview() method.
  //
  // All fields are optional. See `ExecuteContextInitializer` for the usage
  // of the fields as they are the same for `PreviewContextInitializer`.
  struct PreviewContextInitializer {
    // If provided the canceller instance remains owned by the caller.
    SkillCanceller* canceller = nullptr;
    std::optional<EquipmentPack> equipment_pack;
    std::optional<SkillLoggingContext> logging_context;
    std::string world_id = "fake_world";
    std::shared_ptr<intrinsic_proto::motion_planning::v1::MotionPlannerService::
                        StubInterface>
        motion_planner_service;
    std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
        object_world_service;
    std::optional<std::string> context_id = std::nullopt;
  };

  // Creates a `PreviewContext` for testing a skill's Preview() method.
  //
  // See `PreviewContextInitializer` to learn how to pass arguments to this
  // function.
  std::unique_ptr<PreviewContext> MakePreviewContext(
      const PreviewContextInitializer& initializer);

  // Initializes a `GetFootprintContext` for testing a skill's GetFootprint()
  // method.
  //
  // All fields are optional. See `ExecuteContextInitializer` for the usage
  // of the fields as they are the same for `GetFootprintContextInitializer`.
  struct GetFootprintContextInitializer {
    std::optional<EquipmentPack> equipment_pack;
    std::string world_id = "fake_world";
    std::shared_ptr<intrinsic_proto::motion_planning::v1::MotionPlannerService::
                        StubInterface>
        motion_planner_service;
    std::shared_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
        object_world_service;
    std::optional<std::string> context_id = std::nullopt;
  };

  // Creates a `GetFootprintContext` for testing a skill's GetFootprint()
  // method.
  //
  // See `GetFootprintContextInitializer` to learn how to pass arguments to this
  // function.
  std::unique_ptr<GetFootprintContext> MakeGetFootprintContext(
      const GetFootprintContextInitializer& initializer);

 private:
  std::vector<std::unique_ptr<::grpc::Server>> servers_;

  std::shared_ptr<::intrinsic::GeometryLibrary> geometry_library_;

};


// Gets a skill's manifest from runfiles or terminates the program.
//
// The `manifest_path` argument must be a path in runfiles leading to a binary
// proto file. To load a manifest defined by a `skill_manifest` rule like
// `skill_manifest(name = "foobar", ...)`, first add a data dependency on the
// `foobar` target and then pass in the runfiles path to that target plus
// `.pbbin` (ex: `/workspace_name/path/to/foobar.pbbin`) to this function.
intrinsic_proto::skills::SkillManifest GetManifestOrDie(
    const char* manifest_path);


}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_TESTING_SKILL_TEST_UTILS_H_
