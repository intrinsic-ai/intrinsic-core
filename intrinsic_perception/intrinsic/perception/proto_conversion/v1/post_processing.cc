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

#include "intrinsic/perception/proto_conversion/v1/post_processing.h"

#include <cstdint>
#include <optional>
#include <utility>

#include "google/protobuf/map.h"
#include "intrinsic/perception/core/coordinate.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/core/post_processing.h"
#include "intrinsic/perception/core/rectangle.h"
#include "intrinsic/perception/proto/v1/post_processing.pb.h"
#include "intrinsic/perception/proto_conversion/v1/dimensions.h"
#include "intrinsic/perception/proto_conversion/v1/image_buffer.h"

namespace intrinsic_proto::perception::v1 {

std::pair<intrinsic::perception::PostProcessing,
          intrinsic::perception::Encoding>
FromProto(const SensorImagePostProcessing& post_processing) {
  std::optional<intrinsic::perception::Rectangle> crop_region;
  if (post_processing.has_crop()) {
    crop_region.emplace(
        intrinsic::perception::Coordinate(post_processing.crop().origin_x(),
                                          post_processing.crop().origin_y()),
        FromProto(post_processing.crop().dimensions()));
  }

  std::optional<int32_t> cols;
  std::optional<int32_t> rows;
  if (post_processing.has_resize()) {
    cols = post_processing.resize().cols();
    rows = post_processing.resize().rows();
  } else if (post_processing.has_resize_width()) {
    cols = post_processing.resize_width();
  } else if (post_processing.has_resize_height()) {
    rows = post_processing.resize_height();
  }

  return {intrinsic::perception::PostProcessing{
              .crop_region = crop_region,
              .cols = cols,
              .rows = rows,
              .skip_undistortion = post_processing.skip_undistortion()},
          FromProto(post_processing.image_encoding())};
}

std::pair<intrinsic::perception::PostProcessingBySensorId,
          intrinsic::perception::EncodingBySensorId>
FromProto(const google::protobuf::Map<int64_t, SensorImagePostProcessing>&
              post_processing_by_sensor_id) {
  intrinsic::perception::PostProcessingBySensorId post_processings;
  intrinsic::perception::EncodingBySensorId encodings;
  post_processings.reserve(post_processing_by_sensor_id.size());
  encodings.reserve(post_processing_by_sensor_id.size());
  for (const auto& [sensor_id, post_processing_proto] :
       post_processing_by_sensor_id) {
    auto [post_processing, encoding] = FromProto(post_processing_proto);
    post_processings.emplace(sensor_id, std::move(post_processing));
    encodings.emplace(sensor_id, encoding);
  }
  return {post_processings, encodings};
}

SensorImagePostProcessing ToProto(
    const intrinsic::perception::PostProcessing& post_processing,
    intrinsic::perception::Encoding encoding) {
  SensorImagePostProcessing post_processing_proto;
  if (post_processing.crop_region.has_value()) {
    CropOptions crop_options;
    crop_options.set_origin_x(post_processing.crop_region->origin.col);
    crop_options.set_origin_y(post_processing.crop_region->origin.row);
    *crop_options.mutable_dimensions() =
        ToProto(post_processing.crop_region->dimensions);
    *post_processing_proto.mutable_crop() = crop_options;
  }
  if (post_processing.cols.has_value() && post_processing.rows.has_value()) {
    post_processing_proto.mutable_resize()->set_cols(*post_processing.cols);
    post_processing_proto.mutable_resize()->set_rows(*post_processing.rows);
  } else if (post_processing.cols.has_value()) {
    post_processing_proto.set_resize_width(*post_processing.cols);
  } else if (post_processing.rows.has_value()) {
    post_processing_proto.set_resize_height(*post_processing.rows);
  }
  post_processing_proto.set_skip_undistortion(
      post_processing.skip_undistortion);
  post_processing_proto.set_image_encoding(ToProto(encoding));
  return post_processing_proto;
}

google::protobuf::Map<int64_t, SensorImagePostProcessing> ToProto(
    const intrinsic::perception::PostProcessingBySensorId&
        post_processing_by_sensor_id,
    const intrinsic::perception::EncodingBySensorId& encoding_by_sensor_id) {
  google::protobuf::Map<int64_t, SensorImagePostProcessing>
      post_processing_proto;
  for (const auto& [sensor_id, post_processing] :
       post_processing_by_sensor_id) {
    post_processing_proto[sensor_id] =
        ToProto(post_processing, intrinsic::perception::Encoding::kUnspecified);
  }
  for (const auto& [sensor_id, encoding] : encoding_by_sensor_id) {
    post_processing_proto[sensor_id].set_image_encoding(ToProto(encoding));
  }
  return post_processing_proto;
}

}  // namespace intrinsic_proto::perception::v1
