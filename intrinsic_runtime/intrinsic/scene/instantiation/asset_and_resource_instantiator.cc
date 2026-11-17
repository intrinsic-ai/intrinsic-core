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

#include "intrinsic/scene/instantiation/asset_and_resource_instantiator.h"

#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "google/longrunning/operations.pb.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/assets/proto/asset_deployment.pb.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/installed_assets.pb.h"
#include "intrinsic/assets/scene_objects/proto/scene_object_manifest.pb.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/imported_scene_updates.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_import.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "re2/re2.h"

namespace intrinsic {

using ::google::longrunning::Operations;
using ::intrinsic_proto::assets::AssetDeploymentService;
using ::intrinsic_proto::assets::CreateResourceFromCatalogRequest;
using ::intrinsic_proto::assets::CreateResourceFromCatalogResponse;
using ::intrinsic_proto::assets::IdVersion;
using ::intrinsic_proto::assets::v1::CreateInstalledAssetsRequest;
using ::intrinsic_proto::assets::v1::CreateInstalledAssetsResponse;
using ::intrinsic_proto::assets::v1::InstalledAssets;
using ::intrinsic_proto::assets::v1::UpdatePolicy;
using ::intrinsic_proto::scene_object::v1::ImportedSceneObjectInstances;
using ::intrinsic_proto::scene_object::v1::ImportedSceneObjects;
using ::intrinsic_proto::scene_object::v1::ImportedSceneUpdates;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneRequest;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneResponse;
using ::intrinsic_proto::scene_object::v1::InstantiateImportedSceneStatus;
using ::intrinsic_proto::scene_object::v1::ReparentSceneObjectInstance;
using ::intrinsic_proto::scene_object::v1::SceneObject;
using ::intrinsic_proto::scene_object::v1::SceneObjectConfig;
using ::intrinsic_proto::scene_objects::ProcessedSceneObjectManifest;
using ::intrinsic_proto::scene_objects::SceneObjectMetadata;

namespace {

// TODO(b/334132459): Move this to id_utils.h.
// Sanitize input string `s` to satisfy the following conditions
// 1. Only lower case letters, number and '_' are in the string/
// 2. The first character is alphabetic.
// 3. No consecutive '_' characters.
absl::StatusOr<std::string> GetSanitizedName(absl::string_view s) {
  std::string my_string(s);
  if (my_string.empty()) {
    my_string = "a";
  }
  absl::AsciiStrToLower(&my_string);
  RE2::GlobalReplace(&my_string, "[^0-9A-Za-z_]", "_");
  RE2::GlobalReplace(&my_string, "_+", "_");
  if (absl::ascii_isdigit(*my_string.begin()) || *my_string.begin() == '_') {
    my_string = absl::StrCat("a", my_string);
  }
  if (my_string.ends_with('_')) {
    absl::StrAppend(&my_string, "z");
  }
  INTR_RETURN_IF_ERROR(assets::ValidateName(my_string)) << absl::Substitute(
      "Failed to sanitize original name '$0', attempt '$1' failed "
      "Id Name validation.",
      s, my_string);
  return my_string;
}

// Creates a unique name based on `new_name` without colliding with
// `existing_names`, by appending numbers starting from _1. For example if
// new_name is "finger" and `existing_names` contains {"finger", "finger_1"},
// "finger_2" will be returned.
absl::StatusOr<std::string> GetUniqueName(
    absl::string_view new_name,
    const absl::flat_hash_set<std::string>& existing_names) {
  std::string deduplicated_name(new_name);
  int suffix = 0;
  while (existing_names.contains(deduplicated_name)) {
    ++suffix;
    deduplicated_name = absl::StrCat(new_name, "_", suffix);
  }
  return deduplicated_name;
}

absl::StatusOr<ProcessedSceneObjectManifest> CreateSideloadedResourceManifest(
    absl::string_view so_id, const SceneObject& so,
    const SceneObjectMetadata& metadata_template,
    const SceneObjectConfig& config_template) {
  ProcessedSceneObjectManifest manifest;
  *manifest.mutable_assets()->mutable_scene_object_model() = so;
  *manifest.mutable_metadata() = metadata_template;
  *manifest.mutable_assets()->mutable_default_scene_object_config() =
      config_template;

  // Initialize an empty file descriptor set, then populate it based on user
  // data.
  manifest.mutable_assets()->mutable_file_descriptor_set();
  for (const auto& [_, any] : so.user_data()) {
    std::string type_name = any.type_url();
    size_t slash = type_name.find_last_of('/');
    if (slash != std::string::npos) {
      type_name = type_name.substr(slash + 1);
    }
    const google::protobuf::Descriptor* descriptor =
        google::protobuf::DescriptorPool::generated_pool()
            ->FindMessageTypeByName(type_name);
    if (descriptor == nullptr) {
      return absl::NotFoundError(
          absl::StrCat("Descriptor not found for type: ", type_name));
    }
    ::intrinsic::MergeFileDescriptorSet(
        *descriptor, *manifest.mutable_assets()->mutable_file_descriptor_set());
  }

  if (manifest.metadata().display_name() != so.name()) {
    *manifest.mutable_metadata()->mutable_display_name() =
        absl::StrCat(manifest.metadata().display_name(), "_", so.name());
  }
  // TODO(b/334133867): Consider validate so_id earlier in the system so this
  // GetSanitizedName provides less discrepancy from the original id.

  // CAD importer should have returned alphanumeric
  // and _ name. We need to strip the "-" in generated uuids and use lower case
  // for name to satisfy the id requirements of intrinsic_proto.assets.Id.name.
  std::string id_name = manifest.metadata().id().name();
  if (so_id != id_name) {
    absl::StrAppend(&id_name, "_", so_id);
  }
  INTR_ASSIGN_OR_RETURN(
      *manifest.mutable_metadata()->mutable_id()->mutable_name(),
      GetSanitizedName(id_name),
      _ << absl::Substitute(
          "Failed to get sanitized name for resource metadata id name "
          "from metadata template id name '$0' and scene object id '$1'",
          manifest.metadata().id().name(), so_id));

  return manifest;
}

// Installs `scene_objects` as sideloaded resources. Each installation uses the
// given `metadata_template` as the basis of the manifest. Installs with
// `installer_service`. Records successful installation in `status`.
absl::StatusOr<absl::flat_hash_map<std::string, IdVersion>> InstallSceneObjects(
    const ImportedSceneObjects& scene_objects,
    const SceneObjectMetadata& metadata_template,
    const SceneObjectConfig& config_template,
    InstalledAssets::StubInterface& installed_assets_service,
    Operations::StubInterface& installed_assets_operations_service,
    const acl::User& identity, InstantiateImportedSceneStatus& status) {
  intrinsic::stats::ScopedSpan span("InstallSceneObjects");
  absl::flat_hash_map<std::string, IdVersion> so_id_to_installed_asset;
  if (scene_objects.objects().empty()) {
    return absl::InvalidArgumentError("No scene objects to install.");
  }

  CreateInstalledAssetsRequest install_request;
  install_request.set_policy(UpdatePolicy::UPDATE_POLICY_ADD_NEW_ONLY);

  // We use the asset id name to map installed response back to so_id.
  absl::flat_hash_map<std::string, std::string> asset_id_name_to_so_id;

  for (const auto& [so_id, so] : scene_objects.objects()) {
    auto& asset = *install_request.mutable_assets()->Add();

    INTR_ASSIGN_OR_RETURN(*asset.mutable_scene_object(),
                          CreateSideloadedResourceManifest(
                              so_id, so, metadata_template, config_template));

    asset_id_name_to_so_id[asset.scene_object().metadata().id().name()] = so_id;
  }

  // Create installed assets and wait for the operation to finish.
  auto context = identity.NewClientContext();
  google::longrunning::Operation lro;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(installed_assets_service.CreateInstalledAssets(
          context.get(), install_request, &lro)));

