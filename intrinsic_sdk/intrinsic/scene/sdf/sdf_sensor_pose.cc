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

#include "intrinsic/scene/sdf/sdf_sensor_pose.h"

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "sdf/Sensor.hh"

namespace intrinsic {
namespace sdf {

namespace {

bool IsOrientedSensorType(::sdf::SensorType sensor_type) {
  switch (sensor_type) {
    case ::sdf::SensorType::CAMERA:
    case ::sdf::SensorType::DEPTH_CAMERA:
    case ::sdf::SensorType::GPU_LIDAR:
    case ::sdf::SensorType::LIDAR:
    case ::sdf::SensorType::RGBD_CAMERA:
    case ::sdf::SensorType::THERMAL_CAMERA:
      return true;
    default:
      return false;
  }
}

Pose3d SdfSensorTIntrinsicSensor(::sdf::SensorType sensor_type) {
  if (IsOrientedSensorType(sensor_type)) {
    // Convert between the following to conventions:
    // SDF: z up, x forward, y left
    // intrinsic::World: z forward, x right, y down
    // The following is the inverse of "<pose>0 0 0 1.570796326794896619
    // -1.570796326794896619 0</pose>".
    return Pose3d(eigenmath::Quaterniond(0.5, -0.5, 0.5, -0.5));
  } else {
    return Pose3d::Identity();
  }
}

}  // namespace

Pose3d SensorPoseFromSdf(const Pose3d& parent_t_sdf_sensor,
                         ::sdf::SensorType sensor_type) {
  return parent_t_sdf_sensor * SdfSensorTIntrinsicSensor(sensor_type);
}

Pose3d SensorPoseToSdf(const Pose3d& parent_t_intrinsic_sensor,
                       ::sdf::SensorType sensor_type) {
  return parent_t_intrinsic_sensor *
         SdfSensorTIntrinsicSensor(sensor_type).inverse();
}

}  // namespace sdf
}  // namespace intrinsic
