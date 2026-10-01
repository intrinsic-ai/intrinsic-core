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

#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/proto/v1/camera_service.pb.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/perception/proto_conversion/v1/capture_result.h"
#include "intrinsic/perception/proto_conversion/v1/post_processing.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/stats/scoped_span.h"
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

}  // namespace

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
