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

#include "intrinsic/executive/engine/skill_instance.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/engine/skill_action.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/skills/internal/skill_service_client.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/unique_id.h"

namespace intrinsic::executive {

namespace {

absl::Status ResolveSkillParameterDependencies(
    google::protobuf::Any& parameter, const assets::Resolver& resolver,
    const clips::ProtobufManager::DescriptorPoolInfo& pool_info,
    SkillAction skill_action,
    const absl::flat_hash_map<std::string, std::string>&
        fallback_manifest_dependencies) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> casted_parameter,
      clips::ProtobufManager::CastFromAnyWithPool(parameter, pool_info, ""));
  INTR_RETURN_IF_ERROR(resolver.ResolveParameterDependenciesWithFallback(
      *casted_parameter, skill_action, fallback_manifest_dependencies,
      pool_info.descriptor_pool, pool_info.message_factory));
  if (!parameter.PackFrom(*casted_parameter)) {
    return absl::InternalError("Failed to pack parameter");
  }
  return absl::OkStatus();
}

absl::StatusOr<bool> HasAnnotatedResolvedDependency(
    const google::protobuf::Any& parameter,
    const clips::ProtobufManager::DescriptorPoolInfo& pool_info) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> casted_parameter,
      clips::ProtobufManager::CastFromAnyWithPool(parameter, pool_info, ""));
  return ::intrinsic::assets::dependencies::HasResolvedDependency(
      *casted_parameter->GetDescriptor(),
      ::intrinsic::assets::dependencies::ResolvedDepsIntrospectionOptions{
          .check_dependency_annotation = true,
          .check_skill_annotations = false,
      });
}

// Perform skill parameter resolving on the given BehaviorCall. This might
// modify the BehaviorCall's parameters when dependencies are resolved.
//
// A non-OK Status is returned, when the resolving could not be performed.
absl::Status ResolveSkillParameterDependenciesFromResolvedDependencies(
    intrinsic_proto::executive::BehaviorCall& behavior_call,
    SkillAction skill_action, const SkillInstance::CreationContext& context) {
  if (!context.pool_info.has_value()) {
    return absl::InternalError(absl::StrFormat(
        "SkillInstance::Create called without pool information that is "
        "required to deserialize its parameters for skill %s",
        behavior_call.skill_id()));
  }
  // If the skill parameters have a resolved dependency, we need to resolve
  // them.
  INTR_ASSIGN_OR_RETURN(bool has_resolved_dependency,
                        HasAnnotatedResolvedDependency(
                            behavior_call.parameters(), *context.pool_info));
  if (has_resolved_dependency) {
    absl::flat_hash_map<std::string, std::string>
        fallback_manifest_dependencies;
    for (const auto& [resource_slot, resource_spec] :
         behavior_call.resources()) {
      if (resource_spec.has_handle()) {
        fallback_manifest_dependencies[resource_slot] = resource_spec.handle();
      }
    }
    INTR_RETURN_IF_ERROR(ResolveSkillParameterDependencies(
        *behavior_call.mutable_parameters(), context.resolver,
        *context.pool_info, skill_action, fallback_manifest_dependencies));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<skills::SkillServiceClientInterface>>
CreateSkillServiceClient(
    const intrinsic_proto::skills::SkillInstance& instance_proto,
    std::optional<absl::Duration> connect_timeout,
    SkillServiceClientCreator skill_service_client_creator_mock) {
  const stats::ScopedSpan span_client("SkillInstance/CreateSkillServiceClient");
  std::unique_ptr<skills::SkillServiceClientInterface> client;
  if (skill_service_client_creator_mock) {
    INTR_ASSIGN_OR_RETURN(client,
                          skill_service_client_creator_mock(instance_proto));
  } else if (connect_timeout.has_value()) {
    INTR_ASSIGN_OR_RETURN(client,
                          skills::CreateSkillServiceClient(
                              instance_proto, {.timeout = *connect_timeout}));
  } else {
    return absl::FailedPreconditionError(
        "CreateSkillServiceClient: A skill client was requested, but neither a "
        "mock nor a production connect timeout was configured. If this is a "
        "test, you likely need to call SetSkillServiceClientCreatorForTest().");
  }
  return client;
}

SkillInstance::SkillInstance() : mutex_(std::make_unique<absl::Mutex>()) {}

absl::StatusOr<SkillInstance> SkillInstance::Create(
    intrinsic_proto::executive::BehaviorCall& behavior_call,
    SkillAction skill_action, const CreationContext& context) {
  const stats::ScopedSpan span("SkillInstance/Create");

  // If there are no parameters, we cannot resolve anything. This usually only
  // happens in tests.
  if (behavior_call.has_parameters()) {
    INTR_RETURN_IF_ERROR(
        ResolveSkillParameterDependenciesFromResolvedDependencies(
            behavior_call, skill_action, context));
  }

  SkillInstance skill_instance;
  INTR_RETURN_IF_ERROR(skill_instance.InitializeInstanceProto(
      behavior_call, context.grpc_targets, context.id_version));

  INTR_RETURN_IF_ERROR(
      skill_instance.FillEquipmentPack(behavior_call, context));

  INTR_ASSIGN_OR_RETURN(skill_instance.client_,
                        CreateSkillServiceClient(
                            skill_instance.instance_, context.connect_timeout,
                            context.skill_service_client_creator_mock));

  return skill_instance;
}

absl::Status SkillInstance::FillEquipmentPack(
    const intrinsic_proto::executive::BehaviorCall& behavior_call,
    const CreationContext& context) {
  for (const std::string& resource_slot : context.required_equipment_keys) {
    auto resource_spec_it = behavior_call.resources().find(resource_slot);
    if (resource_spec_it == behavior_call.resources().end()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Required resource slot '%s' missing in BehaviorCall for skill %s.",
          resource_slot, behavior_call.skill_id()));
    }
    const auto& resource_spec = resource_spec_it->second;
    if (!resource_spec.has_handle()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Resource slot '%s' is not a handle, but "
          "reference '%s' in BehaviorCall for skill %s.",
          resource_slot, resource_spec.reference(), behavior_call.skill_id()));
    }
    const std::string& resource_name = resource_spec.handle();
    auto handle_it = context.resource_handles.find(resource_name);
    if (handle_it == context.resource_handles.end()) {
      return absl::NotFoundError(
          absl::StrFormat("Resource '%s' not listed by resource registry "
                          "or not in local cache",
                          resource_name));
    }
    const intrinsic_proto::resources::ResourceHandle& handle =
        handle_it->second;
    instance_.mutable_resource_handles()->insert({resource_slot, handle});
  }
  return absl::OkStatus();
}

