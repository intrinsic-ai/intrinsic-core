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

#include "intrinsic/simulation/gazebo/plugins/cameras/simulation_camera_utils.h"

#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/text_format.h"
#include "gz/math/Pose3.hh"
#include "gz/math/eigen3/Conversions.hh"
#include "intrinsic/math/pose3.h"
#include "intrinsic/perception/cameras/camera_identifier.h"
#include "intrinsic/perception/core/camera_params.h"
#include "intrinsic/perception/core/dimensions.h"
#include "intrinsic/perception/core/intrinsic_params.h"
#include "intrinsic/perception/proto/v1/camera_identifier.pb.h"
#include "intrinsic/perception/proto_conversion/v1/camera_identifier.h"
#include "sdf/Camera.hh"
#include "sdf/Element.hh"

namespace intrinsic::perception {
namespace {
using CameraSdf = ::sdf::Camera;

IntrinsicParams GetIntrinsicParamsFromSdf(const CameraSdf& camera_sdf) {
  auto fx = camera_sdf.LensIntrinsicsFx();
  auto fy = camera_sdf.LensIntrinsicsFy();
  auto cx = camera_sdf.LensIntrinsicsCx();
  auto cy = camera_sdf.LensIntrinsicsCy();
  return {Dimensions(camera_sdf.ImageWidth(), camera_sdf.ImageHeight()), fx, fy,
          cx, cy};
}
}  // namespace

Pose3d GzToIntrinsicSensorPose(const gz::math::Pose3d& gz_pose) {
  // Convert Gazebo camera sensor pose to intrinsic convention.
  // This should match intrinsic/scene/sdf/sdf_sensor_pose.cc. That class
  // is not used directly so that the sdf namespace can be used without an
  // absolute path (::sdf).
  gz::math::Pose3d pose =
      gz_pose * gz::math::Pose3d(0, 0, 0, 0.5, -0.5, 0.5, -0.5);
  return Pose3d(gz::math::eigen3::convert(pose.Rot()),
                gz::math::eigen3::convert(pose.Pos()));
}

CameraParams GetCameraParamsFromSdf(const CameraSdf& camera_sdf) {
  return CameraParams(GetIntrinsicParamsFromSdf(camera_sdf));
}

absl::StatusOr<CameraIdentifier> GetCameraIdentifierFromSdf(
    const std::shared_ptr<const sdf::Element>& plugin_sdf) {
  if (!plugin_sdf->HasElement("camera_identifier")) {
    return absl::InvalidArgumentError(
        "Could not find <camera_identifier> element.");
  }

  sdf::ElementConstPtr camera_identifier_elem =
      plugin_sdf->FindElement("camera_identifier");
  const std::string camera_identifier_str =
      camera_identifier_elem->Get<std::string>();
  if (intrinsic_proto::perception::v1::CameraIdentifier camera_identifier;
      google::protobuf::TextFormat::ParseFromString(camera_identifier_str,
                                                    &camera_identifier)) {
    return FromProto(camera_identifier);
  }
  return absl::InvalidArgumentError(
      "Failed to parse <camera_identifier> field as an "
      "intrinsic_proto::perception::v1::CameraIdentifier");
}

absl::StatusOr<PixelType> PixelTypeFromSdfPluginImageType(
    std::string_view plugin_image_type) {
  if (plugin_image_type == "intensity") {
    return PixelType::kIntensity;
  } else if (plugin_image_type == "depth") {
    return PixelType::kDepth;
  } else if (plugin_image_type == "point") {
    return PixelType::kPoint;
  } else if (plugin_image_type == "normal") {
    return PixelType::kNormal;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("Unknown image_type '", plugin_image_type, "'"));
}

}  // namespace intrinsic::perception
