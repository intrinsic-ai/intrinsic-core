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

#include "intrinsic/perception/skills/calibration/initialize_calibration.h"

#include <memory>
#include <string>

#include "absl/algorithm/container.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/data/proto/v1/data_assets.grpc.pb.h"
#include "intrinsic/assets/data/proto/v1/data_assets.pb.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/skills/util/position_part_util.h"
#include "intrinsic/perception/asset_utils.h"
#include "intrinsic/perception/cameras/camera.h"
#include "intrinsic/perception/pose_estimator_id_utils.h"
#include "intrinsic/perception/proto/v1/calibration_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/calibration_service.pb.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/pattern_detection_config.pb.h"
#include "intrinsic/perception/proto/v1/perception_model.pb.h"
#include "intrinsic/perception/proto/v1/pose_estimation_config.pb.h"
#include "intrinsic/perception/proto/v1/pose_estimator_id.pb.h"
#include "intrinsic/perception/skills/calibration/initialize_calibration.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/util/object_reference_utils.h"

namespace intrinsic {
namespace skills {

namespace {

constexpr absl::string_view kCalibrationInterface =
    "grpc://intrinsic_proto.perception.v1.CalibrationService";

constexpr char kDataAssetsInterface[] =
    "grpc://intrinsic_proto.data.v1.DataAssets";

absl::StatusOr<intrinsic_proto::perception::v1::PatternDetectionConfig>
GetPatternDetectionConfigFromDataAssets(
    const intrinsic_proto::perception::v1::PoseEstimatorId& pose_estimator,
    const intrinsic_proto::assets::v1::ResolvedDependency&
        data_assets_service) {
  intrinsic_proto::assets::Id asset_id;
  asset_id.set_name(pose_estimator.id());
  asset_id.set_package(pose_estimator.package());

  grpc::ClientContext data_assets_context;
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<grpc::Channel> channel,
                        intrinsic::assets::dependencies::Connect(
                            data_assets_service, kDataAssetsInterface,
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  auto data_assets_stub =
      intrinsic_proto::data::v1::DataAssets::NewStub(channel);

  INTR_ASSIGN_OR_RETURN(auto perception_model,
                        perception::GetPerceptionModelFromDataAsset(
                            asset_id, *data_assets_stub, &data_assets_context));

  if (!perception_model.has_pose_estimation_config()) {
    return absl::FailedPreconditionError(
        "PerceptionModel does not have a pose_estimation_config.");
  }
  const auto& pose_estimation_config =
      perception_model.pose_estimation_config();

  if (pose_estimation_config.targets().empty()) {
    return absl::FailedPreconditionError(
        "PoseEstimationConfig (v1) has no targets.");
  }
  const auto& target = pose_estimation_config.targets(0);
  if (!target.has_marker() || !target.marker().has_charuco_pattern()) {
    return absl::FailedPreconditionError(
        "PoseEstimationConfig (v1) target is not a Charuco pattern.");
  }
  intrinsic_proto::perception::v1::PatternDetectionConfig
      pattern_detection_config;
  pattern_detection_config.set_name(pose_estimator.id());
  *pattern_detection_config.mutable_charuco_pattern_detection_config()
       ->mutable_charuco_pattern() = target.marker().charuco_pattern();
  return pattern_detection_config;
}

absl::StatusOr<intrinsic_proto::resources::ResourceHandle>
GetCameraResourceHandle(
    const intrinsic_proto::assets::v1::ResolvedDependency& camera,
    absl::string_view camera_name, const perception::GrpcCamera& grpc_camera) {
  INTR_ASSIGN_OR_RETURN(
      const ConnectionParams config_params,
      skills::GetConnectionParamsFromResolvedDependency(
          camera, perception::CameraConfigServiceInterfaceUri()));
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::perception::v1::CameraConfig camera_config,
      grpc_camera.GetCameraConfig(config_params));
  INTR_ASSIGN_OR_RETURN(const ConnectionParams camera_params,
                        skills::GetConnectionParamsFromResolvedDependency(
                            camera, perception::CameraServiceInterfaceUri()));

  intrinsic_proto::resources::ResourceHandle camera_handle;
  camera_handle.set_name(camera_name);
  camera_handle.mutable_connection_info()->mutable_grpc()->set_address(
      camera_params.address);
  camera_handle.mutable_connection_info()->mutable_grpc()->set_server_instance(
      camera_params.instance_name);
  camera_handle.mutable_connection_info()->mutable_grpc()->set_header(
      camera_params.header);
  (*camera_handle.mutable_resource_data())["CameraConfig"]
      .mutable_contents()
      ->PackFrom(camera_config);
  return camera_handle;
}

absl::StatusOr<google::protobuf::RepeatedPtrField<
    intrinsic_proto::resources::ResourceHandle>>
GetCameraResourceHandles(
    const google::protobuf::RepeatedPtrField<
        intrinsic_proto::assets::v1::ResolvedDependency>& cameras) {
  perception::GrpcCamera grpc_camera;
  google::protobuf::RepeatedPtrField<intrinsic_proto::resources::ResourceHandle>
      camera_handles;
  for (const auto& camera : cameras) {
    const std::string& camera_name = !camera.object().name().empty()
                                         ? camera.object().name()
                                         : camera.name();
    if (absl::c_any_of(camera_handles, [&](const auto& existing) {
          return existing.name() == camera_name;
        })) {
      continue;
    }
    INTR_ASSIGN_OR_RETURN(
        *camera_handles.Add(),
        GetCameraResourceHandle(camera, camera_name, grpc_camera));
  }
  if (camera_handles.empty()) {
    return absl::InvalidArgumentError(
        "At least one camera config is required to initialize a calibration "
        "session.");
  }
  return camera_handles;
}

absl::Status ValidateParams(
    const intrinsic_proto::skills::InitializeCalibrationParams& params) {
  if (params.has_pose_estimator() && params.pose_estimator().id().empty()) {
    return absl::InvalidArgumentError("pose_estimator.id cannot be empty.");
  }
  if (!params.has_pattern_detection_config() && !params.has_pose_estimator()) {
    return absl::InvalidArgumentError(
        "One of pattern_detection_config or pose_estimator has to be "
        "specified.");
  }
  if (params.has_calibration_object() &&
      IsEmptyObjectReference(params.calibration_object())) {
    return absl::InvalidArgumentError("calibration_object cannot be empty.");
  }
  return absl::OkStatus();
}

}  // namespace

std::unique_ptr<SkillInterface> InitializeCalibration::CreateSkill() {
  return std::make_unique<InitializeCalibration>();
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
InitializeCalibration::Execute(const ExecuteRequest& request,
                               ExecuteContext& context) {
  INTR_ASSIGN_OR_RETURN(
      auto params,
      request.params<intrinsic_proto::skills::InitializeCalibrationParams>());
  INTR_RETURN_IF_ERROR(ValidateParams(params));

  const EquipmentPack equipment_pack = context.equipment();
  // Connect to the Calibration service.
  grpc::ClientContext ctx;
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<grpc::Channel> channel,
                        intrinsic::assets::dependencies::Connect(
                            params.calibration_service(), kCalibrationInterface,
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  auto calibration_service_stub =
      intrinsic_proto::perception::v1::CalibrationService::NewStub(channel);

  intrinsic_proto::perception::v1::InitializeRequest initialize_request;

  if (params.has_pattern_detection_config()) {
    *initialize_request.mutable_pattern_detection_config() =
        params.pattern_detection_config();
  } else {
    intrinsic_proto::perception::v1::PoseEstimatorId pose_estimator_id =
        perception::WithDefaultPackageIfUnset(params.pose_estimator());
    INTR_ASSIGN_OR_RETURN(
        *initialize_request.mutable_pattern_detection_config(),
        GetPatternDetectionConfigFromDataAssets(pose_estimator_id,
                                                params.data_assets_service()));
  }
  initialize_request.mutable_pattern_detection_config()
      ->set_publish_annotated_image(true);

  INTR_ASSIGN_OR_RETURN(*initialize_request.mutable_camera_resource_handles(),
                        GetCameraResourceHandles(params.cameras()));

  // Extract the robot arm with respect to which the camera pose will be
  // defined.
  world::ObjectWorldClient& world = context.object_world();
  auto position_part =
      equipment_pack.Unpack<intrinsic_proto::icon::Icon2PositionPart>(
          kRobotEquipmentSlot, icon::kIcon2PositionPartKey);
  if (!position_part.ok()) {
    return absl::FailedPreconditionError(
        absl::StrCat("ICON Equipment at slot ", kRobotEquipmentSlot,
                     " does not have a position part."));
  }
  INTR_ASSIGN_OR_RETURN(
      skills::ArmPartInformation arm_part_info,
      skills::GetArmPartInformation(position_part.value(), world,
                                    params.has_arm_part()
                                        ? std::make_optional(params.arm_part())
                                        : std::nullopt));
  *initialize_request.mutable_arm_part() = arm_part_info.object;

  if (params.has_calibration_object() &&
      !IsEmptyObjectReference(params.calibration_object())) {
    *initialize_request.mutable_calibration_object() =
        params.calibration_object();
  }

  if (!params.belief_world_id().empty()) {
    initialize_request.set_belief_world_id(params.belief_world_id());
  }
  if (!params.edit_world_id().empty()) {
    initialize_request.set_edit_world_id(params.edit_world_id());
  }

  LOG(INFO) << "Initialize calibration service with request: "
            << initialize_request.DebugString();

  intrinsic_proto::perception::v1::InitializeResponse initialize_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(calibration_service_stub->Initialize(
      &ctx, initialize_request, &initialize_response)));

  LOG(INFO) << "Initialized calibration service";

  context.canceller().Ready();
  return std::make_unique<
      intrinsic_proto::skills::InitializeCalibrationResult>();
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>
InitializeCalibration::Preview(const PreviewRequest& request,
                               PreviewContext& context) {
  return std::make_unique<
      intrinsic_proto::skills::InitializeCalibrationResult>();
}

absl::StatusOr<intrinsic_proto::skills::Footprint>
InitializeCalibration::GetFootprint(const GetFootprintRequest& request,
                                    GetFootprintContext& context) const {
  intrinsic_proto::skills::Footprint result;
  result.set_lock_the_universe(false);
  return result;
}

}  // namespace skills
}  // namespace intrinsic
