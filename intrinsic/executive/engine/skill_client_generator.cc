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

#include "intrinsic/executive/engine/skill_client_generator.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/assets/proto/asset_type.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/installed_assets.pb.h"
#include "intrinsic/assets/proto/view.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/executive/clips/cc/id_handling.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/readonly_facade.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/engine/skill_action.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/engine/skill_instance.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/resources/proto/resource_registry.pb.h"
#include "intrinsic/skills/internal/skill_registry_client_interface.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread.h"

namespace intrinsic {
namespace executive {
namespace {

const auto kGetSkillsTimeout = absl::Seconds(30);
constexpr int kInstalledAssetsServiceMaxPageSize = 50;

inline constexpr absl::string_view kIngressAddress =
    "istio-ingressgateway.app-ingress.svc.cluster.local:80";
inline constexpr absl::string_view kInstanceHeader = "x-resource-instance-name";

}  // namespace

SkillClientGenerator::SkillClientGenerator(
    skills::SkillRegistryClientInterface* skillreg_client,
    resources::ResourceRegistryClientInterface* resourcereg_client,
    std::optional<absl::Duration> skill_service_connect_timeout,
    intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface*
        installed_assets,
    std::shared_ptr<
        intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface>
        asset_info_internal,
    clips::Environment* environment, clips::ProtobufManager* proto_manager)
    : skillreg_client_(skillreg_client),
      resourcereg_client_(resourcereg_client),
      skill_service_connect_timeout_(skill_service_connect_timeout),
      installed_assets_(installed_assets),
      asset_info_internal_(std::move(asset_info_internal)),
      environment_(environment),
      proto_manager_(proto_manager),
      resolver_(kIngressAddress, kInstanceHeader) {}

absl::Status SkillClientGenerator::Init(
    clips::EnvironmentFunctionFacade* function_facade,
    clips::EnvironmentReadonlyFacade* readonly_facade) {
  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "skill-get-behavior-tree-proto",
      std::function([this](const std::string& skill_id) -> clips::Values {
        INTR_ASSIGN_OR_RETURN(intrinsic_proto::executive::BehaviorTree tree,
                              GetAndCacheBehaviorTree(skill_id),
                              _.LogError().With([](const absl::Status& status) {
                                return clips::Values{
                                    clips::Symbol::False(),
                                    clips::Value(status.message())};
                              }));
        clips::ProtoMessageId tree_id = proto_manager_->AddGeneratedProto(tree);
        return clips::Values{clips::Symbol::True(),
                             clips::Value(tree_id.value())};
      })));

  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
SkillClientGenerator::GetProcessAsset(intrinsic_proto::assets::Id process_id) {
  intrinsic_proto::assets::v1::GetInstalledAssetRequest request;
  *request.mutable_id() = process_id;

  // The full view includes the deployment data of the process asset
  // which includes the full behavior tree proto (including its skill
  // proto).
  request.set_view(
      intrinsic_proto::catalog::AssetViewType::ASSET_VIEW_TYPE_FULL);

  grpc::ClientContext context;
  ConfigureClientContext(&context);

  intrinsic_proto::assets::v1::InstalledAsset asset;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      installed_assets_->GetInstalledAsset(&context, request, &asset)));

  // Sanity check the received asset.
  if (!asset.deployment_data().process().process().has_behavior_tree()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Asset '%s.%s' is not a process asset or does not have "
                        "a behavior tree.",
                        process_id.package(), process_id.name()));
  }
  intrinsic_proto::executive::BehaviorTree& bt =
      *asset.mutable_deployment_data()
           ->mutable_process()
           ->mutable_process()
           ->mutable_behavior_tree();
  INTR_RETURN_IF_ERROR(EnsureUniqueElementIds(bt));
  return std::move(bt);
}

