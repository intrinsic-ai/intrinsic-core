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

#include "intrinsic/perception/skills/capture_images.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/util/message_differencer.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/perception/cameras/camera.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto/v1/camera_settings.pb.h"
#include "intrinsic/perception/proto/v1/capture_data.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "intrinsic/perception/skills/capture_images.pb.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"
#include "intrinsic/skills/cc/execute_context.h"
#include "intrinsic/skills/cc/execute_request.h"
#include "intrinsic/skills/cc/get_footprint_context.h"
#include "intrinsic/skills/cc/get_footprint_request.h"
#include "intrinsic/skills/cc/preview_context.h"
#include "intrinsic/skills/cc/preview_request.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/proto/repeated_field_util.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread_pool.h"
#include "intrinsic/util/time/deadline_timeout.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/transform_node.h"
#include "intrinsic/world/objects/world_object.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic::skills {

namespace {

constexpr int kDefaultNumThreads = 8;
// LINT.IfChange(kv_store)
constexpr std::string_view kDefaultKVStore = "capture_results";
// LINT.ThenChange(//intrinsic/platform/pubsub/fds.yaml:zenoh_router_config)

absl::StatusOr<google::protobuf::RepeatedPtrField<
    intrinsic_proto::perception::v1::CameraSetting>>
ReadCameraSettings(
    const std::vector<std::string>& camera_setting_names,
    const intrinsic_proto::perception::v1::CameraIdentifier& camera_identifier,
    const ConnectionParams& connection_params,
    perception::GrpcCamera& grpc_camera) {
  google::protobuf::RepeatedPtrField<
      intrinsic_proto::perception::v1::CameraSetting>
      camera_settings;
  if (camera_setting_names.empty()) return camera_settings;

  intrinsic_proto::perception::v1::ReadCameraSettingRequest request;
  *request.mutable_camera_identifier() = camera_identifier;
  for (std::string_view name : camera_setting_names) {
    request.set_name(name);
    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::perception::v1::ReadCameraSettingResponse response,
        grpc_camera.Call(&perception::GrpcCamera::Stub::ReadCameraSetting,
                         request, connection_params),
        _.LogError());
    *camera_settings.Add() = std::move(*response.mutable_setting());
  }
  return camera_settings;
}

absl::StatusOr<bool> AllSettingsMatch(
    const google::protobuf::RepeatedPtrField<
        intrinsic_proto::perception::v1::CameraSetting>& camera_settings,
    const intrinsic_proto::perception::v1::CameraIdentifier& camera_identifier,
    const ConnectionParams& connection_params,
    perception::GrpcCamera& grpc_camera) {
  if (camera_settings.empty()) return true;

  bool all_settings_match = true;
  intrinsic_proto::perception::v1::ReadCameraSettingRequest request;
  *request.mutable_camera_identifier() = camera_identifier;
  for (const intrinsic_proto::perception::v1::CameraSetting& camera_setting :
       camera_settings) {
    request.set_name(camera_setting.name());
    INTR_ASSIGN_OR_RETURN(
        const intrinsic_proto::perception::v1::ReadCameraSettingResponse
            response,
        grpc_camera.Call(&perception::GrpcCamera::Stub::ReadCameraSetting,
                         request, connection_params),
        _.LogError());
    all_settings_match &= google::protobuf::util::MessageDifferencer::Equals(
        response.setting(), camera_setting);
    if (!all_settings_match) return false;
  }
  return all_settings_match;
}

