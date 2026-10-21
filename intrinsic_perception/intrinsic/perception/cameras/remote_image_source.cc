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

#include "intrinsic/perception/cameras/remote_image_source.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/proto/v1/camera_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_config.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "intrinsic/perception/proto_conversion/v1/camera_service.h"
#include "intrinsic/perception/proto_conversion/v1/camera_settings.h"
#include "intrinsic/perception/proto_conversion/v1/capture_result.h"
#include "intrinsic/perception/proto_conversion/v1/post_processing.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/proto/repeated_field_util.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/time/deadline_timeout.h"

namespace intrinsic {
namespace perception {

namespace {

constexpr absl::Duration kMaxRequestTimeout = absl::Seconds(1);
constexpr char kServerInstanceMetadataName[] = "x-resource-instance-name";

std::unique_ptr<grpc::ClientContext> CreateContextWithoutDeadline(
    std::optional<std::string> server_instance) {
  auto context = std::make_unique<grpc::ClientContext>();
  if (server_instance.has_value()) {
    context->AddMetadata(kServerInstanceMetadataName, *server_instance);
  }
  return context;
}

std::unique_ptr<grpc::ClientContext> CreateContext(
    absl::Time deadline, std::optional<std::string> server_instance) {
  auto context = CreateContextWithoutDeadline(server_instance);
  context->set_deadline(deadline);
  return context;
}

absl::Status ConditionalPrintCaptureStatistics(
    const intrinsic_proto::perception::v1::CaptureResponse& capture_response,
    absl::Duration request_duration) {
  if (!ABSL_VLOG_IS_ON(1) ||
      !capture_response.capture_result().has_capture_duration())
    return absl::OkStatus();

  INTR_ASSIGN_OR_RETURN(
      const absl::Duration elapsed_capture,
      ToAbslDuration(capture_response.capture_result().capture_duration()));
  const absl::Duration grpc_time = request_duration - elapsed_capture;

  if (grpc_time <= absl::ZeroDuration()) {
    LOG(ERROR) << "tEstimated network transfer time is less than zero: "
               << grpc_time;
    return absl::OkStatus();
  }
  const double bytes_per_second = 1000.0 * capture_response.ByteSizeLong() /
                                  absl::ToDoubleSeconds(grpc_time);
  LOG(INFO) << "Capture() gRPC statistics:\n"
            << absl::StrFormat("\tEstimated network transfer time.: %s\n",
                               absl::FormatDuration(grpc_time))
            << absl::StrFormat("\tEstimated network throughput....: %.2f Mbps",
                               (bytes_per_second * 8) / (1024.0 * 1024.0));
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::vector<CameraIdentifier>>
RemoteImageSource::ListAvailableCameras(
    absl::string_view camera_server_address, absl::Duration timeout,
    std::optional<std::string> server_instance) {
  const absl::Time list_deadline = ToDeadline(timeout);

  INTR_ASSIGN_OR_RETURN(
      auto channel,
      connect::CreateClientChannel(
          camera_server_address, list_deadline,
          connect::UnlimitedMessageSizeGrpcChannelArgs(),
          /*use_default_application_credentials=*/false, server_instance));

  auto camera_stub =
      intrinsic_proto::perception::v1::CameraService::NewStub(channel);
  if (camera_stub == nullptr) {
    return intrinsic::InternalErrorBuilder()
           << "Cannot connect to camera server " << camera_server_address;
  }

  auto context = CreateContext(list_deadline, server_instance);
  intrinsic_proto::perception::v1::ListAvailableCamerasRequest
      list_available_cameras_request;
  intrinsic_proto::perception::v1::ListAvailableCamerasResponse
      list_available_cameras_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(camera_stub->ListAvailableCameras(
                           context.get(), list_available_cameras_request,
                           &list_available_cameras_response)))
      .LogError();
  return FromProto(list_available_cameras_response.camera_identifiers());
}

absl::StatusOr<std::unique_ptr<ImageSourceInterface>> RemoteImageSource::Create(
    const CameraIdentifier& camera_identifier,
    absl::string_view camera_server_address, absl::Duration timeout,
    std::optional<std::string> server_instance) {
  const absl::Time camera_creation_deadline = ToDeadline(timeout);
  INTR_ASSIGN_OR_RETURN(
      auto channel,
      connect::CreateClientChannel(
          camera_server_address, camera_creation_deadline,
          connect::UnlimitedMessageSizeGrpcChannelArgs(),
          /*use_default_application_credentials=*/false, server_instance));

  auto remote_image_source = std::make_unique<RemoteImageSource>();
  remote_image_source->camera_stub_ =
      intrinsic_proto::perception::v1::CameraService::NewStub(channel);
  if (remote_image_source->camera_stub_ == nullptr) {
    return intrinsic::InternalErrorBuilder()
           << "Cannot connect to camera server " << camera_server_address;
  }
  remote_image_source->camera_state_stub_ =
      intrinsic_proto::services::v1::ServiceState::NewStub(channel);
  if (remote_image_source->camera_state_stub_ == nullptr) {
    return intrinsic::InternalErrorBuilder()
           << "Cannot connect to camera service state server "
           << camera_server_address;
  }

  remote_image_source->camera_identifier_ = camera_identifier;
  remote_image_source->server_instance_ = server_instance;

  return remote_image_source;
}

absl::StatusOr<CaptureResult> RemoteImageSource::Capture(
    const CaptureArgs& args) {
  if (camera_stub_ == nullptr) {
    return absl::InternalError(
        "Cannot capture without a connection to the camera server.");
  }
  auto context = CreateContextWithDeadlineFromNow(args.timeout);
  intrinsic_proto::perception::v1::CaptureRequest capture_request;
  *capture_request.mutable_camera_config() =
      ToProto(args.camera_config, camera_identifier_);
  INTR_ASSIGN_OR_RETURN(*capture_request.mutable_timeout(),
                        intrinsic::FromAbslDuration(args.timeout));
  *capture_request.mutable_post_processing_by_sensor_id() =
      ToProto(args.post_processing_by_sensor_id, {});
  capture_request.mutable_sensor_ids()->Add(args.sensor_ids.begin(),
                                            args.sensor_ids.end());
  intrinsic_proto::perception::v1::CaptureResponse capture_response;
  const absl::Time request_start = absl::Now();
  INTR_RETURN_IF_ERROR(ToAbslStatus(camera_stub_->Capture(
      context.get(), capture_request, &capture_response)));
  const absl::Duration request_duration = absl::Now() - request_start;
  INTR_RETURN_IF_ERROR(
      ConditionalPrintCaptureStatistics(capture_response, request_duration));
  return FromProto(std::move(*capture_response.mutable_capture_result()));
}

absl::StatusOr<CameraSettingAccess> RemoteImageSource::ReadCameraSettingAccess(
    absl::string_view name) const {
  auto context = CreateContextWithDeadlineFromNow(kMaxRequestTimeout);
  intrinsic_proto::perception::v1::ReadCameraSettingAccessRequest
      read_setting_access_request;
  *read_setting_access_request.mutable_camera_identifier() =
      ToProto(camera_identifier_);
  read_setting_access_request.set_name(name);
  intrinsic_proto::perception::v1::ReadCameraSettingAccessResponse
      read_setting_access_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(camera_stub_->ReadCameraSettingAccess(
      context.get(), read_setting_access_request,
      &read_setting_access_response)));
  return FromProto(*read_setting_access_response.mutable_access());
}

absl::StatusOr<CameraSettingProperties>
RemoteImageSource::ReadCameraSettingProperties(absl::string_view name) const {
  if (camera_stub_ == nullptr) {
    return absl::InternalError(
        "Cannot read setting properties without a connection to the camera "
        "server.");
  }
  auto context = CreateContextWithDeadlineFromNow(kMaxRequestTimeout);
  intrinsic_proto::perception::v1::ReadCameraSettingPropertiesRequest
      read_setting_properties_request;
  *read_setting_properties_request.mutable_camera_identifier() =
      ToProto(camera_identifier_);
  read_setting_properties_request.set_name(name);
  intrinsic_proto::perception::v1::ReadCameraSettingPropertiesResponse
      read_camera_setting_properties_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(camera_stub_->ReadCameraSettingProperties(
      context.get(), read_setting_properties_request,
      &read_camera_setting_properties_response)));
  return FromProto(
      *read_camera_setting_properties_response.mutable_properties());
}

absl::StatusOr<CameraSetting> RemoteImageSource::ReadCameraSetting(
    absl::string_view name) const {
  auto context = CreateContextWithDeadlineFromNow(kMaxRequestTimeout);
  intrinsic_proto::perception::v1::ReadCameraSettingRequest
      read_setting_request;
  *read_setting_request.mutable_camera_identifier() =
      ToProto(camera_identifier_);
  read_setting_request.set_name(name);
  intrinsic_proto::perception::v1::ReadCameraSettingResponse
      read_setting_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(camera_stub_->ReadCameraSetting(
      context.get(), read_setting_request, &read_setting_response)));
  return FromProto(*read_setting_response.mutable_setting());
}