SkillClientGenerator::SkillRegistrationInternal
SkillClientGenerator::ProcessSkillProtoToSkillRegistration(
    const intrinsic_proto::skills::Skill& skill_proto,
    std::optional<intrinsic_proto::executive::BehaviorTree> behavior_tree,
    SkillRegistrationInternal::SkillSource source) {
  std::string process_type = "Process asset";
  if (source == SkillRegistrationInternal::SkillSource::SKILL_REGISTRY) {
    process_type = "legacy PBT";
  }

  std::vector<std::string> required_equipment_keys;
  for (const auto& [key, _] : skill_proto.resource_selectors()) {
    required_equipment_keys.push_back(key);
  }

  SkillRegistrationInternal skill{
      .id = skill_proto.id(),
      .id_version = skill_proto.id_version(),
      .is_process = true,
      .parameter_message_full_name =
          skill_proto.parameter_description().parameter_message_full_name(),
      .return_value_message_full_name = skill_proto.return_value_description()
                                            .return_value_message_full_name(),
      .behavior_tree = std::move(behavior_tree),
      .required_equipment_keys = std::move(required_equipment_keys),
      .source = source,
  };
  if (skill_proto.parameter_description().has_parameter_descriptor_fileset()) {
    skill.parameter_file_descriptor_set =
        skill_proto.parameter_description().parameter_descriptor_fileset();
  } else if (!skill.parameter_message_full_name.empty()) {
    LOG(ERROR) << "Skill proto in " << process_type << " '" << skill_proto.id()
               << "' defines a parameter message: '"
               << skill.parameter_message_full_name
               << "', but the parameter descriptor set is empty.";
  }
  if (skill_proto.return_value_description().has_descriptor_fileset()) {
    skill.return_value_file_descriptor_set =
        skill_proto.return_value_description().descriptor_fileset();
  } else if (!skill.return_value_message_full_name.empty()) {
    LOG(ERROR) << "Skill proto in " << process_type << " '" << skill_proto.id()
               << "' defines a return value message: '"
               << skill.return_value_message_full_name
               << "', but the return value descriptor set is empty.";
  }
  return skill;
}

absl::StatusOr<std::vector<SkillClientGenerator::SkillRegistrationInternal>>
SkillClientGenerator::ListAllProcessAssets() {
  intrinsic_proto::assets::v1::ListInstalledAssetsRequest request;
  request.set_page_size(kInstalledAssetsServiceMaxPageSize);
  request.mutable_strict_filter()->add_asset_types(
      intrinsic_proto::assets::AssetType::ASSET_TYPE_PROCESS);
  // The full view includes the deployment data of the process asset which
  // includes the full behavior tree proto (including its skill proto). We
  // cannot get the skill proto without also fetching the full BT proto.
  request.set_view(
      intrinsic_proto::catalog::AssetViewType::ASSET_VIEW_TYPE_FULL);

  std::vector<SkillRegistrationInternal> skills;
  do {
    // ClientContext instances may not be reused across rpcs!
    grpc::ClientContext context;
    ConfigureClientContext(&context);

    intrinsic_proto::assets::v1::ListInstalledAssetsResponse response;
    INTR_RETURN_IF_ERROR(ToAbslStatus(installed_assets_->ListInstalledAssets(
                             &context, request, &response)))
            .LogError()
            .SetPrepend()
        << "Failed to list process assets: ";

    for (const intrinsic_proto::assets::v1::InstalledAsset& asset :
         response.installed_assets()) {
      // Sanity check the received asset.
      if (!asset.deployment_data().process().process().has_behavior_tree()) {
        return absl::InternalError(
            "Received a Process asset without a behavior tree.");
      }
      const intrinsic_proto::executive::BehaviorTree& behavior_tree =
          asset.deployment_data().process().process().behavior_tree();

      if (!behavior_tree.description().has_behavior_tree_description()) {
        continue;
      }

      intrinsic_proto::skills::Skill skill_proto = behavior_tree.description();

      // Save the full BT so we don't have to fetch it again later.
      skills.push_back(ProcessSkillProtoToSkillRegistration(
          skill_proto, std::move(behavior_tree),
          SkillRegistrationInternal::SkillSource::INSTALLED_ASSETS_SERVICE));
    }

    request.set_page_token(response.next_page_token());
  } while (!request.page_token().empty());

  return skills;
}