  while (!lro.done()) {
    google::longrunning::WaitOperationRequest wait_request;
    *wait_request.mutable_name() = lro.name();
    auto context = identity.NewClientContext();
    INTR_RETURN_IF_ERROR(
        ToAbslStatus(installed_assets_operations_service.WaitOperation(
            context.get(), wait_request, &lro)));
  }

  if (lro.has_error()) {
    return absl::InternalError(absl::Substitute(
        "Failed to install scene object. Error: $0", lro.error().message()));
  }
  CreateInstalledAssetsResponse install_response;
  if (!lro.response().UnpackTo(&install_response)) {
    return absl::InternalError(
        absl::Substitute("Failed to unpack CreateInstalledAssetsResponse from "
                         "long running operation: $0",
                         lro.name()));
  }

  for (const auto& installed_asset : install_response.installed_assets()) {
    const auto& asset_id_name =
        installed_asset.metadata().id_version().id().name();
    if (!asset_id_name_to_so_id.contains(asset_id_name)) {
      return absl::InternalError(
          absl::Substitute("Installed asset id name '$0' not found in "
                           "asset_id_name_to_so_id map.",
                           asset_id_name));
    }
    const auto& so_id = asset_id_name_to_so_id.at(asset_id_name);
    (*status.mutable_installed_assets()
          ->mutable_scene_object_id_to_installed_asset())[so_id] =
        installed_asset.metadata().id_version().id();
    so_id_to_installed_asset[so_id] = installed_asset.metadata().id_version();
  }

