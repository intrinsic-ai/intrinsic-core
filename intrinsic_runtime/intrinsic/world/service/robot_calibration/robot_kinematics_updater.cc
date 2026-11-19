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

#include "intrinsic/world/service/robot_calibration/robot_kinematics_updater.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/proto/asset_deployment.grpc.pb.h"
#include "intrinsic/assets/proto/asset_deployment.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/control/parts/proto/v1/realtime_part_config.pb.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/proto/limit_provider_service.grpc.pb.h"
#include "intrinsic/icon/proto/limit_provider_service.pb.h"
#include "intrinsic/icon/server/config/icon_main_config.pb.h"
#include "intrinsic/icon/server/config/realtime_control_config.pb.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/resources/proto/resource_registry.pb.h"
#include "intrinsic/resources/proto/runtime_context.pb.h"
#include "intrinsic/scene/config/scene_object_config_updater.h"
#include "intrinsic/scene/proto/v1/object_properties.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_config.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"
#include "intrinsic/scene/util/scene_object_creation.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/service/robot_calibration/robot_calibration_data_service.grpc.pb.h"
#include "intrinsic/world/service/robot_calibration/robot_calibration_data_service.pb.h"
#include "intrinsic/world/service/robot_calibration/robot_update_service.pb.h"

namespace intrinsic::icon {

namespace {

using ::intrinsic_proto::scene_object::v1::SceneObject;
using ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate;
using ::intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdates;
using ::intrinsic_proto::world::ObjectWorldService;

constexpr int kDefaultAssetDeploymentTimeoutSeconds = 60;

bool IconUsesHardwareResource(
    const intrinsic_proto::icon::IconMainConfig& icon_config,
    absl::string_view hardware_resource_name) {
  for (const auto& realtime_part :
       icon_config.realtime_control_config().parts_by_name()) {
    if (realtime_part.second.hardware_resource_name() == hardware_resource_name)
      return true;
  }
  return false;
}

absl::StatusOr<absl::flat_hash_map<
    std::string, intrinsic_proto::scene_object::v1::EntityPoseUpdate>>
GetEntityUpdateMap(
    const intrinsic_proto::world::SceneObjectKinematicsUpdate& kinematics) {
  absl::flat_hash_map<std::string,
                      intrinsic_proto::scene_object::v1::EntityPoseUpdate>
      entity_pose_updates;
  for (const auto& update : kinematics.entity_pose_updates()) {
    if (entity_pose_updates.contains(update.entity_name())) {
      return absl::InvalidArgumentError(
          absl::StrCat("Duplicate entity name: ", update.entity_name()));
    }
    entity_pose_updates[update.entity_name()] = update;
  }
  return entity_pose_updates;
}

absl::StatusOr<bool> RobotChainsAreEqual(
    const intrinsic_proto::world::SceneObjectKinematicsUpdate& a_kinematics,
    const intrinsic_proto::world::SceneObjectKinematicsUpdate& b_kinematics,
    double tolerance = 1e-6) {
  INTR_ASSIGN_OR_RETURN(const auto a_entity_pose_updates,
                        GetEntityUpdateMap(a_kinematics));
  INTR_ASSIGN_OR_RETURN(const auto b_entity_pose_updates,
                        GetEntityUpdateMap(b_kinematics));
  // First we compare the entity pose updates which capture the link poses.
  for (const auto& [b_entity_name, b_entity_update] : b_entity_pose_updates) {
    if (!a_entity_pose_updates.contains(b_entity_name)) {
      INTR_ASSIGN_OR_RETURN(Pose b_pose, intrinsic_proto::FromProto(
                                             b_entity_update.parent_t_this()));
      // If the pose is identity, we can ignore. This allows us to ignore added
      // entities which do not affect the kinematic chain.
      if (b_pose.isApprox(Pose3d::Identity(), tolerance)) {
        LOG(INFO) << "Ignoring entity: " << b_entity_update.entity_name()
                  << " due to identity pose.";
        continue;
      }
      LOG(INFO) << "Entity: " << b_entity_update.entity_name()
                << " with pose: " << b_pose << " not found in 'a_kinematics'.";
      return false;
    }
    INTR_ASSIGN_OR_RETURN(
        Pose a_pose,
        intrinsic_proto::FromProto(
            a_entity_pose_updates.at(b_entity_name).parent_t_this()));
    INTR_ASSIGN_OR_RETURN(Pose b_pose, intrinsic_proto::FromProto(
                                           b_entity_update.parent_t_this()));
    if (!a_pose.isApprox(b_pose, tolerance)) {
      return false;
    }
  }

  // Now we compare the joints
  for (const auto& b_entity_update :
       b_kinematics.update_joints_request().parent_t_inboard()) {
    // Find entity with the same name.
    if (!a_kinematics.update_joints_request().parent_t_inboard().contains(
            b_entity_update.first)) {
      INTR_ASSIGN_OR_RETURN(Pose b_pose,
                            intrinsic_proto::FromProto(b_entity_update.second));
      // If the pose is identity, we can ignore. This allows us to ignore added
      // entities which do not affect the kinematic chain.
      if (b_pose.isApprox(Pose3d::Identity(), tolerance)) {
        LOG(INFO) << "Ignoring entity: " << b_entity_update.first
                  << " due to identity pose.";
        continue;
      }
      LOG(INFO) << "Entity: " << b_entity_update.first
                << " with pose: " << b_pose << " not found in 'a_kinematics'.";
      return false;
    }
    INTR_ASSIGN_OR_RETURN(
        Pose a_pose,
        intrinsic_proto::FromProto(
            a_kinematics.update_joints_request().parent_t_inboard().at(
                b_entity_update.first)));
    INTR_ASSIGN_OR_RETURN(Pose b_pose, intrinsic_proto::FromProtoNormalized(
                                           b_entity_update.second));
    if (!a_pose.isApprox(b_pose, tolerance)) {
      return false;
    }
  }

  return true;
}

}  // namespace

RobotKinematicsUpdater::RobotKinematicsUpdater(
    ObjectWorldService::StubInterface* object_world_stub,
    intrinsic_proto::assets::AssetDeploymentService::StubInterface*
        asset_deployment_service_stub,
    google::longrunning::Operations::StubInterface*
        asset_deployment_operations_stub,
    resources::ResourceRegistryClientInterface* resource_registry_client,
    icon::ChannelFactory* icon_channel_factory)
    : object_world_stub_(object_world_stub),
      asset_deployment_service_stub_(asset_deployment_service_stub),
      asset_deployment_operations_stub_(asset_deployment_operations_stub),
      resource_registry_client_(resource_registry_client),
      icon_channel_factory_(icon_channel_factory) {}

absl::Status RobotKinematicsUpdater::RestartICON(
    absl::string_view resource_id) {
  // Find all ICON resources.
  intrinsic_proto::resources::ListResourceInstanceRequest::StrictFilter
      icon_resource_filter;
  icon_resource_filter.add_capability_names(icon::kIcon2ConnectionKey);
  INTR_ASSIGN_OR_RETURN(
      auto icon_resources,
      resource_registry_client_->ListResources(icon_resource_filter));

  // Trigger a restart on any ICON resource which includes a part which uses the
  // robot resource.
  //
  // TODO(b/336756834): Eventually we would like to remove all ICON restarts.
  // Remove this when ICON can reload its kinematics without restarting.
  for (const auto& icon_instance : icon_resources) {
    intrinsic_proto::icon::IconMainConfig icon_main_config;
    if (!icon_instance.configuration().UnpackTo(&icon_main_config)) {
      return absl::InternalError("Failed to unpack ICON main config.");
    }
    if (!IconUsesHardwareResource(icon_main_config, resource_id)) {
      continue;
    }
    const auto& icon_handle = icon_instance.resource_handle();
    INTR_ASSIGN_OR_RETURN(auto connection_config,
                          skills::GetConnectionParamsFromHandle(icon_handle));
    INTR_ASSIGN_OR_RETURN(
        auto channel,
        icon_channel_factory_->MakeChannel(
            connection_config, connect::kGrpcClientConnectDefaultTimeout));
    icon::Client client(std::move(channel));
    INTR_RETURN_IF_ERROR(client.RestartServer()).LogError()
        << "Failed to restart ICON server";
  }
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<intrinsic::Channel>>
RobotKinematicsUpdater::CreateChannel(
    const ::intrinsic_proto::resources::ResourceHandle& resource_handle) const {
  if (!resource_handle.has_connection_info() ||
      !resource_handle.connection_info().has_grpc()) {
    return absl::NotFoundError(
        "No gRPC connection information was found for the resource.");
  }
  LOG(INFO) << "Resource has connection info: "
            << resource_handle.connection_info();

  return skills::CreateChannelFromHandle(resource_handle);
}

grpc::Status RobotKinematicsUpdater::UpdateRobotKinematics(
    ::grpc::ServerContext* context,
    const ::intrinsic_proto::world::RobotKinematicsUpdateRequest* request,
    ::intrinsic_proto::world::RobotUpdateResponse* response) {
  if (request->resource_id().empty()) {
    return intrinsic::ToGrpcStatus(
        absl::InvalidArgumentError("A resource_id must be provided."));
  }

  if (request->world_id().empty()) {
    return intrinsic::ToGrpcStatus(
        absl::InvalidArgumentError("A world_id must be provided."));
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      auto hardware_module_resource,
      resource_registry_client_->GetResource(request->resource_id()));

  intrinsic_proto::world::SceneObjectKinematicsUpdate
      scene_object_kinematics_update;
  if (request->has_user_specified_update()) {
    scene_object_kinematics_update = request->user_specified_update();
  } else {
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto kinematics_channel,
        CreateChannel(hardware_module_resource.resource_handle()));
    auto kinematics_context = kinematics_channel->GetClientContextFactory()();
    kinematics_context->set_deadline(context->deadline());
    intrinsic_proto::world::RobotCalibrationDataService::Stub kinematics_stub(
        kinematics_channel->GetChannel());
    intrinsic_proto::world::CalibratedKinematicsRequest kinematics_request;
    intrinsic_proto::world::CalibratedKinematicsResponse kinematics_response;
    INTR_RETURN_IF_ERROR_GRPC(kinematics_stub.GetCalibratedKinematics(
        kinematics_context.get(), kinematics_request, &kinematics_response));
    LOG(INFO) << "Received Kinematics: " << kinematics_response;

    if (!kinematics_response.has_scene_object_kinematics_update()) {
      return intrinsic::ToGrpcStatus(
          absl::NotFoundError("No Calibrated kinematic chain was returned."));
    }
    scene_object_kinematics_update =
        std::move(kinematics_response.scene_object_kinematics_update());
  }

  intrinsic_proto::scene_object::v1::SceneObjectConfig scene_object_config =
      hardware_module_resource.scene_object_config();

  std::optional<SceneObjectInstanceUpdate> update_joints_update;
  if (scene_object_config.has_initial_joint_settings()) {
    SceneObjectInstanceUpdate joints_update;
    *joints_update.mutable_update_joints() =
        scene_object_config.initial_joint_settings();
    update_joints_update = joints_update;
  }

  std::optional<SceneObjectInstanceUpdate> set_ik_solvers_update;
  if (scene_object_config.has_initial_ik_solvers()) {
    SceneObjectInstanceUpdate ik_solvers_update;
    *ik_solvers_update.mutable_set_ik_solvers() =
        scene_object_config.initial_ik_solvers();
    set_ik_solvers_update = ik_solvers_update;
  }

  SceneObjectInstanceUpdates filtered_updates;
  for (const SceneObjectInstanceUpdate& update :
       scene_object_config.updates().updates()) {
    if (update.has_update_joints()) {
      if (update_joints_update.has_value()) {
        LOG(WARNING) << "Scene object config has multiple joints updates.";
      } else {
        update_joints_update = update;
      }
    } else if (update.has_set_ik_solvers()) {
      if (set_ik_solvers_update.has_value()) {
        LOG(WARNING) << "Scene object config has multiple ik solver updates.";
      } else {
        set_ik_solvers_update = update;
      }
    } else {
      *filtered_updates.add_updates() = update;
    }
  }
  *scene_object_config.mutable_updates() = filtered_updates;

  SceneObjectInstanceUpdates configuration_updates;

  // Add the joint updates.
  if (!scene_object_kinematics_update.update_joints_request()
           .parent_t_inboard()
           .empty()) {
    if (!update_joints_update.has_value()) {
      update_joints_update = SceneObjectInstanceUpdate();
    }
    *update_joints_update->mutable_update_joints()->mutable_parent_t_inboard() =
        scene_object_kinematics_update.update_joints_request()
            .parent_t_inboard();
    *configuration_updates.add_updates() = *update_joints_update;
  }

  // Add the ik solvers update.
  if (scene_object_kinematics_update.has_set_ik_solvers_update()) {
    if (!set_ik_solvers_update.has_value()) {
      set_ik_solvers_update = SceneObjectInstanceUpdate();
    }
    *set_ik_solvers_update->mutable_set_ik_solvers() =
        scene_object_kinematics_update.set_ik_solvers_update();
    *configuration_updates.add_updates() = *set_ik_solvers_update;
  }

  // Now we add all the entity pose updates.
  for (const auto& update :
       scene_object_kinematics_update.entity_pose_updates()) {
    *configuration_updates.add_updates()->mutable_entity_pose() = update;
  }

  // Apply the updates.
  //
  // TODO: b/486918457 -- Pass the actual scene object here using the installed
  // assets service. Since we're not actually updating anything that needs the
  // scene object config to function, we can pass an empty scene object here.
  // At some point we might want to verify the edits are applicable (e.g. by
  // matching entity names to existing entities), but since we don't do that
  // now we can get away with it.
  INTR_RETURN_IF_ERROR_GRPC(scene_object::ApplyUpdatesToConfig(
      SceneObject(), configuration_updates, &scene_object_config));

  // Update the resource with the new scene object config and restart ICON.
  intrinsic_proto::assets::UpdateResourceRequest update_resource_request;
  update_resource_request.mutable_resource()->set_name(request->resource_id());
  update_resource_request.set_world_id(request->world_id());
  *update_resource_request.mutable_resource()
       ->mutable_configuration()
       ->mutable_scene_object_config() = std::move(scene_object_config);

  google::longrunning::Operation operation;
  INTR_ASSIGN_OR_RETURN_GRPC(
      auto channel, CreateChannel(hardware_module_resource.resource_handle()));
  {
    grpc::ClientContext update_resource_context;
    update_resource_context.set_deadline(context->deadline());
    INTR_RETURN_IF_ERROR_GRPC(asset_deployment_service_stub_->UpdateResource(
        &update_resource_context, update_resource_request, &operation));
  }
  {
    grpc::ClientContext wait_operation_context;
    google::longrunning::WaitOperationRequest wait_request;
    *wait_request.mutable_name() = operation.name();
    wait_request.mutable_timeout()->set_seconds(
        kDefaultAssetDeploymentTimeoutSeconds);
    grpc::Status wait_status = asset_deployment_operations_stub_->WaitOperation(
        &wait_operation_context, wait_request, &operation);
    if (wait_status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
      LOG(WARNING) << "WaitOperation for " << operation.name()
                   << " timed out. Attempting to cancel the operation.";
      grpc::ClientContext cancel_operation_context;
      cancel_operation_context.set_deadline(context->deadline());
      google::longrunning::CancelOperationRequest cancel_request;
      *cancel_request.mutable_name() = operation.name();
      google::protobuf::Empty empty;
      grpc::Status cancel_status =
          asset_deployment_operations_stub_->CancelOperation(
              &cancel_operation_context, cancel_request, &empty);
      if (!cancel_status.ok()) {
        LOG(ERROR) << "Failed to cancel operation " << operation.name() << ": "
                   << ToAbslStatus(cancel_status);
      } else {
        LOG(INFO) << "Successfully initiated cancellation for operation "
                  << operation.name();
      }
      return wait_status;
    }
    INTR_RETURN_IF_ERROR_GRPC(wait_status);
  }
  if (operation.has_error()) {
    return intrinsic::ToGrpcStatus(absl::InternalError(absl::StrCat(
        "Failed to update resource: ", operation.error().message())));
  }
  if (!operation.done()) {
    return intrinsic::ToGrpcStatus(absl::InternalError(
        "Failed to update resource: Operation is not done."));
  }

  INTR_RETURN_IF_ERROR_GRPC(RestartICON(request->resource_id()));
  LOG(INFO) << "Updated robot kinematics for resource: "
            << request->resource_id();
  return grpc::Status::OK;
}

grpc::Status RobotKinematicsUpdater::CheckWorldMatchesHardwareKinematics(
    ::grpc::ServerContext* context,
    const ::intrinsic_proto::world::CheckWorldMatchesHardwareKinematicsRequest*
        request,
    ::intrinsic_proto::world::CheckWorldMatchesHardwareKinematicsResponse*
        response) {
  if (request->resource_id().empty()) {
    return intrinsic::ToGrpcStatus(
        absl::InvalidArgumentError("A resource_id must be provided."));
  }
  if (request->world_id().empty()) {
    return intrinsic::ToGrpcStatus(
        absl::InvalidArgumentError("A world_id must be provided."));
  }
  // 1) First we get the calibrated kinematics from which should implement the
  //  `RobotCalibrationDataService`.
  INTR_ASSIGN_OR_RETURN_GRPC(
      auto hardware_module_resource,
      resource_registry_client_->GetResource(request->resource_id()));
  if (!hardware_module_resource.resource_handle().has_connection_info() ||
      !hardware_module_resource.resource_handle()
           .connection_info()
           .has_grpc()) {
    return intrinsic::ToGrpcStatus(absl::NotFoundError(
        "No gRPC connection information was found for the resource."));
  }
  LOG(INFO) << "Resource has connection info: "
            << hardware_module_resource.resource_handle().connection_info();
  INTR_ASSIGN_OR_RETURN_GRPC(auto kinematics_channel,
                             skills::CreateChannelFromHandle(
                                 hardware_module_resource.resource_handle()));
  auto kinematics_context = kinematics_channel->GetClientContextFactory()();
  kinematics_context->set_deadline(context->deadline());
  intrinsic_proto::world::RobotCalibrationDataService::Stub kinematics_stub(
      kinematics_channel->GetChannel());
  intrinsic_proto::world::CalibratedKinematicsRequest kinematics_request;
  intrinsic_proto::world::CalibratedKinematicsResponse kinematics_response;
  INTR_RETURN_IF_ERROR_GRPC(kinematics_stub.GetCalibratedKinematics(
      kinematics_context.get(), kinematics_request, &kinematics_response));

  if (!kinematics_response.has_scene_object_kinematics_update()) {
    return intrinsic::ToGrpcStatus(
        absl::NotFoundError("No Calibrated kinematic chain was returned."));
  }

  // 2) We need to find the root entity of the robot. We should ignore its pose
  // relative to the parent since this is not part of the kinematic chain.
  intrinsic_proto::world::GetObjectRequest get_object_request;
  get_object_request.set_world_id(request->world_id());
  get_object_request.mutable_object()->mutable_by_name()->set_object_name(
      request->resource_id());
  get_object_request.set_view(intrinsic_proto::world::ObjectView::FULL);
  grpc::ClientContext ctx_get_object;
  ctx_get_object.set_deadline(context->deadline());
  intrinsic_proto::world::Object robot_object;
  INTR_RETURN_IF_ERROR_GRPC(object_world_stub_->GetObject(
      &ctx_get_object, get_object_request, &robot_object));

  WorldHashSet<std::string> frame_names;
  for (const auto& frame : robot_object.frames()) {
    frame_names.insert(frame.name());
  }

  intrinsic_proto::world::SceneObjectKinematicsUpdate
      current_world_kinematics_as_scene_object_update;
  for (const auto& entity : robot_object.entities()) {
    // Ignore the root entity and frame entities. During world creation the
    // sensor frame is renamed and does not necessarily match the sensor entity
    // name (as for standard frames) thus we also check for sensor components.
    if (entity.first == robot_object.root_entity_id() ||
        frame_names.contains(entity.second.name()) ||
        entity.second.has_sensor_component())
      continue;

    if (entity.second.has_kinematics_component()) {
      // Is a joint.
      (*current_world_kinematics_as_scene_object_update
            .mutable_update_joints_request()
            ->mutable_parent_t_inboard())[entity.second.name()] =
          entity.second.kinematics_component().parent_t_inboard();
    } else {
      // Is a link.
      intrinsic_proto::scene_object::v1::EntityPoseUpdate* entity_update =
          current_world_kinematics_as_scene_object_update
              .add_entity_pose_updates();
      entity_update->set_entity_name(entity.second.name());
      *entity_update->mutable_parent_t_this() = entity.second.parent_t_this();
    }
  }

  // 3) Finally we compare the two chains.
  INTR_ASSIGN_OR_RETURN_GRPC(
      bool equal,
      RobotChainsAreEqual(kinematics_response.scene_object_kinematics_update(),
                          current_world_kinematics_as_scene_object_update));
  response->set_world_matches_hardware_kinematics(equal);
  return grpc::Status::OK;
}

absl::StatusOr<intrinsic_proto::icon::GetAllowedLimitsResponse>
ReadlimitsFromHwm(
    grpc::ClientContext& ctx,
    intrinsic_proto::icon::LimitProviderService::Stub& stub,
    const ::intrinsic_proto::world::UpdateRobotLimitsRequest::AutoUpdate&
        auto_update) {
  intrinsic_proto::icon::GetAllowedLimitsRequest request;

  if (auto_update.has_payload_mass_kg()) {
    request.set_payload_mass_kg(auto_update.payload_mass_kg());
  }

  intrinsic_proto::icon::GetAllowedLimitsResponse response;
  INTR_RETURN_IF_ERROR_GRPC(stub.GetAllowedLimits(&ctx, request, &response));

  return response;
}

::grpc::Status RobotKinematicsUpdater::UpdateRobotLimits(
    ::grpc::ServerContext* context,
    const ::intrinsic_proto::world::UpdateRobotLimitsRequest* request,
    ::intrinsic_proto::world::UpdateRobotLimitsResponse* response) {
  if (request->resource_id().empty()) {
    return intrinsic::ToGrpcStatus(
        absl::InvalidArgumentError("A resource_id must be provided."));
  }

  if (request->world_id().empty()) {
    return intrinsic::ToGrpcStatus(
        absl::InvalidArgumentError("A world_id must be provided."));
  }
  INTR_ASSIGN_OR_RETURN_GRPC(
      auto hardware_module_resource,
      resource_registry_client_->GetResource(request->resource_id()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      auto channel, CreateChannel(hardware_module_resource.resource_handle()));

  intrinsic_proto::icon::LimitProviderService::Stub limit_provider_stub(
      channel->GetChannel());

  auto limit_context = channel->GetClientContextFactory()();
  limit_context->set_deadline(context->deadline());

  INTR_ASSIGN_OR_RETURN_GRPC(
      intrinsic_proto::icon::GetAllowedLimitsResponse allowed_limits,
      ReadlimitsFromHwm(*limit_context, limit_provider_stub,
                        request->from_controller()));

  // Use the aliasing constructor to avoid taking ownership of the stub.
  world::ObjectWorldClient world(
      request->world_id(),
      std::shared_ptr<ObjectWorldService::StubInterface>(
          std::shared_ptr<ObjectWorldService::StubInterface>(),
          object_world_stub_));

  // Read the currently active limits from the world and update them with the
  // limits from the HWM.
  INTR_ASSIGN_OR_RETURN_GRPC(
      world::KinematicObject kinematic_object,
      world.GetKinematicObject(WorldObjectName(request->resource_id())));
  INTR_ASSIGN_OR_RETURN_GRPC(
      JointLimits system_limits,
      ToJointLimits(kinematic_object.JointSystemLimits()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      JointLimits application_limits,
      ToJointLimits(kinematic_object.JointApplicationLimits()));

  INTR_ASSIGN_OR_RETURN_GRPC(
      application_limits,
      UpdateJointLimits(application_limits,
                        allowed_limits.application_limits()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      system_limits,
      UpdateJointLimits(system_limits, allowed_limits.system_limits()));

  INTR_ASSIGN_OR_RETURN_GRPC(
      LimitCheckResult result,
      IsWithinLimits(kinematic_object.JointPositions(), application_limits));
  if (!result) {
    return intrinsic::ToGrpcStatus(
        absl::FailedPreconditionError("The current robot joint position is not "
                                      "within the new application limits."));
  }

  // Overwrite the limits in the scene object config with the new limits.
  intrinsic_proto::scene_object::v1::SceneObjectConfig scene_object_config =
      hardware_module_resource.scene_object_config();
  intrinsic_proto::scene_object::v1::SceneObjectInstanceUpdate*
      update_joints_update = nullptr;
  for (auto& update :
       *scene_object_config.mutable_updates()->mutable_updates()) {
    if (update.has_update_joints()) {
      update_joints_update = &update;

      break;
    }
  }
  if (update_joints_update == nullptr) {
    update_joints_update = scene_object_config.mutable_updates()->add_updates();
  }
  // Save the parent_t_inboard map to keep it in the update.
  ::google::protobuf::Map<std::string, intrinsic_proto::Pose> parent_t_inboard =
      update_joints_update->update_joints().parent_t_inboard();
  *update_joints_update->mutable_update_joints() =
      scene_object::ToSceneObjectUpdate(system_limits, application_limits);
  *update_joints_update->mutable_update_joints()->mutable_parent_t_inboard() =
      std::move(parent_t_inboard);

  // Update the resource with the new scene object config and restart ICON.
  intrinsic_proto::assets::UpdateResourceRequest update_resource_request;
  update_resource_request.mutable_resource()->set_name(request->resource_id());
  update_resource_request.set_world_id(request->world_id());
  *update_resource_request.mutable_resource()
       ->mutable_configuration()
       ->mutable_scene_object_config() = std::move(scene_object_config);

  google::longrunning::Operation operation;
  {
    auto update_resource_context = channel->GetClientContextFactory()();
    INTR_RETURN_IF_ERROR_GRPC(asset_deployment_service_stub_->UpdateResource(
        update_resource_context.get(), update_resource_request, &operation));
  }
  {
    auto wait_operation_context = channel->GetClientContextFactory()();
    google::longrunning::WaitOperationRequest wait_request;
    *wait_request.mutable_name() = operation.name();
    wait_request.mutable_timeout()->set_seconds(
        kDefaultAssetDeploymentTimeoutSeconds);
    INTR_RETURN_IF_ERROR_GRPC(asset_deployment_operations_stub_->WaitOperation(
        wait_operation_context.get(), wait_request, &operation));
  }
  if (operation.has_error()) {
    return intrinsic::ToGrpcStatus(absl::InternalError(absl::StrCat(
        "Failed to update resource: ", operation.error().message())));
  }

  INTR_RETURN_IF_ERROR_GRPC(RestartICON(request->resource_id()));
  LOG(INFO) << "Updated robot limits for resource: " << request->resource_id();

  return grpc::Status::OK;
}

}  // namespace intrinsic::icon
