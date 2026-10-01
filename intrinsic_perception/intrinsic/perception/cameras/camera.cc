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

#include "intrinsic/perception/cameras/camera.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "grpcpp/support/client_callback.h"
#include "intrinsic/assets/interface_utils.h"
#include "intrinsic/icon/release/grpc_time_support.h"
#include "intrinsic/perception/cameras/camera_factory.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_lister.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/remote_image_source.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_config_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_config.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace perception {

namespace {
constexpr char kCameraConfigEquipmentKey[] = "CameraConfig";

}  // namespace

absl::string_view CameraServiceInterfaceUri() {
  static const absl::NoDestructor<std::string> kUri(absl::StrCat(
      intrinsic::assets::kGrpcUriPrefix,
      intrinsic_proto::perception::v1::CameraService::service_full_name()));
  return *kUri;
}

absl::string_view CameraConfigServiceInterfaceUri() {
  static const absl::NoDestructor<std::string> kUri(
      absl::StrCat(intrinsic::assets::kGrpcUriPrefix,
                   intrinsic_proto::perception::v1::CameraConfigService::
                       service_full_name()));
  return *kUri;
}

absl::StatusOr<intrinsic_proto::perception::v1::CameraConfig>
UnpackCameraConfig(const google::protobuf::Any& any) {
  if (intrinsic_proto::perception::v1::CameraConfig camera_config;
      any.UnpackTo(&camera_config)) {
    return camera_config;
  }

  return intrinsic::InvalidArgumentErrorBuilder()
         << "Unpacking camera config failed. Are you using "
            "intrinsic_proto::perception::v1::CameraConfig?";
}

absl::StatusOr<intrinsic_proto::perception::v1::CameraConfig>
GetCameraConfigFromHandle(
    const intrinsic_proto::resources::ResourceHandle& handle) {
  const auto it = handle.resource_data().find(kCameraConfigEquipmentKey);
  if (it == handle.resource_data().end()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Missing required equipment parameter: ", kCameraConfigEquipmentKey,
        " in resource handle: ", handle.name()));
  }
  return UnpackCameraConfig(it->second.contents());
}

absl::StatusOr<Camera> Camera::Create(
    const CameraIdentifier& camera_identifier,
    std::optional<std::string> camera_server_address, absl::Duration timeout,
    std::optional<std::string> camera_server_instance) {
  if (camera_server_address.has_value()) {
    // Just return the remote camera. No additional configuration is required
    // since the returned camera is already configured on the server side.
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<ImageSourceInterface> image_source_interface,
        RemoteImageSource::Create(camera_identifier,
                                  camera_server_address.value(), timeout,
                                  camera_server_instance));
    return Camera(std::move(image_source_interface));
  }
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<ImageSourceInterface> image_source_interface,
      CreateImageSource(camera_identifier));
  return Camera(std::move(image_source_interface));
}

absl::StatusOr<Camera> Camera::CreateFromHandle(
    const intrinsic_proto::resources::ResourceHandle& handle,
    absl::Duration timeout) {
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::perception::v1::CameraConfig camera_config,
      GetCameraConfigFromHandle(handle));
  INTR_ASSIGN_OR_RETURN(ConnectionParams connection_params,
                        skills::GetConnectionParamsFromHandle(handle));
  return Create(FromProto(camera_config.identifier()),
                connection_params.address, timeout,
                connection_params.instance_name.empty()
                    ? std::nullopt
                    : std::make_optional(connection_params.instance_name));
}

absl::StatusOr<std::vector<CameraIdentifier>> Camera::ListAvailableCameras(
    std::optional<std::string> camera_server_address, absl::Duration timeout,
    std::optional<std::string> camera_server_instance) {
  if (camera_server_address.has_value()) {
    return RemoteImageSource::ListAvailableCameras(
        camera_server_address.value(), timeout, camera_server_instance);
  }
  // We have to fully qualify here as we are mapping the static member
  // function to an internal implementation of ListAvailableCameras().
  return intrinsic::perception::ListAvailableCameras();
}

Camera::Camera(std::unique_ptr<ImageSourceInterface> image_source_interface)
    : image_source_interface_(std::move(image_source_interface)) {}

absl::StatusOr<DescribeCameraResponse> Camera::DescribeCamera() const {
  DescribeCameraResponse response;
  INTR_ASSIGN_OR_RETURN(response.sensors,
                        image_source_interface_->DescribeCameraSensors());
  return response;
}

absl::StatusOr<CaptureResult> Camera::Capture(const CaptureArgs& args) const {
  return image_source_interface_->Capture(args);
}

absl::StatusOr<CameraSettingAccess> Camera::ReadCameraSettingAccess(
    absl::string_view name) const {
  return image_source_interface_->ReadCameraSettingAccess(name);
}

absl::StatusOr<CameraSettingProperties> Camera::ReadCameraSettingProperties(
    absl::string_view name) const {
  return image_source_interface_->ReadCameraSettingProperties(name);
}

absl::StatusOr<CameraSetting> Camera::ReadCameraSetting(
    absl::string_view name) const {
  return image_source_interface_->ReadCameraSetting(name);
}

absl::Status Camera::UpdateCameraSetting(const CameraSetting& camera_setting) {
  return image_source_interface_->UpdateCameraSetting(camera_setting);
}

