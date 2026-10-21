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

#ifndef INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_SIMULATION_CAMERA_UTILS_H_
#define INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_SIMULATION_CAMERA_UTILS_H_

#include <memory>
#include <optional>
#include <string_view>

#include "absl/status/statusor.h"
#include "gz/math/Pose3.hh"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/pixel_type.h"
#include "sdf/Camera.hh"
#include "sdf/Element.hh"

namespace intrinsic::perception {

Pose3d GzToIntrinsicSensorPose(const gz::math::Pose3d& gz_pose);

absl::StatusOr<PixelType> PixelTypeFromSdfPluginImageType(
    std::string_view plugin_image_type);

CameraParams GetCameraParamsFromSdf(const sdf::Camera& camera_sdf);

absl::StatusOr<CameraIdentifier> GetCameraIdentifierFromSdf(
    const std::shared_ptr<const sdf::Element>& plugin_sdf);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_SIMULATION_GAZEBO_PLUGINS_CAMERAS_SIMULATION_CAMERA_UTILS_H_
