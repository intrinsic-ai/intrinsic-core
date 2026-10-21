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

#ifndef INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_SENSOR_IMAGE_H_
#define INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_SENSOR_IMAGE_H_

#include "absl/status/statusor.h"
#include "intrinsic/perception/cameras/sensor_image.h"
#include "intrinsic/perception/core/encoding.h"
#include "intrinsic/perception/proto/v1/sensor_image.pb.h"

namespace intrinsic_proto::perception::v1 {

absl::StatusOr<intrinsic::perception::SensorImage> FromProto(
    const SensorImage& sensor_image);

absl::StatusOr<intrinsic::perception::SensorImage> FromProto(
    SensorImage&& sensor_image);

absl::StatusOr<SensorImage> ToProto(
    const intrinsic::perception::SensorImage& sensor_image,
    intrinsic::perception::Encoding encoding =
        intrinsic::perception::Encoding::kUnspecified);

}  // namespace intrinsic_proto::perception::v1

// Convenience ToProto aliases to allow argument-dependent lookup. Directly use
// the versioned namespace alternatives above instead when dealing with
// different versions at the same time.
namespace intrinsic::perception {

using ::intrinsic_proto::perception::v1::ToProto;

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_PROTO_CONVERSION_V1_SENSOR_IMAGE_H_
