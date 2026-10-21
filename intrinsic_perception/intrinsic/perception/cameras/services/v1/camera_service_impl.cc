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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/repeated_ptr_field.h"
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
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_drivers.pb.h"
#include "intrinsic/perception/proto/v1/camera_identifier.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/perception/proto/v1/image_buffer.pb.h"
#include "intrinsic/perception/proto/v1/post_processing.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_config.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "intrinsic/perception/proto_conversion/v1/camera_service.h"
#include "intrinsic/perception/proto_conversion/v1/camera_settings.h"
#include "intrinsic/perception/proto_conversion/v1/capture_result.h"
#include "intrinsic/perception/proto_conversion/v1/post_processing.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/stats/metrics_utils.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/ret_check.h"
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
  if (!request->camera_config().has_identifier()) {
    return InvalidArgumentErrorBuilderGrpc()
           << "Capture() requires a config with a camera identifier.";
  }

  const absl::Time capture_start_time = absl::Now();
  INTR_ASSIGN_OR_RETURN_GRPC(
      (const auto [camera_config, camera_identifier]),
      intrinsic_proto::perception::v1::FromProto(request->camera_config()));
  CaptureResult capture;
  {
    // Measure time to grab a frame from the camera.
    const stats::ScopedSpan span("Camera::Capture", context);

    absl::Duration timeout = absl::Milliseconds(500);
    if (request->has_timeout()) {
      if (const auto status_or_timeout = ToAbslDuration(request->timeout());
          status_or_timeout.ok()) {
        timeout = status_or_timeout.value();
      }
    }
    const CaptureArgs capture_args{
        .timeout = timeout,
        .sensor_ids = {request->sensor_ids().begin(),
                       request->sensor_ids().end()},
        .camera_config = camera_config,
        .post_processing_by_sensor_id =
            FromProto(request->post_processing_by_sensor_id()).first};
    INTR_ASSIGN_OR_RETURN_GRPC(const std::shared_ptr<ConcurrentCamera> camera,
                               camera_manager_->Get(camera_identifier));
    INTR_ASSIGN_OR_RETURN_GRPC(capture,
                               camera->Call(&Camera::Capture, capture_args));
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

absl::StatusOr<std::shared_ptr<Publisher>>
CameraServiceImpl::GetOrCreatePublisher(absl::string_view topic_name) {
  absl::MutexLock lock(publisher_mutex_);
  auto& weak_publisher_ptr = publisher_;
  auto publisher_ptr = weak_publisher_ptr.lock();

  if (publisher_ptr == nullptr) {
    INTR_ASSIGN_OR_RETURN(
        Publisher publisher,
        pubsub_.CreatePublisher(topic_name,
                                TopicConfig{.topic_qos = TopicConfig::Sensor}));
    publisher_ptr = std::make_shared<Publisher>(std::move(publisher));
    weak_publisher_ptr = publisher_ptr;
  }

  return publisher_ptr;
}

absl::StatusOr<ImageSourceInterface::CaptureCallback>
CameraServiceImpl::GetPublishingCallback(
    const CameraIdentifier& camera_identifier, std::string topic_name) {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<Publisher> publisher,
                        GetOrCreatePublisher(topic_name),
                        _.LogError() << "Failed to create publisher.");
  INTR_RET_CHECK(publisher != nullptr) << "Publisher is null.";

  LOG(INFO) << "Generating publishing capture callback.";
  return [publisher = std::move(publisher), topic_name = std::move(topic_name),
          camera_identifier](const CaptureResult& capture) -> absl::Status {
    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::perception::v1::CaptureResult capture_result_proto,
        intrinsic_proto::perception::v1::ToProto(capture), _.LogError());

    // Do the actual publishing.
    LOG_EVERY_N_SEC(INFO, 10)
        << "Publishing images for camera id: " << camera_identifier
        << " on topic: " << topic_name;
    if (auto status =
            publisher->Publish(capture_result_proto, capture.capture_at);
        !status.ok()) {
      LOG(WARNING) << "Failed to publish image for camera id: "
                   << camera_identifier << " with error: " << status;
    }

    return absl::OkStatus();
  };
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

absl::StatusOr<ImageSourceInterface::CaptureCallback>
CameraServiceImpl::GetDebugLogsCallback(
    const CameraIdentifier& camera_identifier) {
  if (!absl::GetFlag(FLAGS_camera_service_enable_streaming_debug_stats)) {
    return [](const CaptureResult& capture) { return absl::OkStatus(); };
  }

  struct SessionStats {
    size_t num_frames = 0;
    absl::Time start_time = absl::InfinitePast();
    absl::Time last_capture_time = absl::InfinitePast();
  };

  std::shared_ptr<SessionStats> session_stats =
      std::make_shared<SessionStats>();

  LOG(INFO) << "Generating debug capture callback to track stats.";
  return [camera_identifier, session_stats = session_stats](
             const CaptureResult& capture) -> absl::Status {
    if (session_stats->start_time == absl::InfinitePast()) {
      session_stats->start_time = capture.capture_at;
      session_stats->last_capture_time = capture.capture_at;
    } else {
      session_stats->num_frames++;
      absl::Duration last_capture_duration =
          capture.capture_at - session_stats->last_capture_time;
      session_stats->last_capture_time = capture.capture_at;

      LOG_EVERY_N_SEC(INFO, 5)
          << "\nCamera: " << camera_identifier << "\n\tRate: "
          << (session_stats->num_frames /
              absl::ToDoubleSeconds(session_stats->last_capture_time -
                                    session_stats->start_time))
          << "Hz\n\tInstant Rate: "
          << (1 / absl::ToDoubleSeconds(last_capture_duration))
          << "Hz\n\tFrame count: " << session_stats->num_frames
          << "\n\tLast capture time: " << session_stats->last_capture_time
          << "\n\tDimensions[" << capture.sensor_images.size() << "]: "
          << absl::StrJoin(
                 capture.sensor_images, "\n\t\t",
                 [](std::string* out, const auto& sensor_image) {
                   absl::StrAppend(
                       out, "[", sensor_image.sensor_id(),
                       "]: ", sensor_image.Dimensions().cols, "x",
                       sensor_image.Dimensions().rows, " => ",
                       sensor_image.Dimensions().area(), " pixels (",
                       (sensor_image.Dimensions().area() / 10000) / 100.0,
                       "MP)");
                 });
      ;
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
