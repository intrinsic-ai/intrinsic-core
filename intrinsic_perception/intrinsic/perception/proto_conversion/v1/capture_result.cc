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

#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/core/parallel.h"
#include "intrinsic/perception/core/range_tools.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/perception/proto/v1/sensor_image.pb.h"
#include "intrinsic/perception/proto_conversion/v1/sensor_image.h"
#include "intrinsic/perception/storage/kvstore_options.h"
#include "intrinsic/perception/storage/kvstore_utils.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/time/deadline_timeout.h"

namespace intrinsic_proto::perception::v1 {

namespace {

template <typename CaptureResultRef>
absl::StatusOr<intrinsic::perception::CaptureResult> FromProtoImpl(
    CaptureResultRef&& capture_result,
    const intrinsic::perception::KeyValueStoreReadOptions& kvstore_options) {
  INTR_ASSIGN_OR_RETURN(const absl::Time capture_at,
                        intrinsic::ToAbslTime(capture_result.capture_at()));
  std::optional<absl::Duration> capture_duration = std::nullopt;
  if (capture_result.has_capture_duration()) {
    INTR_ASSIGN_OR_RETURN(
        capture_duration,
        intrinsic::ToAbslDuration(capture_result.capture_duration()));
  }

  const absl::Time deadline = intrinsic::ToDeadline(kvstore_options.timeout);
  const int num_sensors = capture_result.sensor_images_size();
  std::vector<SensorImage> sensor_protos;
  if constexpr (std::is_rvalue_reference_v<CaptureResultRef&&>) {
    sensor_protos.reserve(num_sensors);
    for (int i = 0; i < num_sensors; ++i) {
      sensor_protos.push_back(
          std::move(*capture_result.mutable_sensor_images(i)));
    }
  }

  std::vector<std::optional<intrinsic::perception::SensorImage>>
      sensor_image_slots(num_sensors);
  INTR_RETURN_IF_ERROR(intrinsic::perception::IsolatedParallelFor(
      0, num_sensors, [&](int i) -> absl::Status {
        intrinsic::perception::KeyValueStoreReadOptions sensor_options =
            kvstore_options;
        sensor_options.timeout = intrinsic::ToTimeout(deadline);
        if constexpr (std::is_rvalue_reference_v<CaptureResultRef&&>) {
          INTR_ASSIGN_OR_RETURN(
              sensor_image_slots[i],
              FromProto(std::move(sensor_protos[i]), sensor_options));
        } else {
          INTR_ASSIGN_OR_RETURN(
              sensor_image_slots[i],
              FromProto(capture_result.sensor_images(i), sensor_options));
        }
        return absl::OkStatus();
      }));

  std::vector<intrinsic::perception::SensorImage> sensor_images;
  sensor_images.reserve(num_sensors);
  for (auto& slot : sensor_image_slots) {
    sensor_images.push_back(*std::move(slot));
  }

  return intrinsic::perception::CaptureResult{
      .capture_at = capture_at,
      .sensor_images = std::move(sensor_images),
      .capture_duration = capture_duration};
}

absl::StatusOr<std::shared_ptr<intrinsic::KeyValueStore>>
PrepareExternalStorage(
    const intrinsic::perception::CaptureResult& capture_result,
    const intrinsic::perception::KeyValueStoreWriteOptions& kvstore_options) {
  if (!kvstore_options.storage_location.has_value()) {
    return nullptr;
  }
  if (kvstore_options.storage_location->key().empty()) {
    return absl::InvalidArgumentError(
        "CaptureResult storage_location.key is empty.");
  }
  if (!intrinsic::perception::IsUnique(
          capture_result.sensor_images,
          &intrinsic::perception::SensorImage::sensor_id)) {
    return absl::InvalidArgumentError(
        "CaptureResult contains duplicate sensor_ids.");
  }
  if (kvstore_options.timeout <= absl::ZeroDuration()) {
    return absl::DeadlineExceededError(
        absl::StrCat("Timed out before writing CaptureResult to key: ",
                     kvstore_options.storage_location->key()));
  }

  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<intrinsic::KeyValueStore> resolved_kvstore,
      intrinsic::perception::ResolveKeyValueStore(
          kvstore_options.storage_location->store(),
          kvstore_options.kvstore_factory));
  if (kvstore_options.delete_existing) {
    INTR_RETURN_IF_ERROR(intrinsic::perception::DeleteKeyIfPresent(
        *resolved_kvstore, intrinsic::KeyValueStore::MakeKey(
                               kvstore_options.storage_location->key(), "*")));
  }
  return resolved_kvstore;
}

absl::StatusOr<std::vector<SensorImage>> ConvertSensorImagesToProto(
    const intrinsic::perception::CaptureResult& capture_result,
    const intrinsic::perception::EncodingBySensorId& encoding_by_sensor_id,
    const intrinsic::perception::KeyValueStoreWriteOptions& kvstore_options,
    const std::shared_ptr<intrinsic::KeyValueStore>& resolved_kvstore,
    absl::Time deadline) {
  auto single_store_factory = [&resolved_kvstore](absl::string_view /*store*/)
      -> absl::StatusOr<std::shared_ptr<intrinsic::KeyValueStore>> {
    return resolved_kvstore;
  };

  const int num_sensors = static_cast<int>(capture_result.sensor_images.size());
  std::vector<SensorImage> sensor_image_protos(num_sensors);
  INTR_RETURN_IF_ERROR(intrinsic::perception::IsolatedParallelFor(
      0, num_sensors, [&](int i) -> absl::Status {
        const intrinsic::perception::SensorImage& sensor_image =
            capture_result.sensor_images[i];
        const intrinsic::perception::Encoding encoding =
            intrinsic::perception::OptionalCopyAt(encoding_by_sensor_id,
                                                  sensor_image.sensor_id())
                .value_or(intrinsic::perception::Encoding::kUnspecified);

        intrinsic::perception::KeyValueStoreWriteOptions sensor_options;
        if (resolved_kvstore != nullptr && !sensor_image.IsEmpty()) {
          sensor_options = kvstore_options;
          sensor_options.kvstore_factory = single_store_factory;
          sensor_options.storage_location->set_key(
              intrinsic::KeyValueStore::MakeKey(
                  kvstore_options.storage_location->key(),
                  absl::StrCat(sensor_image.sensor_id())));
          sensor_options.timeout = intrinsic::ToTimeout(deadline);
          sensor_options.delete_existing = false;
        }

        INTR_ASSIGN_OR_RETURN(sensor_image_protos[i],
                              ToProto(sensor_image, encoding, sensor_options));
        return absl::OkStatus();
      }));
  return sensor_image_protos;
}

}  // namespace

