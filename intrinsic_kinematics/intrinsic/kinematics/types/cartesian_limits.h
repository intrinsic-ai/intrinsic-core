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

#ifndef INTRINSIC_KINEMATICS_TYPES_CARTESIAN_LIMITS_H_
#define INTRINSIC_KINEMATICS_TYPES_CARTESIAN_LIMITS_H_

#include <cstddef>

#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/fixed_string.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic {

// Holds Cartesian-space limits for translational position, velocity,
// acceleration and jerk, as well as rotational velocity, acceleration and jerk.
struct CartesianLimits {
 public:
  // Default constructs the Cartesian Limits with unlimited values.
  CartesianLimits();

  // Sets each limit range to (-infinity, infinity).
  void SetUnlimited();

  // Returns a shared instance for unlimited CartesianLimits. This is equivalent
  // to CartesianLimits().SetUnlimited() but allows us to share the instance.
  //
  // NOTE: This uses lazy but shared allocation, the first call will allocate on
  // the heap and all subsequent calls will share the instance.
  static const CartesianLimits& Unlimited();

  // Returns true if all limits are valid:
  // * For all min/max pairs: min < max
  // * For all values but position: min <= 0, max >= 0
  // * No special treatment for infinity (in particular, a CartesianLimits
  //   object is valid after calling SetUnlimited() on it!)
  bool IsValid() const;

  eigenmath::Vector3d min_translational_position = eigenmath::Vector3d::Zero();
  eigenmath::Vector3d max_translational_position = eigenmath::Vector3d::Zero();
  eigenmath::Vector3d min_translational_velocity = eigenmath::Vector3d::Zero();
  eigenmath::Vector3d max_translational_velocity = eigenmath::Vector3d::Zero();
  eigenmath::Vector3d min_translational_acceleration =
      eigenmath::Vector3d::Zero();
  eigenmath::Vector3d max_translational_acceleration =
      eigenmath::Vector3d::Zero();
  eigenmath::Vector3d min_translational_jerk = eigenmath::Vector3d::Zero();
  eigenmath::Vector3d max_translational_jerk = eigenmath::Vector3d::Zero();
  double max_rotational_velocity = 0.;
  double max_rotational_acceleration = 0.;
  double max_rotational_jerk = 0.;

  bool operator==(const CartesianLimits& other) const;
  bool operator!=(const CartesianLimits& other) const;
};

CartesianLimits CreateSimpleCartesianLimits(
    double max_translational_position, double max_translational_velocity,
    double max_translational_acceleration, double max_translational_jerk,
    double max_rotational_velocity, double max_rotational_acceleration,
    double max_rotational_jerk);

// Holds Cartesian-space limits for a specific named coordinate frame after
// being resolved against the robot's kinematic chain for trajectory time
// parameterization (TOPP).
//
// This struct includes `parent_link_name` and `parent_link_t_frame` because the
// kinematics chain (`kinematics::Chain`) used during trajectory optimization
// only contains links and joints, not arbitrary attached world frames.
// Resolving the ancestor link and static offset upfront allows TOPP to compute
// the frame Jacobian directly with respect to the parent link.
struct FrameCartesianPathLimits {
 public:
  static constexpr size_t kMaxFrameNameLength{100};

  // The name of the constrained frame or link.
  icon::FixedString<kMaxFrameNameLength> frame_name;
  // The name of the parent link on the kinematic chain.
  icon::FixedString<kMaxFrameNameLength> parent_link_name;
  // The fixed pose of the constrained frame in the parent link frame
  // (`base_t_frame = base_t_parent_link * parent_link_t_frame`).
  Pose3d parent_link_t_frame = Pose3d::Identity();
  // The Cartesian limits for this frame.
  CartesianLimits limits;

  // Returns true if all fields match (including when both instances have empty
  // frame/parent link names). Uses `Pose3d::isApprox` to compare
  // `parent_link_t_frame` and `CartesianLimits::operator==` (exact equality on
  // finite values; note that `CartesianLimits::operator==` treats any two
  // infinities as equal regardless of sign) to compare `limits`.
  friend bool operator==(const FrameCartesianPathLimits& lhs,
                         const FrameCartesianPathLimits& rhs) {
    return lhs.frame_name == rhs.frame_name &&
           lhs.parent_link_name == rhs.parent_link_name &&
           lhs.parent_link_t_frame.isApprox(rhs.parent_link_t_frame) &&
           lhs.limits == rhs.limits;
  }
};

}  // namespace intrinsic

#endif  // INTRINSIC_KINEMATICS_TYPES_CARTESIAN_LIMITS_H_
