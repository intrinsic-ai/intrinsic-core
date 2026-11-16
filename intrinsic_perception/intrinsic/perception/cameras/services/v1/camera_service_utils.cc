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

#include "intrinsic/perception/cameras/services/v1/camera_service_utils.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_config.h"
#include "intrinsic/perception/proto_conversion/v1/capture_result.h"
#include "intrinsic/perception/proto_conversion/v1/post_processing.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/topic_config.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::perception {

namespace {

constexpr absl::Duration kDeletePropagationTimeout = absl::Seconds(2);
constexpr absl::Duration kDeletePollTimeout = absl::Milliseconds(100);
constexpr absl::Duration kDeletePollRetryDelay = absl::Milliseconds(2);
constexpr absl::Duration kSetVerificationTimeout = absl::Seconds(5);

absl::StatusOr<int> WaitForKeyDeletion(KeyValueStore& kvstore,
                                       absl::string_view key) {
  const absl::Time deadline = absl::Now() + kDeletePropagationTimeout;
  for (int attempts = 1; absl::Now() < deadline; ++attempts) {
    const absl::Status status =
        kvstore.Get<google::protobuf::Any>(key, kDeletePollTimeout).status();
    if (absl::IsNotFound(status)) {
      return attempts;
    }
    if (!status.ok() && !absl::IsDeadlineExceeded(status)) {
      return status;
    }
    absl::SleepFor(kDeletePollRetryDelay);
  }
  return absl::DeadlineExceededError(
      absl::StrCat("Timed out waiting for key deletion to propagate: ", key));
}

absl::Status PublishCaptureResult(
    const Publisher& publisher, const CaptureResult& capture,
    const EncodingBySensorId& encoding_by_sensor_id = {}) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::perception::v1::CaptureResult capture_result_proto,
      intrinsic_proto::perception::v1::ToProto(capture, encoding_by_sensor_id),
      _.LogError());

  LOG_EVERY_N_SEC(INFO, 10)
      << "Publishing images on topic: " << publisher.TopicName();
  if (const absl::Status status =
          publisher.Publish(capture_result_proto, capture.capture_at);
      !status.ok()) {
    LOG(WARNING) << "Failed to publish image on topic: "
                 << publisher.TopicName() << " with error: " << status;
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<ParsedCaptureRequest> ParseCaptureRequest(
    const intrinsic_proto::perception::v1::CaptureRequest& request,
    absl::Duration default_timeout) {
  if (!request.camera_config().has_identifier()) {
    return absl::InvalidArgumentError(
        "CaptureRequest requires a config with a camera identifier.");
  }
  INTR_ASSIGN_OR_RETURN(
      (auto [camera_config, camera_identifier]),
      intrinsic_proto::perception::v1::FromProto(request.camera_config()));
  absl::Duration timeout = default_timeout;
  if (request.has_timeout()) {
    INTR_ASSIGN_OR_RETURN(timeout, ToAbslDuration(request.timeout()));
  }
  auto [post_processing_by_sensor_id, encoding_by_sensor_id] =
      FromProto(request.post_processing_by_sensor_id());
  return ParsedCaptureRequest{
      .camera_identifier = std::move(camera_identifier),
      .capture_args =
          CaptureArgs{
              .timeout = timeout,
              .sensor_ids = {request.sensor_ids().begin(),
                             request.sensor_ids().end()},
              .camera_config = std::move(camera_config),
              .post_processing_by_sensor_id =
                  std::move(post_processing_by_sensor_id),
          },
      .encoding_by_sensor_id = std::move(encoding_by_sensor_id),
  };
}

std::unique_ptr<ConsumerStreamState> AllocateConsumerStreamState(
    absl::string_view base_topic, EncodingBySensorId encoding_by_sensor_id,
    std::vector<bool>& occupied_slots) {
  size_t slot_index = 0;
  while (slot_index < occupied_slots.size() && occupied_slots[slot_index]) {
    ++slot_index;
  }
  if (slot_index == occupied_slots.size()) {
    occupied_slots.push_back(true);
  } else {
    occupied_slots[slot_index] = true;
  }
  return std::make_unique<ConsumerStreamState>(
      absl::StrCat(base_topic, "/stream_", slot_index + 1),
      std::move(encoding_by_sensor_id), slot_index);
}

absl::StatusOr<ImageSourceInterface::CaptureCallback>
CreateBaseTopicPublishingCaptureCallback(const PubSub& pubsub,
                                         absl::string_view base_topic) {
  INTR_ASSIGN_OR_RETURN(
      Publisher publisher,
      pubsub.CreatePublisher(base_topic,
                             TopicConfig{.topic_qos = TopicConfig::Sensor}),
      _.LogError() << "Failed to create base topic publisher.");

  LOG(INFO) << "Generating base topic publishing capture callback for topic: "
            << base_topic;
  return [publisher = std::move(publisher)](
             const CaptureResult& capture) mutable -> absl::Status {
    const absl::StatusOr<bool> has_subscribers =
        publisher.HasMatchingSubscribers();
    if (!has_subscribers.ok()) {
      LOG_EVERY_N_SEC(WARNING, 10)
          << "Failed to check for matching subscribers on base topic "
          << publisher.TopicName() << ": " << has_subscribers.status();
      return absl::OkStatus();
    }
    if (!*has_subscribers) {
      return absl::OkStatus();
    }
    return PublishCaptureResult(publisher, capture);
  };
}

absl::StatusOr<ImageSourceInterface::CaptureCallback>
CreatePublishingCaptureCallback(const PubSub& pubsub,
                                const ConsumerStreamState& consumer_state) {
  INTR_ASSIGN_OR_RETURN(
      Publisher publisher,
      pubsub.CreatePublisher(consumer_state.topic_name(),
                             TopicConfig{.topic_qos = TopicConfig::Sensor}),
      _.LogError() << "Failed to create publisher.");

  LOG(INFO) << "Generating publishing capture callback for topic: "
            << consumer_state.topic_name();
  return [publisher = std::move(publisher),
          encoding_state = consumer_state.encoding_state()](
             const CaptureResult& capture) -> absl::Status {
    return PublishCaptureResult(publisher, capture, encoding_state->Get());
  };
}

ImageSourceInterface::CaptureCallback CreateDebugLogsCaptureCallback(
    absl::string_view camera_log_id) {
  struct SessionStats {
    size_t num_frames = 0;
    absl::Time start_time = absl::InfinitePast();
    absl::Time last_capture_time = absl::InfinitePast();
  };

  LOG(INFO) << "Generating debug capture callback to track stats.";
  return [camera_log_id = std::string(camera_log_id),
          session_stats = SessionStats{}](
             const CaptureResult& capture) mutable -> absl::Status {
    if (session_stats.start_time == absl::InfinitePast()) {
      session_stats.start_time = capture.capture_at;
      session_stats.last_capture_time = capture.capture_at;
    } else {
      ++session_stats.num_frames;
      const absl::Duration last_capture_duration =
          capture.capture_at - session_stats.last_capture_time;
      session_stats.last_capture_time = capture.capture_at;
      const absl::Duration total_duration =
          session_stats.last_capture_time - session_stats.start_time;
      const double average_rate_hz =
          total_duration > absl::ZeroDuration()
              ? session_stats.num_frames / absl::ToDoubleSeconds(total_duration)
              : 0.0;
      const double instant_rate_hz =
          last_capture_duration > absl::ZeroDuration()
              ? 1.0 / absl::ToDoubleSeconds(last_capture_duration)
              : 0.0;

      LOG_EVERY_N_SEC(INFO, 5)
          << "\nCamera: " << camera_log_id << "\n\tRate: " << average_rate_hz
          << "Hz\n\tInstant Rate: " << instant_rate_hz
          << "Hz\n\tFrame count: " << session_stats.num_frames
          << "\n\tLast capture time: " << session_stats.last_capture_time
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
    }

    return absl::OkStatus();
  };
}

absl::Status StoreCaptureResult(
    intrinsic_proto::perception::v1::CaptureResult&& capture_result,
    const PubSub& pubsub,
    const intrinsic_proto::perception::v1::CaptureRequest& request,
    intrinsic_proto::perception::v1::CaptureResponse& response) {
  const stats::ScopedSpan store_span("CameraService::StoreCaptureResult");
  if (request.has_capture_result_location()) {
    const absl::Time total_start = absl::Now();
    INTR_ASSIGN_OR_RETURN(
        KeyValueStore kvstore,
        pubsub.KeyValueStore(request.capture_result_location().store()));
    const std::string& key = request.capture_result_location().key();

    size_t total_image_bytes = 0;
    size_t payload_bytes = 0;
    if (ABSL_VLOG_IS_ON(1)) {
      for (const auto& sensor_image : capture_result.sensor_images()) {
        total_image_bytes += sensor_image.buffer().data().size();
      }
      payload_bytes = capture_result.ByteSizeLong();
    }

    // 1. Delete previous entry to ensure clean state and avoid reading stale
    // data during verification.
    const absl::Time delete_start = absl::Now();
    INTR_RETURN_IF_ERROR(kvstore.Delete(key));
    const absl::Duration delete_duration = absl::Now() - delete_start;

    // 2. Poll Get until NotFound (or timeout) to ensure deletion has
    // propagated.
    const absl::Time get_start = absl::Now();
    INTR_ASSIGN_OR_RETURN(const int get_attempts,
                          WaitForKeyDeletion(kvstore, key));
    const absl::Duration get_duration = absl::Now() - get_start;

    // 3. Set with verification using kFirstReply.
    KeyValueStore::SetWithVerificationOptions options{
        .mode = KeyValueStore::SetWithVerificationOptions::VerificationMode::
            kFirstReply,
        .timeout = kSetVerificationTimeout,
    };
    const absl::Time set_start = absl::Now();
    INTR_RETURN_IF_ERROR(
        kvstore.SetWithVerification(key, std::move(capture_result), options));
    const absl::Duration set_duration = absl::Now() - set_start;
    const absl::Duration total_duration = absl::Now() - total_start;

    VLOG(1) << "Stored capture result in KVStore at key: " << key
            << " | Delete: " << delete_duration
            << " | Get (poll NotFound, attempts: " << get_attempts
            << "): " << get_duration
            << " | SetWithVerification: " << set_duration
            << " | Total: " << total_duration
            << " | Image size: " << total_image_bytes << " bytes"
            << " | Payload size: " << payload_bytes << " bytes";

    *response.mutable_capture_result_location() =
        request.capture_result_location();
  } else {
    *response.mutable_capture_result() = std::move(capture_result);
  }
  return absl::OkStatus();
}

absl::Status EncodeAndStoreCaptureResult(
    CaptureResult&& capture_result, const PubSub& pubsub,
    const intrinsic_proto::perception::v1::CaptureRequest& request,
    intrinsic_proto::perception::v1::CaptureResponse& response) {
  auto [_, encoding_by_sensor_id] =
      FromProto(request.post_processing_by_sensor_id());
  intrinsic_proto::perception::v1::CaptureResult capture_result_proto;
  {
    const stats::ScopedSpan encode_span("CameraService::EncodeImages");
    INTR_ASSIGN_OR_RETURN(capture_result_proto,
                          intrinsic_proto::perception::v1::ToProto(
                              capture_result, encoding_by_sensor_id));
  }
  INTR_RETURN_IF_ERROR(StoreCaptureResult(std::move(capture_result_proto),
                                          pubsub, request, response));
  return absl::OkStatus();
}

}  // namespace intrinsic::perception