absl::Status Camera::GetFaultsStatus() const {
  return image_source_interface_->GetFaultsStatus();
}

absl::Status Camera::ClearFaults() {
  return image_source_interface_->ClearFaults();
}

absl::StatusOr<ImageSourceInterface::CallbackToken> Camera::AddCaptureCallback(
    absl::AnyInvocable<absl::Status(const CaptureResult&)> callback) {
  return image_source_interface_->AddCaptureCallback(std::move(callback));
}

absl::Status Camera::RemoveCaptureCallback(
    ImageSourceInterface::CallbackToken token) {
  return image_source_interface_->RemoveCaptureCallback(token);
}

absl::Status Camera::StartStream(const CaptureArgs& params) {
  return image_source_interface_->StartStream(params);
}

absl::Status Camera::StopStream() {
  return image_source_interface_->StopStream();
}

ConcurrentCamera::ConcurrentCamera(std::unique_ptr<Camera> camera)
    : camera_(std::move(camera)) {}

absl::StatusOr<std::shared_ptr<ConcurrentCamera>> ConcurrentCamera::Create(
    const CameraIdentifier& identifier) {
  INTR_ASSIGN_OR_RETURN(Camera camera, Camera::Create(identifier));
  return std::shared_ptr<ConcurrentCamera>(
      new ConcurrentCamera(std::make_unique<Camera>(std::move(camera))));
}

void ConcurrentCamera::SetDisabled(bool disabled) {
  const absl::MutexLock lock(mutex_);
  is_disabled_ = disabled;
}

bool ConcurrentCamera::IsDisabled() const {
  const absl::MutexLock lock(mutex_);
  return is_disabled_;
}

absl::StatusOr<ActiveCameraConfig> ActiveCameraConfig::FromProto(
    const intrinsic_proto::perception::v1::CameraConfig& camera_config_proto) {
  INTR_ASSIGN_OR_RETURN(
      (auto [camera_config, camera_id]),
      intrinsic_proto::perception::v1::FromProto(camera_config_proto));
  return ActiveCameraConfig{
      .identifier = std::move(camera_id),
      .config = std::move(camera_config),
  };
}

intrinsic_proto::perception::v1::CameraConfig ActiveCameraConfig::ToProto()
    const {
  return intrinsic_proto::perception::v1::ToProto(config, identifier);
}

CameraManager::CameraManager(const ActiveCameraConfig& active_camera)
    : active_camera_config_(active_camera) {}

absl::StatusOr<ActiveCameraConfig> CameraManager::GetActiveCameraConfig()
    const {
  const absl::MutexLock lock(mutex_);
  if (!active_camera_config_.has_value()) {
    return absl::FailedPreconditionError("No active camera set.");
  }
  return *active_camera_config_;
}

absl::StatusOr<std::shared_ptr<ConcurrentCamera>>
CameraManager::GetActiveCamera() {
  CameraIdentifier identifier;
  {
    const absl::MutexLock lock(mutex_);
    if (!active_camera_config_.has_value()) {
      return absl::FailedPreconditionError("No active camera set.");
    }
    identifier = active_camera_config_->identifier;
  }
  return Get(identifier);
}

absl::StatusOr<std::shared_ptr<ConcurrentCamera>> CameraManager::Get(
    const CameraIdentifier& identifier) {
  if (identifier == CameraIdentifier()) {
    return absl::FailedPreconditionError("No valid camera identifier.");
  }

  const absl::MutexLock lock(mutex_);
  auto it = camera_by_id_.find(identifier);
  if (it != camera_by_id_.end()) {
    if (std::shared_ptr<ConcurrentCamera> camera = it->second.lock();
        camera != nullptr) {
      return camera;
    }
  }
  INTR_ASSIGN_OR_RETURN(auto camera, ConcurrentCamera::Create(identifier));
  camera_by_id_[identifier] = camera;
  if (active_camera_config_.has_value() &&
      identifier == active_camera_config_->identifier) {
    active_concurrent_camera_ = camera;
  }
  return camera;
}

void CameraManager::SetActiveCamera(const ActiveCameraConfig& active_camera) {
  absl::MutexLock lock(mutex_);
  if (active_camera_config_.has_value() &&
      active_camera_config_->identifier == active_camera.identifier) {
    active_camera_config_->config = active_camera.config;
    return;
  }
  active_concurrent_camera_ = nullptr;
  active_camera_config_ = active_camera;
}

absl::StatusOr<std::shared_ptr<GrpcCamera::CameraConnection>>
GrpcCamera::GetConnection(const ConnectionParams& connection_params) const {
  return connection_cache_.Get(connection_params);
}

absl::StatusOr<intrinsic_proto::perception::v1::CameraConfig>
GrpcCamera::GetCameraConfig(const ConnectionParams& connection_params) const {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<CameraConfigConnection> connection,
                        config_connection_cache_.Get(connection_params));
  std::unique_ptr<grpc::ClientContext> client_context =
      connection->channel->GetClientContextFactory()();
  intrinsic_proto::perception::v1::GetCameraConfigRequest request;
  intrinsic_proto::perception::v1::CameraConfig response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(connection->stub->GetCameraConfig(
      client_context.get(), request, &response)));
  return response;
}

}  // namespace perception
}  // namespace intrinsic