absl::Status SkillInstance::InitializeInstanceProto(
    const intrinsic_proto::executive::BehaviorCall& behavior_call,
    const intrinsic_proto::assets::v1alpha1::InternalSkillData::GrpcTargets&
        grpc_targets,
    absl::string_view id_version) {
  const stats::ScopedSpan span_client("SkillInstance/InitializeInstanceProto");
  if (behavior_call.has_instance_name()) {
    instance_.set_instance_name(behavior_call.instance_name());
  } else {
    instance_.set_instance_name(
        absl::StrCat(behavior_call.skill_id(), "/", intrinsic::UniqueId()));
  }
  instance_.set_id_version(std::string(id_version));
  instance_.mutable_project_handle()->set_grpc_target(grpc_targets.project());
  instance_.mutable_execute_handle()->set_grpc_target(grpc_targets.execute());

  return absl::OkStatus();
}

void SkillInstance::AbandonCall() {
  absl::MutexLock lock(*mutex_);
  abandoned_ = true;
  if (wait_context_ != nullptr) {
    // Note unlike TryCancelExecute on the skill client this works
    // directly on the gRPC context and thus gives no guarantees on
    // anything being canceled on the skill server. This is *not* skill
    // cancellation by the Intrinsic platform APIs that works on the skill
    // server, but a gRPC cancel call on the grpc client.
    // We can just call this here directly as it is thread-safe.
    wait_context_->TryCancel();
  } else {
    LOG(INFO) << "Failed to abandon call for skill '" << instance_.id_version()
              << "' as there is no ongoing call.";
  }
}

bool SkillInstance::IsAbandoned() {
  absl::MutexLock lock(*mutex_);
  return abandoned_;
}

void SkillInstance::UpdateWaitContext(grpc::ClientContext* context) {
  absl::MutexLock lock(*mutex_);
  wait_context_ = context;
}

bool SkillInstance::UpdateWaitContextUnlessAbandoned(
    grpc::ClientContext* context) {
  absl::MutexLock lock(*mutex_);
  if (abandoned_) {
    return false;
  }
  wait_context_ = context;
  return true;
}

}  // namespace intrinsic::executive
