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

#include "intrinsic/skills/internal/skill_registry_client.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/channel_arguments.h"
#include "grpcpp/support/status.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/skills/cc/client_common.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/internal/proto/behavior_tree_registry_internal.grpc.pb.h"
#include "intrinsic/skills/internal/proto/behavior_tree_registry_internal.pb.h"
#include "intrinsic/skills/internal/proto/skill_registry_internal.grpc.pb.h"
#include "intrinsic/skills/internal/proto/skill_registry_internal.pb.h"
#include "intrinsic/skills/proto/behavior_tree_registry.grpc.pb.h"
#include "intrinsic/skills/proto/behavior_tree_registry.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skill_registry.grpc.pb.h"
#include "intrinsic/skills/proto/skill_registry.pb.h"
#include "intrinsic/skills/proto/skill_registry_config.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/util/status/annotate.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace skills {

absl::StatusOr<std::unique_ptr<SkillRegistryClient>> CreateSkillRegistryClient(
    absl::string_view grpc_address, absl::Duration timeout) {
  // Increase the message size limits.
  //
  // Some of the responses we're receiving here may contain the proto
  // descriptors for multiple skills which can get large. In practice, we've
  // seen up to ~4MB so far.
  //
  // TODO(b/356228011): Pagination is the better approach for this.
  grpc::ChannelArguments channel_args =
      connect::UnlimitedMessageSizeGrpcChannelArgs();

  // The skill registry may need to call out to one or more skill information
  // services. Those services might not be ready at startup. We configure a
  // retry policy to mitigate b/283020857.
  // (See
  // https://github.com/grpc/grpc-go/blob/master/examples/features/retry/README.md
  //  for an example of this gRPC feature.)
  channel_args.SetServiceConfigJSON(R"(
      {
        "methodConfig": [{
          "name": [{"service": "intrinsic_proto.skills.SkillRegistry"}],
          "waitForReady": true,
          "timeout": "300s",
          "retryPolicy": {
              "maxAttempts": 10,
              "initialBackoff": "0.1s",
              "maxBackoff": "10s",
              "backoffMultiplier": 1.5,
              "retryableStatusCodes": [ "UNAVAILABLE" ]
          }
        }]
      })");

  INTR_ASSIGN_OR_RETURN(std::shared_ptr<grpc::Channel> channel,
                        connect::CreateClientChannel(
                            grpc_address, absl::Now() + timeout, channel_args));

  return std::make_unique<SkillRegistryClient>(
      intrinsic_proto::skills::SkillRegistryInternal::NewStub(channel),
      intrinsic_proto::skills::SkillRegistry::NewStub(channel),
      intrinsic_proto::skills::BehaviorTreeRegistryInternal::NewStub(channel),
      intrinsic_proto::skills::BehaviorTreeRegistry::NewStub(channel));
}

absl::StatusOr<std::vector<intrinsic_proto::skills::Skill>>
SkillRegistryClient::GetSkills() const {
  return GetSkills(kClientDefaultTimeout);
}

absl::StatusOr<std::vector<intrinsic_proto::skills::Skill>>
SkillRegistryClient::GetSkills(absl::Duration timeout) const {
  ::grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + timeout));

  google::protobuf::Empty request;
  intrinsic_proto::skills::GetSkillsResponse response;

  ::grpc::Status status = stub_->GetSkills(&context, request, &response);
  if (!status.ok()) {
    return AnnotateError(
        ToAbslStatus(status),
        absl::StrCat(
            "SkillRegistryClient::GetSkills gRPC call failed; (",
            absl::StatusCodeToString(absl::StatusCode(status.error_code())),
            ")"));
  }

  // Convert to std::vector.
  return std::vector<intrinsic_proto::skills::Skill>(response.skills().begin(),
                                                     response.skills().end());
}

absl::StatusOr<intrinsic_proto::skills::Skill>
SkillRegistryClient::GetSkillById(absl::string_view skill_id) const {
  ::grpc::ClientContext context;
  intrinsic_proto::skills::GetSkillRequest req;
  req.set_id(skill_id);
  intrinsic_proto::skills::GetSkillResponse resp;
  INTR_RETURN_IF_ERROR(ToAbslStatus(stub_->GetSkill(&context, req, &resp)));
  return resp.skill();
}

absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
SkillRegistryClient::GetBehaviorTree(absl::string_view skill_id) const {
  return GetBehaviorTree(skill_id, kClientDefaultTimeout);
}

absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
SkillRegistryClient::GetBehaviorTree(absl::string_view skill_id,
                                     absl::Duration timeout) const {
  ::grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + timeout));

  intrinsic_proto::skills::GetBehaviorTreeRequest req;
  req.set_id(skill_id);
  intrinsic_proto::skills::GetBehaviorTreeResponse resp;
  ::grpc::Status status =
      bt_stub_internal_->GetBehaviorTree(&context, req, &resp);
  if (!status.ok()) {
    return AnnotateError(
        ToAbslStatus(status),
        absl::StrCat(
            "SkillRegistryClient::GetBehaviorTree gRPC call failed; (",
            absl::StatusCodeToString(absl::StatusCode(status.error_code())),
            ")"));
  }

  return resp.behavior_tree();
}

absl::Status SkillRegistryClient::RegisterOrUpdateSkill(
    intrinsic_proto::skills::SkillRegistration skill_registration) const {
  return RegisterOrUpdateSkill(skill_registration, kClientDefaultTimeout);
}

absl::Status SkillRegistryClient::RegisterOrUpdateSkill(
    intrinsic_proto::skills::SkillRegistration skill_registration,
    absl::Duration timeout) const {
  ::grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + timeout));

  intrinsic_proto::skills::RegisterOrUpdateSkillRequest request;
  *request.mutable_skill_registration() = skill_registration;

  google::protobuf::Empty response;
  return ToAbslStatus(
      stub_internal_->RegisterOrUpdateSkill(&context, request, &response));
}

absl::Status SkillRegistryClient::RegisterOrUpdateBehaviorTree(
    intrinsic_proto::skills::BehaviorTreeRegistration
        behavior_tree_registration) const {
  return RegisterOrUpdateBehaviorTree(behavior_tree_registration,
                                      kClientDefaultTimeout);
}

absl::Status SkillRegistryClient::RegisterOrUpdateBehaviorTree(
    intrinsic_proto::skills::BehaviorTreeRegistration
        behavior_tree_registration,
    absl::Duration timeout) const {
  ::grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + timeout));

  intrinsic_proto::skills::RegisterOrUpdateBehaviorTreeRequest request;
  *request.mutable_registration() = behavior_tree_registration;

  intrinsic_proto::skills::RegisterOrUpdateBehaviorTreeResponse response;
  return ToAbslStatus(
      bt_stub_->RegisterOrUpdateBehaviorTree(&context, request, &response));
}

absl::Status SkillRegistryClient::UnregisterBehaviorTree(
    std::string skill_id) const {
  return UnregisterBehaviorTree(skill_id, kClientDefaultTimeout);
}

absl::Status SkillRegistryClient::UnregisterBehaviorTree(
    std::string skill_id, absl::Duration timeout) const {
  ::grpc::ClientContext context;
  context.set_deadline(absl::ToChronoTime(absl::Now() + timeout));

  intrinsic_proto::skills::UnregisterBehaviorTreeRequest request;
  request.set_id(skill_id);

  google::protobuf::Empty response;
  return ToAbslStatus(
      bt_stub_->UnregisterBehaviorTree(&context, request, &response));
}

}  // namespace skills
}  // namespace intrinsic