  return so_id_to_installed_asset;
}

// Instantiates scene object instances as resource installations.
absl::Status InstantiateSceneObjectInstances(
    const ImportedSceneObjectInstances& scene_object_instances,
    const ImportedSceneUpdates& instance_updates,
    absl::string_view display_name, absl::string_view world_id,
    absl::flat_hash_set<std::string> existing_resource_instance_names,
    absl::flat_hash_map<std::string, IdVersion> so_id_to_installed_asset,
    AssetDeploymentService::StubInterface& asset_deployment_service,
    Operations::StubInterface& asset_deployment_operations_service,
    const acl::User& identity, InstantiateImportedSceneStatus& status) {
  intrinsic::stats::ScopedSpan span("InstantiateSceneObjectInstances");
  INTR_ASSIGN_OR_RETURN(
      std::vector<AssetAndResourceInstantiator::InstanceWithReparentUpdate>
          sorted_instances,
      AssetAndResourceInstantiator::
          GetSortedSceneObjectInstancesWithReparentUpdate(
              scene_object_instances, instance_updates));

  for (const auto& soi_with_reparent : sorted_instances) {
    const auto& soi_id = soi_with_reparent.instance_id;
    intrinsic::stats::ScopedSpan soi_span(
        absl::StrCat("InstantiateSceneObjectInstance/", soi_id));
    const auto& soi = scene_object_instances.instances().at(soi_id);
    if (!so_id_to_installed_asset.contains(soi.scene_object_id())) {
      return absl::NotFoundError(absl::Substitute(
          "Cannot install scene object instance $0, a installed "
          "resource for scene object $1 not found",
          soi, soi.scene_object_id()));
    }
    CreateResourceFromCatalogRequest create_resource_request;
    create_resource_request.set_world_id(world_id);
    google::longrunning::Operation operation;

    INTR_ASSIGN_OR_RETURN(
        *create_resource_request.mutable_type_id_version(),
        assets::IdVersionFromProto(
            so_id_to_installed_asset.at(soi.scene_object_id())));
    // TODO(b/334133867): Consider validate soi_id earlier in the system so this
    // GetSanitizedName provides less discrepancy from the original id.

    INTR_ASSIGN_OR_RETURN(
        const std::string sanitized_name, GetSanitizedName(soi_id),
        _ << absl::Substitute("Failed to get sanitized name for resource "
                              "instance id from scene object instance id '$0'",
                              soi_id));
    INTR_ASSIGN_OR_RETURN(
        *create_resource_request.mutable_configuration()->mutable_name(),
        GetUniqueName(sanitized_name, existing_resource_instance_names));

    // If there is a reparent update we want to specify that with
    // create_resource_request.
    if (soi_with_reparent.reparent.has_value()) {
      const auto& reparent = soi_with_reparent.reparent;
      const auto& parent_soi_id =
          reparent->parent_entity().instance_ref().instance_id();
      const auto& created_instances =
          status.created_instances()
              .scene_object_instance_id_to_created_resource_instance();
      if (!created_instances.contains(parent_soi_id)) {
        return absl::InternalError(
            absl::Substitute("Parent scene object instance id $0 not found in "
                             "created scene object instances.",
                             parent_soi_id));
      }
      *create_resource_request.mutable_configuration()
           ->mutable_parent()
           ->mutable_reference()
           ->mutable_by_name()
           ->mutable_object_name() = created_instances.at(parent_soi_id).name();

      if (reparent->has_set_relative_pose()) {
        *create_resource_request.mutable_configuration()
             ->mutable_parent_t_this() =
            reparent->set_relative_pose().new_pose();
      }
    }
    auto grpc_context = identity.NewClientContext();
    INTR_RETURN_IF_ERROR(
        ToAbslStatus(asset_deployment_service.CreateResourceFromCatalog(
            grpc_context.get(), create_resource_request, &operation)));

    // Wait for the operation to finish and record the created resource
    // instance.
    while (!operation.done()) {
      google::longrunning::WaitOperationRequest wait_request;
      *wait_request.mutable_name() = operation.name();
      auto grpc_context = identity.NewClientContext();
      INTR_RETURN_IF_ERROR(
          ToAbslStatus(asset_deployment_operations_service.WaitOperation(
              grpc_context.get(), wait_request, &operation)));
    }
    if (operation.has_error()) {
      auto error =
          intrinsic::InternalErrorBuilder() << absl::Substitute(
              "Asset deployment operation failed for imported scene object "
              "instance '$0'",
              soi_id);
      error.AttachExtendedStatus(
          "ai.intrinsic.scene_object_import", 10000,
          {
              .title = "Failed to deploy imported scene object instance as "
                       "resource instance",
              .debug_message =
                  absl::Substitute("Asset Deployment operation failed for "
                                   "request $0 with error: $1",
                                   create_resource_request, operation.error()),
          });
      return error;
    }
    CreateResourceFromCatalogResponse response;
    if (!operation.response().UnpackTo(&response)) {
      return absl::InternalError(
          "Failed to unpack CreateResourceFromCatalogResponse.");
    }

    existing_resource_instance_names.insert(response.name());
    (*status.mutable_created_instances()
          ->mutable_scene_object_instance_id_to_created_resource_instance())
        [soi_id] = std::move(response);
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<
    std::vector<AssetAndResourceInstantiator::InstanceWithReparentUpdate>>
AssetAndResourceInstantiator::GetSortedSceneObjectInstancesWithReparentUpdate(
    const intrinsic_proto::scene_object::v1::ImportedSceneObjectInstances&
        scene_object_instances,
    const intrinsic_proto::scene_object::v1::ImportedSceneUpdates&
        instance_updates) {
  // Gather parent children relationships first.
  absl::flat_hash_map<std::string, absl::flat_hash_set<std::string>>
      parent_to_children;
  absl::flat_hash_map<std::string, ReparentSceneObjectInstance>
      child_to_reparent;
  for (const auto& update : instance_updates.updates()) {
    if (!update.has_reparent()) continue;
    const auto& parent =
        update.reparent().parent_entity().instance_ref().instance_id();
    const auto& child = update.reparent().child_object().instance_id();
    if (!scene_object_instances.instances().contains(parent)) {
      return absl::InvalidArgumentError(
          absl::Substitute("Parent $0 not found.", parent));
    }
    if (!scene_object_instances.instances().contains(child)) {
      return absl::InvalidArgumentError(
          absl::Substitute("Child $0 not found.", child));
    }
    if (!parent_to_children.contains(parent)) {
      parent_to_children[parent] = {};
    }
    parent_to_children.at(parent).insert(child);
    if (child_to_reparent.contains(child)) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Child $0 has multiple reparent updates. It is already reparented to "
          "$1, cannot also reparent to $2.",
          child,
          child_to_reparent.at(child)
              .parent_entity()
              .instance_ref()
              .instance_id(),
          parent));
    }
    child_to_reparent[child] = update.reparent();
  }