absl::StatusOr<std::vector<SkillClientGenerator::SkillRegistrationInternal>>
SkillClientGenerator::ListAllSkillAssets() {
  intrinsic_proto::assets::v1::ListInstalledAssetsRequest request;
  request.set_page_size(kInstalledAssetsServiceMaxPageSize);
  request.mutable_strict_filter()->add_asset_types(
      intrinsic_proto::assets::AssetType::ASSET_TYPE_SKILL);
  request.set_view(
      intrinsic_proto::catalog::AssetViewType::ASSET_VIEW_TYPE_ALL_METADATA);

  std::vector<SkillRegistrationInternal> skills;
  do {
    // ClientContext instances may not be reused across rpcs!
    grpc::ClientContext context;
    ConfigureClientContext(&context);

    intrinsic_proto::assets::v1::ListInstalledAssetsResponse response;
    INTR_RETURN_IF_ERROR(ToAbslStatus(installed_assets_->ListInstalledAssets(
                             &context, request, &response)))
            .LogError()
            .SetPrepend()
        << "Failed to list skill assets: ";

    for (const intrinsic_proto::assets::v1::InstalledAsset& asset :
         response.installed_assets()) {
      // Sanity check the received asset.
      if (!asset.skill_specific_metadata().has_details()) {
        return absl::InternalError(
            "Received a Skill asset without skill details.");
      }
      const intrinsic_proto::skills::SkillDetails& details =
          asset.skill_specific_metadata().details();

      INTR_ASSIGN_OR_RETURN(
          std::string skill_id,
          assets::IdFromProto(asset.metadata().id_version().id()));
      INTR_ASSIGN_OR_RETURN(
          std::string skill_id_version,
          assets::IdVersionFromProto(asset.metadata().id_version()));

      std::vector<std::string> required_equipment_keys;
      for (const auto& [key, _] : details.dependencies().required_equipment()) {
        required_equipment_keys.push_back(key);
      }

      SkillRegistrationInternal skill{
          .id = skill_id,
          .id_version = skill_id_version,
          .is_process = false,
          .parameter_message_full_name =
              details.parameter().message_full_name(),
          .return_value_message_full_name =
              details.execute_result().message_full_name(),
          .required_equipment_keys = std::move(required_equipment_keys),
          .source =
              SkillRegistrationInternal::SkillSource::INSTALLED_ASSETS_SERVICE,
      };

      if (asset.metadata().file_descriptor_set().file().empty() &&
          (!skill.parameter_message_full_name.empty() ||
           !skill.return_value_message_full_name.empty())) {
        LOG(ERROR) << absl::StrFormat(
            "Skill asset '%s' has a parameter and/or return value message "
            "('%s'/'%s') but the file descriptor set is unset or empty.",
            skill_id, skill.parameter_message_full_name,
            skill.return_value_message_full_name);
      }

      if (!skill.parameter_message_full_name.empty()) {
        skill.parameter_file_descriptor_set =
            asset.metadata().file_descriptor_set();
      }
      if (!skill.return_value_message_full_name.empty()) {
        skill.return_value_file_descriptor_set =
            asset.metadata().file_descriptor_set();
      }
      skills.push_back(std::move(skill));
    }

    request.set_page_token(response.next_page_token());
  } while (!request.page_token().empty());

  return skills;
}

absl::StatusOr<std::vector<SkillClientGenerator::SkillRegistrationInternal>>
SkillClientGenerator::GetLegacyPbtsFromSkillRegistry() {
  INTR_ASSIGN_OR_RETURN(
      std::vector<intrinsic_proto::skills::Skill> skill_protos,
      skillreg_client_->GetSkills(kGetSkillsTimeout),
      _.LogError().SetPrepend()
          << "Failed to fetch skill infos from skill registry: ");

  std::vector<SkillRegistrationInternal> skills;
  for (const intrinsic_proto::skills::Skill& skill_proto : skill_protos) {
    if (!skill_proto.has_behavior_tree_description()) {
      continue;
    }

    // We have not yet fetched the entire behavior tree. Store std::nullopt now
    // and fetch it later on-demand.
    skills.push_back(ProcessSkillProtoToSkillRegistration(
        skill_proto, /*behavior_tree=*/std::nullopt,
        SkillRegistrationInternal::SkillSource::SKILL_REGISTRY));
  }
  return skills;
}

