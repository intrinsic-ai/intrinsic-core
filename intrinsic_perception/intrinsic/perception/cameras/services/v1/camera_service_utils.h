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

#ifndef INTRINSIC_PERCEPTION_CAMERAS_SERVICES_V1_CAMERA_SERVICE_UTILS_H_
#define INTRINSIC_PERCEPTION_CAMERAS_SERVICES_V1_CAMERA_SERVICE_UTILS_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/image_source_interface.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/platform/pubsub/pubsub.h"

namespace intrinsic::perception {

constexpr std::string_view kCameraServiceLoggerPrefix = "/camera_service/";

// Unpacked `CaptureRequest` fields separating the driver-level `CaptureArgs`
// (settings, timeout, sensor IDs, and crop/resize post-processing) from the
// service-level `CameraIdentifier` and per-sensor image encodings.
struct ParsedCaptureRequest {
  CameraIdentifier camera_identifier;
  CaptureArgs capture_args;
  EncodingBySensorId encoding_by_sensor_id;
};

// Per-consumer `CaptureStream` state tracking the consumer's assigned PubSub
// topic (e.g. `<base_topic>/stream_<N>`), its occupied slot index, its
// registered `ImageSourceInterface` callback token, thread-safe mutable
// per-sensor image encodings (shared with the publishing callback), and an
// idempotent RAII cleanup hook invoked when the consumer disconnects.
class ConsumerStreamState {
 public:
  class EncodingState {
   public:
    explicit EncodingState(EncodingBySensorId encoding_by_sensor_id = {})
        : encoding_by_sensor_id_(std::move(encoding_by_sensor_id)) {}

    EncodingBySensorId Get() const ABSL_LOCKS_EXCLUDED(mutex_) {
      absl::MutexLock lock(mutex_);
      return encoding_by_sensor_id_;
    }

    void Update(EncodingBySensorId encoding_by_sensor_id)
        ABSL_LOCKS_EXCLUDED(mutex_) {
      absl::MutexLock lock(mutex_);
      encoding_by_sensor_id_ = std::move(encoding_by_sensor_id);
    }

   private:
    mutable absl::Mutex mutex_;
    EncodingBySensorId encoding_by_sensor_id_ ABSL_GUARDED_BY(mutex_);
  };

  explicit ConsumerStreamState(std::string topic_name,
                               EncodingBySensorId encoding_by_sensor_id = {},
                               size_t slot_index = 0)
      : topic_name_(std::move(topic_name)),
        slot_index_(slot_index),
        encoding_state_(
            std::make_shared<EncodingState>(std::move(encoding_by_sensor_id))) {
  }

  ConsumerStreamState(const ConsumerStreamState&) = delete;
  ConsumerStreamState& operator=(const ConsumerStreamState&) = delete;

  ~ConsumerStreamState() { RunCleanup(); }

  const std::string& topic_name() const { return topic_name_; }
  size_t slot_index() const { return slot_index_; }

  const ImageSourceInterface::CallbackToken& callback_token() const {
    return callback_token_;
  }
  void set_callback_token(ImageSourceInterface::CallbackToken callback_token) {
    callback_token_ = std::move(callback_token);
  }

  void set_cleanup(std::function<void()> cleanup) {
    cleanup_ = std::move(cleanup);
  }

  // Invokes and clears the registered cleanup closure at most once.
  void RunCleanup() {
    if (std::function<void()> cleanup = std::exchange(cleanup_, nullptr)) {
      cleanup();
    }
  }

  EncodingBySensorId GetEncodingBySensorId() const {
    return encoding_state_->Get();
  }

  void UpdateEncodingBySensorId(EncodingBySensorId encoding_by_sensor_id) {
    encoding_state_->Update(std::move(encoding_by_sensor_id));
  }

  std::shared_ptr<const EncodingState> encoding_state() const {
    return encoding_state_;
  }

 private:
  std::string topic_name_;
  size_t slot_index_;
  ImageSourceInterface::CallbackToken callback_token_;
  std::function<void()> cleanup_;
  std::shared_ptr<EncodingState> encoding_state_;
};

// Parses `request` into its `CameraIdentifier`, `CaptureArgs`, and
// `EncodingBySensorId`, using `default_timeout` when `request.has_timeout()`
// is false. Returns `InvalidArgumentError` if `camera_config` does not specify
// an identifier or if `request.timeout()` is invalid.
absl::StatusOr<ParsedCaptureRequest> ParseCaptureRequest(
    const intrinsic_proto::perception::v1::CaptureRequest& request,
    absl::Duration default_timeout = CaptureArgs::kDefaultTimeout);

// Allocates a `ConsumerStreamState` for a new stream consumer, claiming the
// lowest-numbered unoccupied `<base_topic>/stream_<N>` slot in
// `occupied_slots` (or appending `<base_topic>/stream_<N+1>`). Callers must
// mark `occupied_slots[state->slot_index()] = false` only after unregistering
// the consumer's capture callback (or on registration failure) so a reused
// `<base_topic>/stream_<N>` topic never overlaps an in-flight departing
// consumer callback.
//
// While a camera stream is active, the service also provides a session-level
// raw (not undistorted, cropped, resized, or encoded) stream on `<base_topic>`
// (conventionally `/assets/<camera_name>/capture_result`, published only when
// subscribers are present).
std::unique_ptr<ConsumerStreamState> AllocateConsumerStreamState(
    absl::string_view base_topic, EncodingBySensorId encoding_by_sensor_id,
    std::vector<bool>& occupied_slots);

// Creates a session-level capture callback for `<base_topic>` that publishes
// unencoded `CaptureResult` frames only when `<base_topic>` has active PubSub
// subscribers (`Publisher::HasMatchingSubscribers()`).
absl::StatusOr<ImageSourceInterface::CaptureCallback>
CreateBaseTopicPublishingCaptureCallback(const PubSub& pubsub,
                                         absl::string_view base_topic);

// Creates a capture callback that encodes each `CaptureResult` using
// `consumer_state`'s shared `EncodingState` and publishes it on
// `consumer_state.topic_name()`. Captures a `std::shared_ptr<const
// EncodingState>` by value so the callback remains memory-safe even if
// `consumer_state` is destroyed independently.
absl::StatusOr<ImageSourceInterface::CaptureCallback>
CreatePublishingCaptureCallback(const PubSub& pubsub,
                                const ConsumerStreamState& consumer_state);

// Creates a capture callback that periodically logs streaming frame rate and
// image dimensions.
ImageSourceInterface::CaptureCallback CreateDebugLogsCaptureCallback(
    absl::string_view camera_log_id);

absl::Status StoreCaptureResult(
    intrinsic_proto::perception::v1::CaptureResult&& capture_result,
    const PubSub& pubsub,
    const intrinsic_proto::perception::v1::CaptureRequest& request,
    intrinsic_proto::perception::v1::CaptureResponse& response);

// Encodes the capture result and stores it in the capture response or,
// if requested, in a kvstore.
absl::Status EncodeAndStoreCaptureResult(
    CaptureResult&& capture_result, const PubSub& pubsub,
    const intrinsic_proto::perception::v1::CaptureRequest& request,
    intrinsic_proto::perception::v1::CaptureResponse& response);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CAMERAS_SERVICES_V1_CAMERA_SERVICE_UTILS_H_
