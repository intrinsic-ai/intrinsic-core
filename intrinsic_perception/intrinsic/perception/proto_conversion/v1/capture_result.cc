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

#include "intrinsic/perception/proto_conversion/v1/capture_result.h"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/perception/proto/v1/sensor_image.pb.h"
#include "intrinsic/perception/proto_conversion/v1/sensor_image.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic_proto::perception::v1 {

namespace {

template <typename CaptureResultRef>
absl::StatusOr<intrinsic::perception::CaptureResult> FromProtoImpl(
    CaptureResultRef&& capture_result) {
  INTR_ASSIGN_OR_RETURN(const absl::Time capture_at,
                        intrinsic::ToAbslTime(capture_result.capture_at()));

  std::vector<intrinsic::perception::SensorImage> sensor_images;
  sensor_images.reserve(capture_result.sensor_images().size());
  for (int i = 0; i < capture_result.sensor_images_size(); ++i) {
    if constexpr (std::is_rvalue_reference_v<CaptureResultRef&&>) {
      INTR_ASSIGN_OR_RETURN(
          intrinsic::perception::SensorImage img,
          FromProto(std::move(*capture_result.mutable_sensor_images(i))));
      sensor_images.push_back(std::move(img));
    } else {
      INTR_ASSIGN_OR_RETURN(intrinsic::perception::SensorImage img,
                            FromProto(capture_result.sensor_images(i)));
      sensor_images.push_back(std::move(img));
    }
  }
  std::optional<absl::Duration> capture_duration = std::nullopt;
  if (capture_result.has_capture_duration()) {
    INTR_ASSIGN_OR_RETURN(
        capture_duration,
        intrinsic::ToAbslDuration(capture_result.capture_duration()));
  }

  return intrinsic::perception::CaptureResult{
      .capture_at = capture_at,
      .sensor_images = std::move(sensor_images),
      .capture_duration = capture_duration};
}

}  // namespace

absl::StatusOr<intrinsic::perception::CaptureResult> FromProto(
    const CaptureResult& capture_result) {
  return FromProtoImpl(capture_result);
}

absl::StatusOr<intrinsic::perception::CaptureResult> FromProto(
    CaptureResult&& capture_result) {
  return FromProtoImpl(std::move(capture_result));
}

absl::StatusOr<CaptureResult> ToProto(
    const intrinsic::perception::CaptureResult& capture_result,
    const absl::flat_hash_map<int64_t, intrinsic::perception::Encoding>&
        encoding_by_sensor_id) {
  CaptureResult result;

  INTR_ASSIGN_OR_RETURN(*result.mutable_capture_at(),
                        intrinsic::FromAbslTime(capture_result.capture_at));

  result.mutable_sensor_images()->Reserve(capture_result.sensor_images.size());
  for (const intrinsic::perception::SensorImage& sensor_image :
       capture_result.sensor_images) {
    const auto it = encoding_by_sensor_id.find(sensor_image.sensor_id());
    const intrinsic::perception::Encoding encoding =
        it != encoding_by_sensor_id.end()
            ? it->second
            : intrinsic::perception::Encoding::kUnspecified;
    INTR_ASSIGN_OR_RETURN(*result.add_sensor_images(),
                          ToProto(sensor_image, encoding));
  }

  if (capture_result.capture_duration.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        *result.mutable_capture_duration(),
        intrinsic::FromAbslDuration(capture_result.capture_duration.value()));
  }

  return result;
}

}  // namespace intrinsic_proto::perception::v1
