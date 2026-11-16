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

#ifndef INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_POST_PROCESSING_H_
#define INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_POST_PROCESSING_H_

#include <cstdint>
#include <utility>

#include "google/protobuf/map.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/core/post_processing.h"
#include "intrinsic/perception/proto/v1/post_processing.pb.h"

namespace intrinsic_proto::perception::v1 {

std::pair<intrinsic::perception::PostProcessing,
          intrinsic::perception::Encoding>
FromProto(const SensorImagePostProcessing& post_processing);

std::pair<intrinsic::perception::PostProcessingBySensorId,
          intrinsic::perception::EncodingBySensorId>
FromProto(const google::protobuf::Map<int64_t, SensorImagePostProcessing>&
              post_processing_by_sensor_id);

SensorImagePostProcessing ToProto(
    const intrinsic::perception::PostProcessing& post_processing,
    intrinsic::perception::Encoding encoding);

google::protobuf::Map<int64_t, SensorImagePostProcessing> ToProto(
    const intrinsic::perception::PostProcessingBySensorId&
        post_processing_by_sensor_id,
    const intrinsic::perception::EncodingBySensorId& encoding_by_sensor_id);

}  // namespace intrinsic_proto::perception::v1

// Convenience ToProto aliases to allow argument-dependent lookup. Directly use
// the versioned namespace alternatives above instead when dealing with
// different versions at the same time.
namespace intrinsic::perception {

using ::intrinsic_proto::perception::v1::ToProto;

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_POST_PROCESSING_H_
