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

#include "intrinsic/perception/cameras/services/v1/camera_service_impl.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/util/message_differencer.h"
#include "grpcpp/server_context.h"
#include "intrinsic/perception/cameras/camera.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/camera_setting.h"
#include "intrinsic/perception/cameras/camera_setting_access.h"
#include "intrinsic/perception/cameras/camera_setting_properties.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_information.h"
#include "intrinsic/perception/cameras/services/v1/camera_service_utils.h"
#include "intrinsic/perception/logging/sensor_image.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "intrinsic/perception/proto_conversion/v1/camera_service.h"
#include "intrinsic/perception/proto_conversion/v1/camera_settings.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/stats/metrics_utils.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "opentelemetry/metrics/meter.h"
#include "opentelemetry/metrics/sync_instruments.h"

ABSL_FLAG(
    bool, camera_service_enable_streaming_debug_stats, false,
    "Whether to enable camera streaming debug stat collection and printing.");

namespace intrinsic {
namespace perception {

namespace metrics_logging {
namespace {

using ::opentelemetry::metrics::Counter;
using ::opentelemetry::metrics::Gauge;

constexpr char kFramesProcessingRateName[] =
    "intrinsic/perception/camera_stream_frame_rate";
constexpr char kFramesProcessingRateDescription[] =
    "Number of frames processed.";
constexpr char kFramesProcessedName[] =
    "intrinsic/perception/camera_stream_frame_count";
constexpr char kFramesProcessedDescription[] = "Number of frames processed.";
constexpr char kTimeToProcessFrameName[] =
    "intrinsic/perception/camera_stream_frame_time";
constexpr char kTimeToProcessFrameDescription[] =
    "Amount of time spent to process frames.";
constexpr char kNanoSeconds[] = "ns";
constexpr char kHertz[] = "hz";

// Instruments in this file are intentionally released so they remain alive
// for the program's lifetime, avoiding global destructor ordering errors on
// service destruction. Each is a one-time allocation, so this doesn't leak.
Gauge<double>& LogFramesProcessingRate() {
  static Gauge<double>* gauge =
      stats::GetMeter()
          ->CreateDoubleGauge(kFramesProcessingRateName,
                              kFramesProcessingRateDescription, kHertz)
          .release();
  return *gauge;
}

Counter<uint64_t>& LogFramesProcessed() {
  static Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(kFramesProcessedName,
                                kFramesProcessedDescription, "1")
          .release();
  return *counter;
}

Counter<uint64_t>& LogTimeToProcessFrame() {
  static Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(kTimeToProcessFrameName,
                                kTimeToProcessFrameDescription, kNanoSeconds)
          .release();
  return *counter;
}

}  // namespace
}  // namespace metrics_logging

void RegisterCameraServiceMetrics() {
  // Eagerly initialize the instruments to register them.
  metrics_logging::LogFramesProcessingRate();
  metrics_logging::LogFramesProcessed();
  metrics_logging::LogTimeToProcessFrame();
}

CameraServiceImpl::CameraServiceImpl(
    absl_nonnull std::shared_ptr<CameraManager> camera_manager,
    std::string_view streaming_topic_name)
    : camera_manager_(std::move(camera_manager)),
      streaming_topic_name_(streaming_topic_name) {}

grpc::Status CameraServiceImpl::ListAvailableCameras(
    grpc::ServerContext* absl_nonnull context,
    const intrinsic_proto::perception::v1::
        ListAvailableCamerasRequest* absl_nonnull request,
    intrinsic_proto::perception::v1::ListAvailableCamerasResponse* absl_nonnull
        response) {
  LOG(INFO) << "ListAvailableCameras";
  INTR_ASSIGN_OR_RETURN_GRPC(std::vector<CameraIdentifier> camera_identifiers,
                             Camera::ListAvailableCameras());
  response->mutable_camera_identifiers()->Reserve(camera_identifiers.size());
  for (const CameraIdentifier& camera_identifier : camera_identifiers) {
    *response->add_camera_identifiers() = ToProto(camera_identifier);
  }
  return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::DescribeCamera(
    grpc::ServerContext* absl_nonnull context,
    const intrinsic_proto::perception::v1::DescribeCameraRequest* absl_nonnull
        request,
    intrinsic_proto::perception::v1::DescribeCameraResponse* absl_nonnull
        response) {
  LOG(INFO) << "DescribeCamera: " << request->ShortDebugString();
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "DescribeCamera() requires a camera identifier.";
  }
  INTR_ASSIGN_OR_RETURN_GRPC(
      const std::shared_ptr<ConcurrentCamera> camera,
      camera_manager_->Get(FromProto(request->camera_identifier())));
  INTR_ASSIGN_OR_RETURN_GRPC(const DescribeCameraResponse response_struct,
                             camera->Call(&Camera::DescribeCamera));
  response->mutable_sensors()->Reserve(response_struct.sensors.size());
  for (const SensorInformation& info : response_struct.sensors) {
    *response->add_sensors() = ToProto(info);
  }
  return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::Capture(
    grpc::ServerContext* absl_nonnull context,
    const intrinsic_proto::perception::v1::CaptureRequest* absl_nonnull request,
    intrinsic_proto::perception::v1::CaptureResponse* absl_nonnull response) {
  LOG(INFO) << "Capture: " << request->ShortDebugString();
  const absl::Time capture_start_time = absl::Now();
  INTR_ASSIGN_OR_RETURN_GRPC(const ParsedCaptureRequest parsed_request,
                             ParseCaptureRequest(*request));
  const CameraIdentifier& camera_identifier = parsed_request.camera_identifier;
  CaptureResult capture;
  {
    // Measure time to grab a frame from the camera.
    const stats::ScopedSpan span("Camera::Capture", context);
    INTR_ASSIGN_OR_RETURN_GRPC(const std::shared_ptr<ConcurrentCamera> camera,
                               camera_manager_->Get(camera_identifier));
    INTR_ASSIGN_OR_RETURN_GRPC(
        capture, camera->Call(&Camera::Capture, parsed_request.capture_args));
  }
  const absl::Time capture_at = capture.capture_at;
  if (request->has_context()) {
    const stats::ScopedSpan span("LogCaptureResult", context);
    INTR_RETURN_IF_ERROR_GRPC(
        LogSensorImages(executor_, capture.sensor_images,
                        absl::StrCat(kCameraServiceLoggerPrefix,
                                     CanonicalString(camera_identifier)),
                        request->context())
            .status());
  }
  {
    const stats::ScopedSpan span("EncodeAndStoreCaptureResult", context);
    INTR_RETURN_IF_ERROR_GRPC(EncodeAndStoreCaptureResult(
        std::move(capture), pubsub_, *request, *response));
  }
  const absl::Duration capture_duration = absl::Now() - capture_start_time;
  LOG(INFO) << "Captured from image source at " << capture_at << " in "
            << capture_duration << ".";
  return grpc::Status::OK;
}

absl::StatusOr<ImageSourceInterface::CaptureCallback>
CameraServiceImpl::GetMetricsCallback(
    const CameraIdentifier& camera_identifier) {
  struct SessionStats {
    bool is_started = false;
    absl::Time last_capture_time;
  };

  std::shared_ptr<SessionStats> session_stats =
      std::make_shared<SessionStats>();

  LOG(INFO) << "Generating capture callback to track metrics.";
  return [camera_identifier, session_stats = session_stats](
             const CaptureResult& capture) -> absl::Status {
    if (!session_stats->is_started) {
      session_stats->last_capture_time = capture.capture_at;
      session_stats->is_started = true;
    } else {
      absl::Duration last_capture_duration =
          capture.capture_at - session_stats->last_capture_time;
      session_stats->last_capture_time = capture.capture_at;

      // Record the instant rate to the OpenTelemetry metrics.
      // TODO(b/442910605): The metrics are not currently propagated to pantheon
      metrics_logging::LogFramesProcessed().Add(1);
      if (last_capture_duration > absl::ZeroDuration()) {
        metrics_logging::LogFramesProcessingRate().Record(
            1.0 / absl::ToDoubleSeconds(last_capture_duration));
      }
      metrics_logging::LogTimeToProcessFrame().Add(absl::ToInt64Nanoseconds(
          std::max(absl::ZeroDuration(), last_capture_duration)));
    }

    return absl::OkStatus();
  };
}

grpc::Status CameraServiceImpl::ReadCameraSettingAccess(
    grpc::ServerContext* absl_nonnull context,
    const intrinsic_proto::perception::v1::
        ReadCameraSettingAccessRequest* absl_nonnull request,
    intrinsic_proto::perception::v1::
        ReadCameraSettingAccessResponse* absl_nonnull response) {
  LOG(INFO) << "ReadCameraSettingAccess: " << request->ShortDebugString();
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "ReadCameraSettingAccess() requires a camera identifier.";
  }
  INTR_ASSIGN_OR_RETURN_GRPC(
      const std::shared_ptr<ConcurrentCamera> camera,
      camera_manager_->Get(FromProto(request->camera_identifier())));
  INTR_ASSIGN_OR_RETURN_GRPC(
      const CameraSettingAccess access,
      camera->Call(&Camera::ReadCameraSettingAccess, request->name()),
      _.LogError());
  *response->mutable_access() = ToProto(access);
  return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::ReadCameraSettingProperties(
    grpc::ServerContext* absl_nonnull context,
    const intrinsic_proto::perception::v1::
        ReadCameraSettingPropertiesRequest* absl_nonnull request,
    intrinsic_proto::perception::v1::
        ReadCameraSettingPropertiesResponse* absl_nonnull response) {
  LOG(INFO) << "ReadCameraSettingProperties: " << request->ShortDebugString();
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "ReadCameraSettingProperties() requires a camera identifier.";
  }
  INTR_ASSIGN_OR_RETURN_GRPC(
      const std::shared_ptr<ConcurrentCamera> camera,
      camera_manager_->Get(FromProto(request->camera_identifier())));
  INTR_ASSIGN_OR_RETURN_GRPC(
      const CameraSettingProperties properties,
      camera->Call(&Camera::ReadCameraSettingProperties, request->name()),
      _.LogError());
  *response->mutable_properties() = ToProto(properties);
  return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::ReadCameraSetting(
    grpc::ServerContext* absl_nonnull context,
    const intrinsic_proto::perception::v1::
        ReadCameraSettingRequest* absl_nonnull request,
    intrinsic_proto::perception::v1::ReadCameraSettingResponse* absl_nonnull
        response) {
  LOG(INFO) << "ReadCameraSetting: " << request->ShortDebugString();
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "ReadCameraSetting() requires a camera identifier.";
  }
  INTR_ASSIGN_OR_RETURN_GRPC(
      const std::shared_ptr<ConcurrentCamera> camera,
      camera_manager_->Get(FromProto(request->camera_identifier())));
  INTR_ASSIGN_OR_RETURN_GRPC(
      const CameraSetting setting,
      camera->Call(&Camera::ReadCameraSetting, request->name()));
  *response->mutable_setting() = ToProto(setting);
  return grpc::Status::OK;
}

grpc::Status CameraServiceImpl::UpdateCameraSetting(
    grpc::ServerContext* absl_nonnull context,
    const intrinsic_proto::perception::v1::
        UpdateCameraSettingRequest* absl_nonnull request,
    intrinsic_proto::perception::v1::UpdateCameraSettingResponse* absl_nonnull
        response) {
  LOG(INFO) << "UpdateCameraSetting: " << request->ShortDebugString();
  if (!request->has_camera_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "UpdateCameraSetting() requires a camera identifier.";
  }

  INTR_ASSIGN_OR_RETURN_GRPC(
      const std::shared_ptr<ConcurrentCamera> camera,
      camera_manager_->Get(FromProto(request->camera_identifier())));
  INTR_RETURN_IF_ERROR_GRPC(
      camera->Call(&Camera::UpdateCameraSetting, FromProto(request->setting())))
      .LogError();
  return grpc::Status::OK;
}

}  // namespace perception
}  // namespace intrinsic