absl::Status SkillClientGenerator::UpdateGrpcTargets(
    std::vector<SkillClientGenerator::SkillRegistrationInternal>&
        skill_assets) {
  if (skill_assets.empty()) {
    return absl::OkStatus();
  }
  if (asset_info_internal_ == nullptr) {
    return absl::FailedPreconditionError(
        "Cannot fetch internal skill data without AssetInfoInternal stub");
  }

  intrinsic_proto::assets::v1alpha1::BatchGetInternalSkillDataRequest request;
  for (const SkillRegistrationInternal& skill_asset : skill_assets) {
    request.add_ids(skill_asset.id);
  }
  grpc::ClientContext context;
  ConfigureClientContext(&context);
  intrinsic_proto::assets::v1alpha1::BatchGetInternalSkillDataResponse response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(asset_info_internal_->BatchGetInternalSkillData(
          &context, request, &response)))
          .LogError()
          .SetPrepend()
      << "Failed to batch get internal skill data: ";

  absl::flat_hash_map<std::string,
                      intrinsic_proto::assets::v1alpha1::InternalSkillData>
      internal_data_map;
  for (const intrinsic_proto::assets::v1alpha1::InternalSkillData& data :
       response.internal_skill_data()) {
    INTR_ASSIGN_OR_RETURN(std::string skill_id, intrinsic::assets::IdFromProto(
                                                    data.id_version().id()));
    internal_data_map[skill_id] = data;
  }

  for (SkillRegistrationInternal& skill_asset : skill_assets) {
    auto it = internal_data_map.find(skill_asset.id);
    if (it == internal_data_map.end()) {
      return absl::NotFoundError(absl::StrFormat(
          "Internal skill data not found for skill: %s", skill_asset.id));
    }
    const intrinsic_proto::assets::v1alpha1::InternalSkillData& data =
        it->second;
    if (!data.has_grpc_targets()) {
      return absl::NotFoundError(absl::StrFormat(
          "GRPC targets missing for skill: %s", skill_asset.id));
    }
    if (data.grpc_targets().execute().empty() ||
        data.grpc_targets().project().empty()) {
      return absl::NotFoundError(absl::StrFormat(
          "GRPC targets incomplete for skill: %s", skill_asset.id));
    }
    skill_asset.grpc_targets = data.grpc_targets();
  }
  return absl::OkStatus();
}

absl::Status SkillClientGenerator::GetAndCacheSkills() {
  if (skillreg_client_ == nullptr) {
    return absl::FailedPreconditionError(
        "Cannot fetch skills without skill registry client");
  }
  if (installed_assets_ == nullptr) {
    return absl::FailedPreconditionError(
        "Cannot fetch installed assets without installed assets stub");
  }

  // For Skill and Process assets we need two separate ListInstalledAssets
  // calls. There is no clever way to combine those into a single RPC (yet).
  INTR_ASSIGN_OR_RETURN(
      std::vector<SkillClientGenerator::SkillRegistrationInternal>
          process_assets,
      ListAllProcessAssets());
  INTR_ASSIGN_OR_RETURN(
      std::vector<SkillClientGenerator::SkillRegistrationInternal> skill_assets,
      ListAllSkillAssets());

  INTR_RETURN_IF_ERROR(UpdateGrpcTargets(skill_assets)).LogError();

  INTR_ASSIGN_OR_RETURN(
      std::vector<SkillClientGenerator::SkillRegistrationInternal> legacy_pbts,
      GetLegacyPbtsFromSkillRegistry());

  // Combine all skill registrations into the 'skills_' cache. We expect all IDs
  // to be unique and, thus, the insertion order should not matter.
  skills_.clear();
  for (const SkillClientGenerator::SkillRegistrationInternal& process_asset :
       process_assets) {
    skills_[process_asset.id] = std::move(process_asset);
  }

  for (const SkillClientGenerator::SkillRegistrationInternal& skill_asset :
       skill_assets) {
    skills_[skill_asset.id] = std::move(skill_asset);
  }

  for (const SkillClientGenerator::SkillRegistrationInternal& legacy_pbt :
       legacy_pbts) {
    skills_[legacy_pbt.id] = std::move(legacy_pbt);
  }

  return absl::OkStatus();
}