absl::StatusOr<intrinsic::perception::CaptureResult> FromProto(
    const CaptureResult& capture_result,
    const intrinsic::perception::KeyValueStoreReadOptions& kvstore_options) {
  return FromProtoImpl(capture_result, kvstore_options);
}

absl::StatusOr<intrinsic::perception::CaptureResult> FromProto(
    CaptureResult&& capture_result,
    const intrinsic::perception::KeyValueStoreReadOptions& kvstore_options) {
  return FromProtoImpl(std::move(capture_result), kvstore_options);
}

absl::StatusOr<CaptureResult> ToProto(
    const intrinsic::perception::CaptureResult& capture_result,
    const intrinsic::perception::EncodingBySensorId& encoding_by_sensor_id,
    const intrinsic::perception::KeyValueStoreWriteOptions& kvstore_options) {
  CaptureResult result;

  INTR_ASSIGN_OR_RETURN(*result.mutable_capture_at(),
                        intrinsic::FromAbslTime(capture_result.capture_at));
  if (capture_result.capture_duration.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        *result.mutable_capture_duration(),
        intrinsic::FromAbslDuration(capture_result.capture_duration.value()));
  }

  const absl::Time deadline = intrinsic::ToDeadline(kvstore_options.timeout);
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<intrinsic::KeyValueStore> resolved_kvstore,
      PrepareExternalStorage(capture_result, kvstore_options));
  INTR_ASSIGN_OR_RETURN(
      std::vector<SensorImage> sensor_image_protos,
      ConvertSensorImagesToProto(capture_result, encoding_by_sensor_id,
                                 kvstore_options, resolved_kvstore, deadline));

  result.mutable_sensor_images()->Reserve(
      static_cast<int>(sensor_image_protos.size()));
  for (SensorImage& sensor_image_proto : sensor_image_protos) {
    *result.add_sensor_images() = std::move(sensor_image_proto);
  }

  return result;
}

}  // namespace intrinsic_proto::perception::v1