  // Do a BFS on the parentage relationship.
  std::vector<InstanceWithReparentUpdate> sorted_soi_ids;
  sorted_soi_ids.reserve(scene_object_instances.instances().size());

  // Gather all instances without parent first. We can create them safely at the
  // beginning.
  std::list<std::string> queue;
  for (const auto& [instance_id, _] : scene_object_instances.instances()) {
    if (child_to_reparent.contains(instance_id)) continue;
    queue.push_back(instance_id);
  }

  // Traverse the parent_to_children relationship.
  while (!queue.empty()) {
    const std::string& front = queue.front();
    if (parent_to_children.contains(front)) {
      for (const auto& child : parent_to_children.at(front)) {
        queue.push_back(child);
      }
    }
    std::optional<ReparentSceneObjectInstance> reparent = std::nullopt;
    if (child_to_reparent.contains(front)) {
      reparent = child_to_reparent.at(front);
    }
    sorted_soi_ids.push_back(InstanceWithReparentUpdate{
        .instance_id = front,
        .reparent = std::move(reparent),
    });
    queue.pop_front();
  }

  if (sorted_soi_ids.size() != scene_object_instances.instances().size()) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Only $0 instances gathered from $1 original instance "
        "map, the missing elements could form their own parentage loop(s)",
        sorted_soi_ids.size(), scene_object_instances.instances().size()));
  }

  return sorted_soi_ids;
}

