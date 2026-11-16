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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_SKILL_INSTANCE_H_
#define INTRINSIC_EXECUTIVE_ENGINE_SKILL_INSTANCE_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/proto/v1alpha1/asset_info_internal.pb.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/internal/skill_registry_client_interface.h"
#include "intrinsic/skills/internal/skill_service_client_interface.h"
#include "intrinsic/skills/proto/skills.pb.h"

namespace intrinsic::executive {

using SkillServiceClientCreator = std::function<
    absl::StatusOr<std::unique_ptr<skills::SkillServiceClientInterface>>(
        intrinsic_proto::skills::SkillInstance)>;

// Establishes a connection to the skill service.
absl::StatusOr<std::unique_ptr<skills::SkillServiceClientInterface>>
CreateSkillServiceClient(
    const intrinsic_proto::skills::SkillInstance& instance_proto,
    std::optional<absl::Duration> connect_timeout,
    SkillServiceClientCreator skill_service_client_creator_mock);

// An instance for a specific skill call
class SkillInstance {
  SkillInstance();
  SkillInstance(const SkillInstance&) = delete;
  SkillInstance& operator=(const SkillInstance&) = delete;

 public:
  SkillInstance(SkillInstance&&) = default;
  // Not move-assignable: This would drop the client of the assigned-to instance
  // without tearing down its concurrent logging (and doing that can take
  // significant time).
  SkillInstance& operator=(SkillInstance&&) = delete;

  // Tears down concurrent logging of the skill service client.
  // NOTE: This makes destroying a SkillInstance a potentially long blocking
  // operation, as it waits for the logging thread to finish. Whoever releases
  // the last reference to a SkillInstance must thus do so outside of locks and
  // not in the CLIPS thread.
  ~SkillInstance();

  struct CreationContext {
    intrinsic_proto::assets::v1alpha1::InternalSkillData::GrpcTargets
        grpc_targets;  // defines how to connect to this skill instance
    const absl::flat_hash_map<std::string,
                              intrinsic_proto::resources::ResourceHandle>&
        resource_handles;
    std::optional<absl::Duration> connect_timeout;
    SkillServiceClientCreator skill_service_client_creator_mock = nullptr;
    std::vector<std::string> required_equipment_keys;
    std::string id_version;
  };

  // Creates a skill instance for the behavior call's skill id.
  // If the behavior call specifies an instance_name that is taken, otherwise an
  // instance name is generated.
  // The old-style resources field of the behavior call is stored in the
  // instance proto for skill calls.
  // Concurrent logging of the skill service client is initialized on creation
  // and torn down when the SkillInstance is destroyed.
  static absl::StatusOr<SkillInstance> Create(
      const intrinsic_proto::executive::BehaviorCall& behavior_call,
      const CreationContext& context);

  const skills::SkillServiceClientInterface* GetClient() const {
    return client_.get();
  }
  skills::SkillServiceClientInterface* GetClient() { return client_.get(); }

  const intrinsic_proto::skills::SkillInstance& GetInstanceProto() const {
    return instance_;
  }

  void AbandonCall();
  bool IsAbandoned();
  void UpdateWaitContext(grpc::ClientContext* context);

  // Updates the wait context only if the call is not abandoned.
  // Returns true if the wait context was updated, false if the call was
  // abandoned.
  bool UpdateWaitContextUnlessAbandoned(grpc::ClientContext* context);

 private:
  // This fills the legacy EquipmentPack in equipment for a skill instance from
  // the given resources retrieved by the resource registry.
  absl::Status FillEquipmentPack(
      absl::string_view skill_id,
      const google::protobuf::Map<
          std::string,
          intrinsic_proto::executive::BehaviorCall::ResourceSpecification>&
          resources,
      const CreationContext& context);

  // Fills the instance proto by generating an instance name if not given and
  // retrieving the grpc addresses for this skill instance.
  absl::Status InitializeInstanceProto(
      absl::string_view skill_id,
      std::optional<absl::string_view> instance_name,
      const intrinsic_proto::assets::v1alpha1::InternalSkillData::GrpcTargets&
          grpc_targets,
      absl::string_view id_version);

  std::unique_ptr<skills::SkillServiceClientInterface> client_;
  intrinsic_proto::skills::SkillInstance instance_;

  grpc::ClientContext* absl_nullable wait_context_ ABSL_GUARDED_BY(mutex_) =
      nullptr;
  bool abandoned_ ABSL_GUARDED_BY(mutex_) = false;

  std::unique_ptr<absl::Mutex> mutex_;
};

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_ENGINE_SKILL_INSTANCE_H_
