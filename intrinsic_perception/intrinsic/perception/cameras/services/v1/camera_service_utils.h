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

#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
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

// Parses `request` into its `CameraIdentifier`, `CaptureArgs`, and
// `EncodingBySensorId`, using `default_timeout` when `request.has_timeout()`
// is false. Returns `InvalidArgumentError` if `camera_config` does not specify
// an identifier or if `request.timeout()` is invalid.
absl::StatusOr<ParsedCaptureRequest> ParseCaptureRequest(
    const intrinsic_proto::perception::v1::CaptureRequest& request,
    absl::Duration default_timeout = CaptureArgs::kDefaultTimeout);

// Creates a session-level capture callback for `<base_topic>` that publishes
// unencoded `CaptureResult` frames.
absl::StatusOr<ImageSourceInterface::CaptureCallback>
CreateBaseTopicPublishingCaptureCallback(const PubSub& pubsub,
                                         absl::string_view base_topic);

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