absl::StatusOr<std::unique_ptr<AssetAndResourceInstantiator>>
AssetAndResourceInstantiator::Create(
    absl_nonnull std::shared_ptr<AssetDeploymentService::StubInterface>
        asset_deployment_service_stub,
    absl_nonnull std::shared_ptr<Operations::StubInterface>
        asset_deployment_operations_stub,
    absl_nonnull std::shared_ptr<InstalledAssets::StubInterface>
        installed_assets_service_stub,
    absl_nonnull std::shared_ptr<Operations::StubInterface>
        installed_assets_operations_stub) {
  INTR_RET_CHECK_NE(asset_deployment_service_stub, nullptr);
  INTR_RET_CHECK_NE(asset_deployment_operations_stub, nullptr);
  INTR_RET_CHECK_NE(installed_assets_service_stub, nullptr);
  INTR_RET_CHECK_NE(installed_assets_operations_stub, nullptr);
  return absl::WrapUnique(new AssetAndResourceInstantiator(
      std::move(asset_deployment_service_stub),
      std::move(asset_deployment_operations_stub),
      std::move(installed_assets_service_stub),
      std::move(installed_assets_operations_stub)));
}

AssetAndResourceInstantiator::AssetAndResourceInstantiator(
    std::shared_ptr<AssetDeploymentService::StubInterface>
        asset_deployment_service_stub,
    std::shared_ptr<Operations::StubInterface> asset_deployment_operations_stub,
    std::shared_ptr<InstalledAssets::StubInterface>
        installed_assets_service_stub,
    std::shared_ptr<Operations::StubInterface> installed_assets_operations_stub)
    : asset_deployment_service_stub_(
          ABSL_DIE_IF_NULL(asset_deployment_service_stub)),
      asset_deployment_operations_stub_(
          ABSL_DIE_IF_NULL(asset_deployment_operations_stub)),
      installed_assets_service_stub_(
          ABSL_DIE_IF_NULL(installed_assets_service_stub)),
      installed_assets_operations_stub_(
          ABSL_DIE_IF_NULL(installed_assets_operations_stub)) {}

absl::StatusOr<InstantiateImportedSceneResponse>
AssetAndResourceInstantiator::InstantiateImportedScene(
    const InstantiateImportedSceneRequest& request, const acl::User& identity,
    std::shared_ptr<InstantiateImportedSceneStatus> status) {
  intrinsic::stats::ScopedSpan span(
      "AssetAndResourceInstantiator/InstantiateImportedScene");
  INTR_ASSIGN_OR_RETURN(
      auto so_id_to_installed_asset,
      InstallSceneObjects(
          request.imported_scene().scene_objects(), request.config().metadata(),
          request.config().scene_object_config(),
          *installed_assets_service_stub_, *installed_assets_operations_stub_,
          identity, *status));
  LOG(INFO) << "Finished side loading assets for imported scene.";

  absl::flat_hash_set<std::string> existing_resource_instance_names{
      request.config().existing_resource_instance_names().begin(),
      request.config().existing_resource_instance_names().end()};

  INTR_RETURN_IF_ERROR(InstantiateSceneObjectInstances(
      request.imported_scene().scene_object_instances(),
      request.imported_scene().instance_updates(),
      request.config().metadata().display_name(), request.world_id(),
      std::move(existing_resource_instance_names),
      std::move(so_id_to_installed_asset), *asset_deployment_service_stub_,
      *asset_deployment_operations_stub_, identity, *status));

  LOG(INFO) << "Finished creating instances for imported scene.";
  InstantiateImportedSceneResponse response;
  *response.mutable_installed_assets() = status->installed_assets();
  *response.mutable_created_instances() = status->created_instances();
  return response;
}

}  // namespace intrinsic