absl::Status SkillClientGenerator::GetAndCacheResources() {
  if (resourcereg_client_ == nullptr) {
    return absl::FailedPreconditionError(
        "Cannot fetch resources without resource registry client");
  }
  INTR_ASSIGN_OR_RETURN(
      std::vector<intrinsic_proto::resources::ResourceInstance> instances,
      resourcereg_client_->ListResources({}),
      _.LogError().SetPrepend() << "Failed to fetch resources: ");
  resource_handles_.clear();
  for (const intrinsic_proto::resources::ResourceInstance& instance :
       instances) {
    if (instance.has_resource_handle()) {
      const auto& handle = instance.resource_handle();
      resource_handles_[handle.name()] = handle;
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::executive::BehaviorTree>
SkillClientGenerator::GetAndCacheBehaviorTree(absl::string_view skill_id) {
  absl::MutexLock lock(skills_mutex_);
  auto skill_entry = skills_.find(skill_id);
  if (skill_entry == skills_.end()) {
    return absl::NotFoundError(absl::StrFormat(
        "Could not find skill entry for behavior tree with id '%s'", skill_id));
  }
  SkillRegistrationInternal& skill_registration = skill_entry->second;

  if (!skill_registration.is_process) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "Behavior tree requested for non-process '%s'", skill_id));
  }

  if (skill_registration.behavior_tree.has_value()) {

    if (!skill_registration.behavior_tree_element_ids_ensured) {
      INTR_RETURN_IF_ERROR(
          EnsureUniqueElementIds(*skill_registration.behavior_tree));
      skill_registration.behavior_tree_element_ids_ensured = true;
    }

    // Return saved behavior tree from Process asset we have retrieved earlier
    // when listing Process assets.
    return *skill_registration.behavior_tree;
  }

  // Handle behavior trees of legacy PBTs from the skill registry.
  //
  // Use a local cache (skill_id_to_behavior_tree_) to avoid repeated requests.
  // Mainly we'd like to avoid retrieving the same tree multiple times during
  // one import call but, since we check the version, we can keep the cache
  // between runs.

  auto behavior_tree_entry = skill_id_to_behavior_tree_.find(skill_id);
  if (behavior_tree_entry != skill_id_to_behavior_tree_.end()) {
    if (skill_registration.id_version ==
        behavior_tree_entry->second.description().id_version()) {
      // Current version is in the cache, return it.
      return behavior_tree_entry->second;
    }
  }

  // Cache miss, perform RPC to skill registry.
  if (skillreg_client_ == nullptr) {
    return absl::FailedPreconditionError(
        "Cannot fetch behavior tree without skill registry client");
  }

  INTR_ASSIGN_OR_RETURN(intrinsic_proto::executive::BehaviorTree legacy_pbt,
                        skillreg_client_->GetBehaviorTree(skill_id),
                        _.LogError());


  INTR_RETURN_IF_ERROR(EnsureUniqueElementIds(legacy_pbt));

  skill_id_to_behavior_tree_[skill_id] = legacy_pbt;
  return legacy_pbt;
}

absl::StatusOr<SkillClientGenerator::SkillRegistration>
SkillClientGenerator::GetSkillRegistration(absl::string_view skill_id) const
    ABSL_LOCKS_EXCLUDED(skills_mutex_) {
  absl::MutexLock lock(skills_mutex_);
  auto skill_entry = skills_.find(skill_id);
  if (skill_entry == skills_.end()) {
    return absl::NotFoundError(absl::StrFormat(
        "Skill '%s' is not present in current skills", skill_id));
  }
  // Return a public version of SkillRegistrationInternal which contains only
  // required properties. Avoid copying everything.
  return SkillRegistration{
      .is_process = skill_entry->second.is_process,
      .parameter_message_full_name =
          skill_entry->second.parameter_message_full_name,
  };
}

absl::StatusOr<SkillInstance> SkillClientGenerator::CreateSkillInstance(
    intrinsic_proto::executive::BehaviorCall& behavior_call,
    const clips::DescriptorPoolId descriptor_pool_id,
    SkillAction skill_action) {
  const stats::ScopedSpan span("SkillClientGenerator/CreateSkillInstance");
  absl::MutexLock lock(skills_mutex_);
  // Check whether skill is known
  if (!skills_.contains(behavior_call.skill_id())) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Skill '%s' unknown", behavior_call.skill_id()));
  }

  const SkillRegistrationInternal& skill_reg =
      skills_.at(behavior_call.skill_id());
  if (!skill_reg.grpc_targets.has_value()) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "Skill '%s' did not have connection information available.",
        behavior_call.skill_id()));
  }

  std::optional<clips::ProtobufManager::DescriptorPoolInfo> pool_info;
  if (behavior_call.has_parameters()) {
    INTR_ASSIGN_OR_RETURN(
        pool_info, proto_manager_->GetDescriptorPool(descriptor_pool_id));
  }
  return SkillInstance::Create(
      behavior_call, skill_action,
      {.pool_info = pool_info,
       .resolver = resolver_,
       .grpc_targets = *skill_reg.grpc_targets,
       .resource_handles = resource_handles_,
       .connect_timeout = skill_service_connect_timeout_,
       .skill_service_client_creator_mock = skill_service_client_creator_mock_,
       .required_equipment_keys = skill_reg.required_equipment_keys,
       .id_version = skill_reg.id_version});
}

