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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_SKILL_CLIENT_GENERATOR_H_
#define INTRINSIC_EXECUTIVE_ENGINE_SKILL_CLIENT_GENERATOR_H_

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
#include "intrinsic/assets/dependencies/resolver.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/v1alpha1/asset_info_internal.grpc.pb.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/readonly_facade.h"
#include "intrinsic/executive/engine/skill_action.h"
#include "intrinsic/executive/engine/skill_instance.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/internal/skill_registry_client_interface.h"
#include "intrinsic/skills/proto/skills.pb.h"

namespace intrinsic {
namespace executive {

// Validates skills with the registry and creates 'SkillInstance' objects that
// contain a resolved 'SkillInstance' and a 'SkillServiceClientInterface'.
class SkillClientGenerator {
 public:
  // Cached data for a callable skill or process (this is a partial version
  // containing only publicly required properties).
  struct SkillRegistration {
    bool is_process;
    std::string parameter_message_full_name;
  };

  SkillClientGenerator(
      skills::SkillRegistryClientInterface* skillreg_client,
      resources::ResourceRegistryClientInterface* resourcereg_client,
      std::optional<absl::Duration> skill_service_connect_timeout,
      intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface*
          installed_assets,
      std::shared_ptr<
          intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface>
          asset_info_internal,
      clips::Environment* environment, clips::ProtobufManager* proto_manager);

  // Set a mock creator for testing. If set, this will be used instead of
  // creating a real skill service client.
  void SetSkillServiceClientCreatorForTest(
      SkillServiceClientCreator skill_service_client_creator_mock) {
    skill_service_client_creator_mock_ =
        std::move(skill_service_client_creator_mock);
  }

  // Register functions in CLIPS to expose updating and adding skill-infos.
  absl::Status Init(clips::EnvironmentFunctionFacade* function_facade,
                    clips::EnvironmentReadonlyFacade* readonly_facade)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(function_facade->clips_mutex(),
                                    readonly_facade->GetClipsMutex());

  absl::StatusOr<SkillInstance> CreateSkillInstance(
      intrinsic_proto::executive::BehaviorCall& behavior_call,
      clips::DescriptorPoolId descriptor_pool_id, SkillAction skill_action)
      ABSL_LOCKS_EXCLUDED(skills_mutex_);

  absl::Status ResetSkillInstances() ABSL_LOCKS_EXCLUDED(skills_mutex_);

  absl::Status UpdateSkillsAndResources(absl::string_view operation_name)
      ABSL_LOCKS_EXCLUDED(skills_mutex_);

  // Retrieves a cached skill registration. This only retrieves protos that
  // have already been cached, e.g., by UpdateSkillsAndResources, but does not
  // perform any retrieval.
  absl::StatusOr<SkillRegistration> GetSkillRegistration(
      absl::string_view skill_id) const ABSL_LOCKS_EXCLUDED(skills_mutex_);

  // Get the BehaviorTree for the Process with the given skill ID.
  absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
  GetAndCacheBehaviorTree(absl::string_view skill_id)
      ABSL_LOCKS_EXCLUDED(skills_mutex_);

  // Retrieve a process asset with process_id if it exists.
  absl::StatusOr<intrinsic_proto::executive::BehaviorTree> GetProcessAsset(
      intrinsic_proto::assets::Id process_id);

 private:
  // Cached data for a callable skill or process (internal version).
  struct SkillRegistrationInternal {
    // Skill metadata
    std::string id;
    std::string id_version;
    bool is_process;
    std::string parameter_message_full_name;
    std::string return_value_message_full_name;
    std::optional<google::protobuf::FileDescriptorSet>
        parameter_file_descriptor_set;
    std::optional<google::protobuf::FileDescriptorSet>
        return_value_file_descriptor_set;

    // Behavior tree (if this registration represents a process AND the behavior
    // tree has already been fetched). Populated from the installed assets
    // service when listing process assets.

    // Unique element ids are enforced on first access via
    // GetAndCacheBehaviorTree (see behavior_tree_element_ids_ensured below).

    std::optional<intrinsic_proto::executive::BehaviorTree> behavior_tree;

    // Whether missing element ids have been generated (in place) and all
    // element ids are validated. Avoids re-validating the cached tree on every
    // GetAndCacheBehaviorTree().
    bool behavior_tree_element_ids_ensured = false;


