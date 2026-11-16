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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_REMOTE_IMAGE_SOURCE_H_
#define INTRINSIC_PERCEPTION_CAMERAS_REMOTE_IMAGE_SOURCE_H_

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
#include "grpcpp/support/sync_stream.h"
#include "intrinsic/assets/services/proto/v1/service_state.grpc.pb.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/proto/v1/camera_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/subscription.h"

namespace intrinsic::perception {

class RemoteImageSource : public ImageSourceInterface {
 public:
  static absl::StatusOr<std::vector<CameraIdentifier>> ListAvailableCameras(
      absl::string_view camera_server_address, absl::Duration timeout,
      std::optional<std::string> server_instance = std::nullopt);

  static absl::StatusOr<std::unique_ptr<ImageSourceInterface>> Create(
      const CameraIdentifier& camera_identifier,
      absl::string_view camera_server_address, absl::Duration timeout,
      std::optional<std::string> server_instance = std::nullopt);

  absl::StatusOr<CaptureResult> Capture(const CaptureArgs& args) override;

  absl::StatusOr<CameraSettingAccess> ReadCameraSettingAccess(
      absl::string_view name) const override;
  absl::StatusOr<CameraSettingProperties> ReadCameraSettingProperties(
      absl::string_view name) const override;
  absl::StatusOr<CameraSetting> ReadCameraSetting(
      absl::string_view name) const override;
  absl::Status UpdateCameraSetting(
      const CameraSetting& camera_setting) override;

  absl::StatusOr<std::vector<SensorInformation>> DescribeCameraSensors()
      const final;

  absl::Status GetFaultsStatus() const final;

  absl::Status ClearFaults() final;

  // Registers `callback` to receive the server-published stream; returns
  // `absl::UnimplementedError` if `options` specifies non-empty
  // `post_processing_by_sensor_id` or `sensor_ids` (note that `std::nullopt`
  // observers receive whatever frames the server publishes on the stream
  // topic).
  absl::StatusOr<CallbackToken> AddCaptureCallback(
      ImageSourceInterface::CaptureCallback callback,
      std::optional<CaptureCallbackOptions> options) final;

  // Returns `absl::UnimplementedError` (unsupported on `RemoteImageSource`).
  absl::Status UpdateCaptureCallbackOptions(
      const CallbackToken& token, CaptureCallbackOptions options) final;

  absl::Status RemoveCaptureCallback(CallbackToken token) final;

  absl::Status StartStream(const CaptureArgs& params) final;

  absl::Status StopStream() final;

 private:
  std::unique_ptr<grpc::ClientContext> CreateContextWithDeadline(
      absl::Time deadline) const;
  std::unique_ptr<grpc::ClientContext> CreateContextWithDeadlineFromNow(
      absl::Duration timeout) const;

  CameraIdentifier camera_identifier_;
  std::unique_ptr<intrinsic_proto::perception::v1::CameraService::Stub>
      camera_stub_;
  std::unique_ptr<intrinsic_proto::services::v1::ServiceState::Stub>
      camera_state_stub_;
  std::optional<std::string> server_instance_;

  absl::Mutex callback_mutex_;
  absl::Mutex callback_invocation_mutex_ ABSL_ACQUIRED_BEFORE(callback_mutex_);
  absl::flat_hash_map<CallbackToken, std::shared_ptr<CaptureCallback>>
      callbacks_ ABSL_GUARDED_BY(callback_mutex_);
  absl::flat_hash_map<CallbackToken, std::weak_ptr<CaptureCallback>>
      in_flight_removed_callbacks_ ABSL_GUARDED_BY(callback_mutex_);

};

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_REMOTE_IMAGE_SOURCE_H_