absl::Status SkillClientGenerator::ClearSkillOperations(
    const SkillRegistrationInternal& skill_registration) {
  intrinsic_proto::skills::SkillInstance instance_proto;
  // no instance name as we connect to the skill for all operations
  instance_proto.set_id_version(skill_registration.id_version);
  instance_proto.mutable_project_handle()->set_grpc_target(
      skill_registration.grpc_targets->project());
  instance_proto.mutable_execute_handle()->set_grpc_target(
      skill_registration.grpc_targets->execute());

  std::optional<absl::Duration> clear_operations_timeout;
  if (skill_service_connect_timeout_.has_value()) {
    clear_operations_timeout =
        intrinsic::connect::kGrpcClientConnectDefaultTimeout;
  }
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<skills::SkillServiceClientInterface> client,
      CreateSkillServiceClient(instance_proto, clear_operations_timeout,
                               skill_service_client_creator_mock_));

  return client->ClearOperations();
}

absl::Status SkillClientGenerator::ResetSkillInstances()
    ABSL_LOCKS_EXCLUDED(skills_mutex_) {
  const stats::ScopedSpan span("SkillClientGenerator/ResetSkillInstances");
  LOG(INFO) << "Resetting SkillInstances";
  absl::MutexLock lock(skills_mutex_);
  std::vector<absl::Status> errors;
  for (const auto& [skill_id, skill_registration] : skills_) {
    if (skill_registration.is_process) {
      continue;
    }
    if (!skill_registration.grpc_targets.has_value()) {
      errors.push_back(absl::FailedPreconditionError(
          absl::StrFormat("Skill '%s' did not have connection information "
                          "available - cannot reset instances.",
                          skill_id)));
      continue;
    }
    absl::Status status = ClearSkillOperations(skill_registration);
    if (!status.ok()) {
      errors.push_back(status);
    }
  };
  LOG(INFO) << "Reset SkillInstances Done";

  if (!errors.empty()) {
    std::vector<std::string> error_messages;
    error_messages.reserve(errors.size());
    for (const absl::Status& error : errors) {
      error_messages.push_back(std::string(error.message()));
    }
    return absl::Status(errors.front().code(),
                        absl::StrCat("Failed to reset some skill instances: [",
                                     absl::StrJoin(error_messages, "; "), "]"));
  }

  return absl::OkStatus();
}

absl::StatusOr<std::string>
SkillClientGenerator::SkillRegistrationInternal::GetTypeUrlPrefix() const {
  return GenerateIntrinsicTypeUrl(source == SkillSource::SKILL_REGISTRY
                                      ? kIntrinsicTypeUrlAreaSkills
                                      : kIntrinsicTypeUrlAreaAssets,
                                  id);
}

absl::Status SkillClientGenerator::AddSkillInfo(
    const SkillRegistrationInternal& skill, absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex(), &skills_mutex_) {
  clips::Symbol skill_type = clips::Symbol("SKILL");
  if (skill.is_process) {
    skill_type = clips::Symbol("BEHAVIOR-TREE");
  }

  LOG(INFO) << "Adding skill-info for " << skill.id
            << " at id_version: " << skill.id_version;
  INTR_ASSIGN_OR_RETURN(std::string type_url_prefix, skill.GetTypeUrlPrefix());

  clips::DescriptorPoolId parameter_pool_id =
      proto_manager_->kInvalidDescriptorPoolId;
  if (skill.parameter_file_descriptor_set.has_value()) {
    INTR_ASSIGN_OR_RETURN(parameter_pool_id,
                          proto_manager_->AddDescriptorPool(
                              *skill.parameter_file_descriptor_set,
                              absl::StrFormat("'%s' Parameters", skill.id),
                              type_url_prefix, operation_name));
  }
  clips::DescriptorPoolId return_value_pool_id =
      proto_manager_->kInvalidDescriptorPoolId;
  if (skill.return_value_file_descriptor_set.has_value()) {
    INTR_ASSIGN_OR_RETURN(return_value_pool_id,
                          proto_manager_->AddDescriptorPool(
                              *skill.return_value_file_descriptor_set,
                              absl::StrFormat("'%s' Return Value", skill.id),
                              type_url_prefix, operation_name));
  }
  return environment_
      ->AssertFact(
          "skill-info",
          {{"skill-id", skill.id},
           {"operation-name", operation_name},
           {"skill-type", skill_type},
           {"parameter-descriptor-pool-id", parameter_pool_id.value()},
           {"return-value-descriptor-pool-id", return_value_pool_id.value()},
           {"parameter-message-name", skill.parameter_message_full_name},
           {"return-value-message-name", skill.return_value_message_full_name}})
      .status();
}