absl::StatusOr<intrinsic_proto::perception::v1::CaptureRequest>
CreateCaptureRequest(
    const intrinsic_proto::perception::v1::CameraConfig& camera_config,
    const intrinsic_proto::skills::CaptureImagesParams& params,
    const intrinsic_proto::data_logger::Context& data_logger_context) {
  INTR_ASSIGN_OR_RETURN(
      google::protobuf::Duration timeout,
      FromAbslDuration(connect::kGrpcClientConnectDefaultTimeout));
  if (params.has_timeout()) {
    timeout = params.timeout();
  }

  intrinsic_proto::kvstore::StorageLocation capture_result_location;
  capture_result_location.set_store(
      params.capture_result_location().store().empty()
          ? kDefaultKVStore
          : params.capture_result_location().store());
  capture_result_location.set_key(
      params.capture_result_location().key().empty()
          ? CanonicalString(FromProto(camera_config.identifier()))
          : params.capture_result_location().key());

  intrinsic_proto::perception::v1::CaptureRequest capture_request;
  *capture_request.mutable_camera_config() = camera_config;
  *capture_request.mutable_timeout() = std::move(timeout);
  *capture_request.mutable_sensor_ids() = params.sensor_ids();
  *capture_request.mutable_post_processing_by_sensor_id() =
      params.post_processing_by_sensor_id();
  *capture_request.mutable_capture_result_location() =
      std::move(capture_result_location);

  for (const intrinsic_proto::perception::v1::CameraSetting& camera_setting :
       params.camera_settings_to_append()) {
    *capture_request.mutable_camera_config()->add_camera_settings() =
        camera_setting;
  }

  if (params.log_debug_data()) {
    *capture_request.mutable_context() = data_logger_context;
  }

  return capture_request;
}

absl::StatusOr<intrinsic_proto::perception::v1::CaptureResponse>
CaptureUntilAllSettingsMatch(
    const intrinsic_proto::perception::v1::CameraConfig& camera_config,
    const intrinsic_proto::skills::CaptureImagesParams& params,
    const intrinsic_proto::data_logger::Context& data_logger_context,
    const ConnectionParams& connection_params,
    perception::GrpcCamera& grpc_camera) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::perception::v1::CaptureRequest request,
      CreateCaptureRequest(camera_config, params, data_logger_context));
  INTR_ASSIGN_OR_RETURN(const absl::Duration timeout,
                        ToAbslDuration(request.timeout()));
  const absl::Time deadline = ToDeadline(timeout);

  for (absl::Duration request_timeout = timeout;
       request_timeout > absl::ZeroDuration();
       request_timeout = ToTimeout(deadline)) {
    INTR_ASSIGN_OR_RETURN(*request.mutable_timeout(),
                          FromAbslDuration(request_timeout));
    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::perception::v1::CaptureResponse response,
        grpc_camera.Call(&perception::GrpcCamera::Stub::Capture, request,
                         connection_params),
        _.LogError());
    INTR_ASSIGN_OR_RETURN(const bool all_settings_match,
                          AllSettingsMatch(params.camera_settings_to_wait_for(),
                                           camera_config.identifier(),
                                           connection_params, grpc_camera));
    if (all_settings_match) {
      return response;
    }
  }
  return InternalErrorBuilder()
         << "Settings to wait for did not match after " << timeout << ".";
}

struct CaptureResult {
  intrinsic_proto::kvstore::StorageLocation capture_result_location;
  google::protobuf::RepeatedPtrField<
      intrinsic_proto::perception::v1::CameraSetting>
      camera_settings;
};

absl::StatusOr<CaptureResult> Capture(
    const intrinsic_proto::skills::CaptureImagesParams& params,
    const intrinsic_proto::data_logger::Context& data_logger_context,
    perception::GrpcCamera& grpc_camera) {
  INTR_ASSIGN_OR_RETURN(
      const ConnectionParams config_params,
      skills::GetConnectionParamsFromResolvedDependency(
          params.camera(), perception::CameraConfigServiceInterfaceUri()));
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::perception::v1::CameraConfig camera_config,
      grpc_camera.GetCameraConfig(config_params));
  INTR_ASSIGN_OR_RETURN(
      const ConnectionParams connection_params,
      skills::GetConnectionParamsFromResolvedDependency(
          params.camera(), perception::CameraServiceInterfaceUri()));

  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::perception::v1::CaptureResponse capture_response,
      CaptureUntilAllSettingsMatch(camera_config, params, data_logger_context,
                                   connection_params, grpc_camera));

  INTR_ASSIGN_OR_RETURN(
      google::protobuf::RepeatedPtrField<
          intrinsic_proto::perception::v1::CameraSetting>
          camera_settings,
      ReadCameraSettings(AsVector(params.camera_settings_to_return()),
                         camera_config.identifier(), connection_params,
                         grpc_camera));

  INTR_RET_CHECK(capture_response.has_capture_result_location());
  return CaptureResult{.capture_result_location = std::move(
                           *capture_response.mutable_capture_result_location()),
                       .camera_settings = std::move(camera_settings)};
}