absl::Status RemoteImageSource::UpdateCameraSetting(
    const CameraSetting& camera_setting) {
  auto context = CreateContextWithDeadlineFromNow(kMaxRequestTimeout);
  intrinsic_proto::perception::v1::UpdateCameraSettingRequest
      update_setting_request;
  *update_setting_request.mutable_camera_identifier() =
      ToProto(camera_identifier_);
  *update_setting_request.mutable_setting() = ToProto(camera_setting);
  intrinsic_proto::perception::v1::UpdateCameraSettingResponse
      update_setting_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(camera_stub_->UpdateCameraSetting(
      context.get(), update_setting_request, &update_setting_response)));
  return absl::OkStatus();
}

absl::StatusOr<std::vector<SensorInformation>>
RemoteImageSource::DescribeCameraSensors() const {
  auto context = CreateContextWithDeadlineFromNow(kMaxRequestTimeout);
  intrinsic_proto::perception::v1::DescribeCameraRequest request;
  *request.mutable_camera_identifier() = ToProto(camera_identifier_);
  intrinsic_proto::perception::v1::DescribeCameraResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      camera_stub_->DescribeCamera(context.get(), request, &response)));
  std::vector<SensorInformation> sensors;
  sensors.reserve(response.sensors_size());
  for (const intrinsic_proto::perception::v1::SensorInformation& sensor_proto :
       response.sensors()) {
    INTR_ASSIGN_OR_RETURN(SensorInformation sensor, FromProto(sensor_proto));
    sensors.push_back(std::move(sensor));
  }
  return sensors;
}