absl::Status SkillClientGenerator::RemoveSkillInfoContents(
    const clips::Fact& skill_fact) {
  INTR_ASSIGN_OR_RETURN(
      clips::Value param_pool,
      skill_fact.GetSlotValue("parameter-descriptor-pool-id"));
  INTR_ASSIGN_OR_RETURN(int64_t param_pool_id, param_pool.GetInteger());
  if (clips::DescriptorPoolId(param_pool_id) !=
      clips::ProtobufManager::kInvalidDescriptorPoolId) {
    proto_manager_->RemoveDescriptorPool(
        clips::DescriptorPoolId(param_pool_id));
  }
  INTR_ASSIGN_OR_RETURN(
      clips::Value return_value_pool,
      skill_fact.GetSlotValue("return-value-descriptor-pool-id"));
  INTR_ASSIGN_OR_RETURN(int64_t return_value_pool_id,
                        return_value_pool.GetInteger());
  if (clips::DescriptorPoolId(return_value_pool_id) !=
      clips::ProtobufManager::kInvalidDescriptorPoolId) {
    proto_manager_->RemoveDescriptorPool(
        clips::DescriptorPoolId(return_value_pool_id));
  }

  return absl::OkStatus();
}

absl::Status SkillClientGenerator::UpdateSkillFacts(
    absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex(), &skills_mutex_) {
  LOG(INFO) << "Adding skill infos";
  // For now just delete all the contents of all skill info facts for all
  // operations.
  // If necessary, this could be optimized to record pools by id_version and
  // possibly just "moving" pools for the same id_version skill to AddSkillInfo.
  // TODO(b/319835803) Move this to remove specific skill-info facts when
  // deleting an old operation.
  std::vector<clips::Fact> skill_facts =
      environment_->QueryFacts("skill-info", {});
  for (const clips::Fact& skill_fact : skill_facts) {
    INTR_RETURN_IF_ERROR(RemoveSkillInfoContents(skill_fact));
  }
  INTR_RETURN_IF_ERROR(environment_->RetractFacts("skill-info", {}));

  for (const auto& [skill_id, skill_registration] : skills_) {
    INTR_RETURN_IF_ERROR(AddSkillInfo(skill_registration, operation_name));
  }
  return absl::OkStatus();
}

absl::Status SkillClientGenerator::UpdateSkillsAndResources(
    absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex()) {
  //  Parallelize calls to skill and resource registry, finishes when both have
  //  completed.
  absl::MutexLock lock(skills_mutex_);
  absl::Mutex status_mutex;
  absl::Status get_status = absl::OkStatus();
  std::vector<Thread> workers;

  auto task = [this, &status_mutex, &get_status]() {
    skills_mutex_.AssertHeld();

    if (auto status = GetAndCacheSkills(); !status.ok()) {
      absl::MutexLock lock(status_mutex);
      get_status.Update(status);
      return;
    }
  };
  workers.emplace_back(task);
  workers.emplace_back([this, &status_mutex, &get_status]() {
    if (auto task_status = GetAndCacheResources(); !task_status.ok()) {
      absl::MutexLock lock(status_mutex);
      get_status.Update(task_status);
    }
  });
  for (auto& worker : workers) {
    worker.join();
  }

  INTR_RETURN_IF_ERROR(get_status);

  return UpdateSkillFacts(operation_name);
}

}  // namespace executive
}  // namespace intrinsic