absl::StatusOr<intrinsic_proto::Pose> GetWorldTCamera(
    const intrinsic_proto::assets::v1::ResolvedDependency& camera_dependency,
    const world::ObjectWorldClient& world) {
  INTR_ASSIGN_OR_RETURN(
      const world::WorldObject camera,
      world.GetObject(WorldObjectName(camera_dependency.object().name())));
  INTR_ASSIGN_OR_RETURN(const Pose3d world_t_camera,
                        world.GetTransform(camera));
  return ToProto(world_t_camera);
}

// Obtains the transform from the reference frame to the camera frame in a
// single atomic query. When the camera and the reference frame are rigidly
// attached to a moving robot, querying their relative transform directly
// prevents temporal synchronization issues and joint jitter that occur if
// factoring the transform through the world root at different timestamps.
absl::StatusOr<intrinsic_proto::Pose> GetReferenceTCamera(
    const intrinsic_proto::assets::v1::ResolvedDependency& camera_dependency,
    const intrinsic_proto::world::TransformNodeReference& reference_frame,
    const world::ObjectWorldClient& world) {
  INTR_ASSIGN_OR_RETURN(
      const world::WorldObject camera,
      world.GetObject(WorldObjectName(camera_dependency.object().name())));
  INTR_ASSIGN_OR_RETURN(const world::TransformNode reference_node,
                        world.GetTransformNode(reference_frame));
  INTR_ASSIGN_OR_RETURN(const Pose3d ref_t_camera,
                        world.GetTransform(reference_node, camera));
  return ToProto(ref_t_camera);
}

}  // namespace

CaptureImages::CaptureImages()
    : thread_pool_(/*num_threads=*/kDefaultNumThreads) {}

std::unique_ptr<SkillInterface> CaptureImages::CreateSkill() {
  return std::make_unique<CaptureImages>();
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
CaptureImages::Execute(const ExecuteRequest& request, ExecuteContext& context) {
  INTR_ASSIGN_OR_RETURN(
      const auto params,
      request.params<intrinsic_proto::skills::CaptureImagesParams>());

  absl::StatusOr<CaptureResult> capture_result;
  absl::Notification capture_done;
  const stats::ScopedSpan capture_images_span("CaptureImages::Execute");

  INTR_RETURN_IF_ERROR(thread_pool_.Schedule([&]() {
    const stats::ScopedSpan capture_span("CaptureImages::Capture",
                                         capture_images_span.span());
    capture_result = Capture(
        params, context.logging_context().data_logger_context, grpc_camera_);
    capture_done.Notify();
  }));

  absl::StatusOr<intrinsic_proto::Pose> world_t_camera;
  absl::StatusOr<intrinsic_proto::Pose> reference_t_camera;
  {
    const stats::ScopedSpan get_transform_span("CaptureImages::GetTransforms",
                                               capture_images_span.span());
    world_t_camera = GetWorldTCamera(params.camera(), context.object_world());
    if (params.has_reference_frame()) {
      reference_t_camera = GetReferenceTCamera(
          params.camera(), params.reference_frame(), context.object_world());
    }
  }

  capture_done.WaitForNotification();

  INTR_RETURN_IF_ERROR(capture_result.status());
  INTR_RETURN_IF_ERROR(world_t_camera.status());
  if (params.has_reference_frame()) {
    INTR_RETURN_IF_ERROR(reference_t_camera.status());
  }

  intrinsic_proto::skills::CaptureImagesResult result;
  *result.mutable_capture_data()->mutable_capture_result_location() =
      std::move(capture_result->capture_result_location);
  *result.mutable_capture_data()->mutable_world_t_camera() =
      *std::move(world_t_camera);
  if (params.has_reference_frame()) {
    *result.mutable_capture_data()->mutable_reference_t_camera() =
        *std::move(reference_t_camera);
  }
  *result.mutable_camera_settings() =
      std::move(capture_result->camera_settings);
  return std::make_unique<intrinsic_proto::skills::CaptureImagesResult>(result);
}

absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>
CaptureImages::Preview(const PreviewRequest& request, PreviewContext& context) {
  return std::make_unique<intrinsic_proto::skills::CaptureImagesResult>();
}

absl::StatusOr<intrinsic_proto::skills::Footprint> CaptureImages::GetFootprint(
    const GetFootprintRequest& request, GetFootprintContext& context) const {
  return intrinsic_proto::skills::Footprint();
}

}  // namespace intrinsic::skills
