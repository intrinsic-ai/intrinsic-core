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
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/skills/internal/skill_service_client.h"
#include "intrinsic/skills/proto/skill_service.grpc.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/unique_id.h"

namespace intrinsic::executive {

absl::StatusOr<std::unique_ptr<skills::SkillServiceClientInterface>>
CreateSkillServiceClient(
    const intrinsic_proto::skills::SkillInstance& instance_proto,
    std::optional<absl::Duration> connect_timeout,
    const SkillServiceChannelCreator& create_skill_service_channel,
    SkillServiceClientCreator skill_service_client_creator_mock) {
  const stats::ScopedSpan span_client("SkillInstance/CreateSkillServiceClient");
  std::unique_ptr<skills::SkillServiceClientInterface> client;
  if (skill_service_client_creator_mock) {
    INTR_ASSIGN_OR_RETURN(client,
                          skill_service_client_creator_mock(instance_proto));
  } else if (connect_timeout.has_value() && create_skill_service_channel) {
    const absl::Time deadline = absl::Now() + *connect_timeout;
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> project_channel,
        create_skill_service_channel(
            instance_proto.project_handle().grpc_target(), deadline));
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> execute_channel,
        create_skill_service_channel(
            instance_proto.execute_handle().grpc_target(), deadline));
    client = std::make_unique<skills::SkillServiceClient>(
        instance_proto,
        intrinsic_proto::skills::Projector::NewStub(project_channel),
        intrinsic_proto::skills::Executor::NewStub(execute_channel),
        skills::SkillServiceClientConfig{.timeout = *connect_timeout});
  } else {
    return absl::FailedPreconditionError(
        "CreateSkillServiceClient: A skill client was requested, but neither a "
        "mock nor a production connect timeout was configured. If this is a "
        "test, you likely need to call SetSkillServiceClientCreatorForTest().");
  }
  return client;
}

SkillInstance::SkillInstance() : mutex_(std::make_unique<absl::Mutex>()) {}

SkillInstance::~SkillInstance() {
  // client_ is null if this instance was moved from or if Create failed before
  // the client was created. In both cases there is nothing to tear down.
  if (client_ != nullptr) {
    client_->TearDownConcurrentLogging();
  }
}

absl::StatusOr<SkillInstance> SkillInstance::Create(
    const intrinsic_proto::executive::BehaviorCall& behavior_call,
    const CreationContext& context) {
  const stats::ScopedSpan span("SkillInstance/Create");

  SkillInstance skill_instance;
  INTR_RETURN_IF_ERROR(skill_instance.InitializeInstanceProto(
      behavior_call.skill_id(),
      behavior_call.has_instance_name()
          ? std::make_optional<absl::string_view>(behavior_call.instance_name())
          : std::nullopt,
      context.grpc_targets, context.id_version));

  INTR_RETURN_IF_ERROR(skill_instance.FillEquipmentPack(
      behavior_call.skill_id(), behavior_call.resources(), context));

  INTR_ASSIGN_OR_RETURN(skill_instance.client_,
                        CreateSkillServiceClient(
                            skill_instance.instance_, context.connect_timeout,
                            context.create_skill_service_channel,
                            context.skill_service_client_creator_mock));

  // Concurrent logging belongs to the lifetime of the instance: It is
  // initialized here and torn down in the destructor.
  // The instance is moved after this point as it is returned by value. This
  // does not affect logging: Only the pointer to the client moves, not the
  // client itself with its logging thread.
  skill_instance.client_->InitConcurrentLogging();

  return skill_instance;
}

absl::Status SkillInstance::FillEquipmentPack(
    absl::string_view skill_id,
    const google::protobuf::Map<
        std::string,
        intrinsic_proto::executive::BehaviorCall::ResourceSpecification>&
        resources,
    const CreationContext& context) {
  for (const std::string& resource_slot : context.required_equipment_keys) {
    auto resource_spec_it = resources.find(resource_slot);
    if (resource_spec_it == resources.end()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Required resource slot '%s' missing in BehaviorCall for skill %s.",
          resource_slot, skill_id));
    }
    const auto& resource_spec = resource_spec_it->second;
    if (!resource_spec.has_handle()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Resource slot '%s' is not a handle, but "
                          "reference '%s' in BehaviorCall for skill %s.",
                          resource_slot, resource_spec.reference(), skill_id));
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
    absl::string_view skill_id, std::optional<absl::string_view> instance_name,
    const intrinsic_proto::assets::v1alpha1::InternalSkillData::GrpcTargets&
        grpc_targets,
    absl::string_view id_version) {
  const stats::ScopedSpan span_client("SkillInstance/InitializeInstanceProto");
  if (instance_name.has_value()) {
    instance_.set_instance_name(std::string(*instance_name));
  } else {
    instance_.set_instance_name(
        absl::StrCat(skill_id, "/", intrinsic::UniqueId()));
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