    // Legacy equipment keys. The skill's manifest dictates the required ground
    // truth. Because skills can mix legacy and new proto dependencies, behavior
    // call resources may not be fully populated. We use behavior call resources
    // if the dependency is not yet migrated or to pull fallback values.
    // Otherwise, the dependency is pulled from the skill's parameter proto.
    std::vector<std::string> required_equipment_keys;

    // Other relevant infos about the skill
    enum class SkillSource {
      SKILL_REGISTRY,
      INSTALLED_ASSETS_SERVICE,
    };

    SkillSource source;
    std::optional<
        intrinsic_proto::assets::v1alpha1::InternalSkillData::GrpcTargets>
        grpc_targets;

    absl::StatusOr<std::string> GetTypeUrlPrefix() const;
  };

  absl::Status AddSkillInfo(const SkillRegistrationInternal& skill_registration,
                            absl::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex(), &skills_mutex_);

  absl::Status RemoveSkillInfoContents(const clips::Fact& skill_fact);
  absl::Status UpdateSkillFacts(absl::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex(), &skills_mutex_);

  static SkillRegistrationInternal ProcessSkillProtoToSkillRegistration(
      const intrinsic_proto::skills::Skill& skill_proto,
      std::optional<intrinsic_proto::executive::BehaviorTree> behavior_tree,
      SkillRegistrationInternal::SkillSource source);

  absl::StatusOr<std::vector<SkillRegistrationInternal>> ListAllProcessAssets();
  absl::StatusOr<std::vector<SkillRegistrationInternal>> ListAllSkillAssets();
  absl::StatusOr<std::vector<SkillRegistrationInternal>>
  GetLegacyPbtsFromSkillRegistry();
  absl::Status UpdateGrpcTargets(
      std::vector<SkillClientGenerator::SkillRegistrationInternal>&
          skill_assets);

  absl::Status ResolveSkillParameterDependencies(
      google::protobuf::Any& parameter,
      clips::DescriptorPoolId descriptor_pool_id, SkillAction skill_action,
      const absl::flat_hash_map<std::string, std::string>&
          fallback_manifest_dependencies) const;

  absl::StatusOr<bool> HasAnnotatedResolvedDependency(
      const google::protobuf::Any& parameter,
      clips::DescriptorPoolId descriptor_pool_id) const;

  // Update skills_ from the installed assets service and the skill registry.
  // Only call from UpdateSkillsAndResources to ensure the information is
  // reflected in CLIPS.
  absl::Status GetAndCacheSkills() ABSL_EXCLUSIVE_LOCKS_REQUIRED(skills_mutex_);

  absl::Status GetAndCacheResources();

  absl::Status ClearSkillOperations(
      const SkillRegistrationInternal& skill_registration);

  skills::SkillRegistryClientInterface* skillreg_client_;  // externally owned.
  resources::ResourceRegistryClientInterface*
      resourcereg_client_;  // externally owned.
  // Timeout for the skill service client connection. Used in production.
  // If set, a real skill service client will be created.
  // Mutually exclusive with skill_service_client_creator_mock_ during
  // execution.
  std::optional<absl::Duration> skill_service_connect_timeout_;
  // Mock creator for skill service clients, used for testing.
  // If set, this takes precedence over skill_service_connect_timeout_.
  SkillServiceClientCreator skill_service_client_creator_mock_;
  intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface*
      installed_assets_;  // externally owned.
  std::shared_ptr<
      intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface>
      asset_info_internal_;
  mutable absl::Mutex skills_mutex_;
  absl::flat_hash_map<std::string, SkillRegistrationInternal> skills_
      ABSL_GUARDED_BY(skills_mutex_);
  absl::flat_hash_map<std::string, intrinsic_proto::resources::ResourceHandle>
      resource_handles_;

  clips::Environment* environment_;        // externally owned.
  clips::ProtobufManager* proto_manager_;  // externally owned.

  // Behavior tree cache for legacy PBTs from the skill registry only.
  // Behavior trees from Process assets are fetched eagerly and are directly
  // stored in SkillRegistrationInternal.
  absl::flat_hash_map<std::string, intrinsic_proto::executive::BehaviorTree>
      skill_id_to_behavior_tree_;

  // Used to resolver skill parameters before skill execution, preview, etc.
  intrinsic::assets::Resolver resolver_;
};

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_SKILL_CLIENT_GENERATOR_H_
