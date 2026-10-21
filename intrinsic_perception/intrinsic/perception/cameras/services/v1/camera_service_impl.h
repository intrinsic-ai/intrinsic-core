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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_SERVICES_V1_CAMERA_SERVICE_IMPL_H_
#define INTRINSIC_PERCEPTION_CAMERAS_SERVICES_V1_CAMERA_SERVICE_IMPL_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "intrinsic/perception/cameras/camera.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/core/single_thread_executor.h"
#include "intrinsic/perception/proto/v1/camera_service.grpc.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"

namespace intrinsic {
namespace perception {

// Registers instruments and eagerly initializes metrics. Must be called in
// main() after intrinsic::OpenCensusPlugin is constructed.
void RegisterCameraServiceMetrics();

class CameraServiceImpl final
    : public intrinsic_proto::perception::v1::CameraService::Service {
 public:
  explicit CameraServiceImpl(
      absl_nonnull std::shared_ptr<CameraManager> camera_manager =
          std::make_shared<CameraManager>(),
      std::string_view streaming_topic_name = {});

  grpc::Status ListAvailableCameras(
      grpc::ServerContext* absl_nonnull context,
      const intrinsic_proto::perception::v1::
          ListAvailableCamerasRequest* absl_nonnull request,
      intrinsic_proto::perception::v1::
          ListAvailableCamerasResponse* absl_nonnull response) override;

  grpc::Status DescribeCamera(
      grpc::ServerContext* absl_nonnull context,
      const intrinsic_proto::perception::v1::DescribeCameraRequest* absl_nonnull
          request,
      intrinsic_proto::perception::v1::DescribeCameraResponse* absl_nonnull
          response) override;

  grpc::Status Capture(
      grpc::ServerContext* absl_nonnull context,
      const intrinsic_proto::perception::v1::CaptureRequest* absl_nonnull
          request,
      intrinsic_proto::perception::v1::CaptureResponse* absl_nonnull response)
      override;

  grpc::Status ReadCameraSettingAccess(
      grpc::ServerContext* absl_nonnull context,
      const intrinsic_proto::perception::v1::
          ReadCameraSettingAccessRequest* absl_nonnull request,
      intrinsic_proto::perception::v1::
          ReadCameraSettingAccessResponse* absl_nonnull response) override;

  grpc::Status ReadCameraSettingProperties(
      grpc::ServerContext* absl_nonnull context,
      const intrinsic_proto::perception::v1::
          ReadCameraSettingPropertiesRequest* absl_nonnull request,
      intrinsic_proto::perception::v1::
          ReadCameraSettingPropertiesResponse* absl_nonnull response) override;

  grpc::Status ReadCameraSetting(
      grpc::ServerContext* absl_nonnull context,
      const intrinsic_proto::perception::v1::
          ReadCameraSettingRequest* absl_nonnull request,
      intrinsic_proto::perception::v1::ReadCameraSettingResponse* absl_nonnull
          response) override;

  grpc::Status UpdateCameraSetting(
      grpc::ServerContext* absl_nonnull context,
      const intrinsic_proto::perception::v1::
          UpdateCameraSettingRequest* absl_nonnull request,
      intrinsic_proto::perception::v1::UpdateCameraSettingResponse* absl_nonnull
          response) override;

 private:
  const absl_nonnull std::shared_ptr<CameraManager> camera_manager_;
  const std::string streaming_topic_name_;
  const PubSub pubsub_;
  const SingleThreadExecutor executor_;

  struct StreamingSession {
    // The request that started the streaming session.
    intrinsic_proto::perception::v1::CaptureRequest request;

    absl::Mutex mutex;
    std::optional<std::function<void()>> cleanup ABSL_GUARDED_BY(mutex) =
        std::nullopt;

    ~StreamingSession() {
      if (cleanup.has_value()) {
        (*cleanup)();
      }
    }
  };

  absl::Mutex streaming_mutex_;
  std::weak_ptr<StreamingSession> streaming_session_
      ABSL_GUARDED_BY(streaming_mutex_);

  // A publisher that can be reused.
  absl::Mutex publisher_mutex_;
  std::weak_ptr<Publisher> publisher_ ABSL_GUARDED_BY(publisher_mutex_);

  absl::StatusOr<std::shared_ptr<Publisher>> GetOrCreatePublisher(
      absl::string_view topic_name) ABSL_LOCKS_EXCLUDED(publisher_mutex_);

  // Gets the publishing callback to the image source.
  absl::StatusOr<ImageSourceInterface::CaptureCallback> GetPublishingCallback(
      const CameraIdentifier& camera_identifier, std::string topic_name);
  // Adds the metrics callback to the image source.
  absl::StatusOr<ImageSourceInterface::CaptureCallback> GetMetricsCallback(
      const CameraIdentifier& camera_identifier);
  // Optionally adds the debug logs callback to the image source.
  absl::StatusOr<ImageSourceInterface::CaptureCallback> GetDebugLogsCallback(
      const CameraIdentifier& camera_identifier);
};

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_CAMERAS_SERVICES_V1_CAMERA_SERVICE_IMPL_H_