absl::Status RemoteImageSource::GetFaultsStatus() const {
  if (camera_state_stub_ == nullptr) {
    return absl::InternalError(
        "Cannot get faults status without a connection to the camera service "
        "state server.");
  }
  auto context = CreateContextWithDeadlineFromNow(kMaxRequestTimeout);
  const intrinsic_proto::services::v1::GetStateRequest request;
  intrinsic_proto::services::v1::SelfState state;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      camera_state_stub_->GetState(context.get(), request, &state)));
  return state.state_code() ==
                 intrinsic_proto::services::v1::SelfState::STATE_CODE_ERROR
             ? absl::DataLossError(state.extended_status().title())
             : absl::OkStatus();
}

absl::Status RemoteImageSource::ClearFaults() {
  if (camera_state_stub_ == nullptr) {
    return absl::InternalError(
        "Cannot clear faults without a connection to the camera service state "
        "server.");
  }
  auto context = CreateContextWithDeadlineFromNow(kMaxRequestTimeout);
  const intrinsic_proto::services::v1::EnableRequest request;
  intrinsic_proto::services::v1::EnableResponse response;
  return ToAbslStatus(
      camera_state_stub_->Enable(context.get(), request, &response));
}

std::unique_ptr<grpc::ClientContext>
RemoteImageSource::CreateContextWithDeadline(absl::Time deadline) const {
  return CreateContext(deadline, server_instance_);
}

std::unique_ptr<grpc::ClientContext>
RemoteImageSource::CreateContextWithDeadlineFromNow(
    absl::Duration timeout) const {
  return CreateContextWithDeadline(ToDeadline(timeout));
}

absl::StatusOr<ImageSourceInterface::CallbackToken>
RemoteImageSource::AddCaptureCallback(CaptureCallback callback) {
  absl::MutexLock lock(callback_mutex_);
  static int global_id = 0;
  CallbackToken callback_token(absl::StrCat(absl::Now(), global_id++));
  INTR_RET_CHECK(!callbacks_.contains(callback_token))
      << "Callback with id " << callback_token << " already exists.";
  callbacks_[callback_token] = std::move(callback);
  return callback_token;
}

absl::Status RemoteImageSource::RemoveCaptureCallback(CallbackToken token) {
  absl::MutexLock lock(callback_mutex_);
  callbacks_.erase(token);
  return absl::OkStatus();
}

absl::Status RemoteImageSource::StartStream(const CaptureArgs& params) {
  return absl::UnimplementedError(
      "Camera streaming is not "
      "supported.");
}

absl::Status RemoteImageSource::StopStream() {
  return absl::OkStatus();
}

}  // namespace perception
}  // namespace intrinsic
