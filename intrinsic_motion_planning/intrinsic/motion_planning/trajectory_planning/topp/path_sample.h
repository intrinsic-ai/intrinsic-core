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

#ifndef INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_PATH_SAMPLE_H_
#define INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_PATH_SAMPLE_H_

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.pb.h"

namespace intrinsic {
namespace topp {

struct PathSample {
  // Make a PathSample with each limit range set to (-infinity, infinity).
  // `size` is the number of elements each joint vector should have.
  static absl::StatusOr<PathSample> Unlimited(int size);

  // Sets the size of all joint vectors to `size`, path variables and
  // derivatives are set to zero. Clears all limit values to 0.
  absl::Status SetSize(int size);

  // Sets each limit range in `joint_limits`, `cart_limits`, and all entries in
  // `frame_cartesian_limits` to (-infinity, infinity).
  void SetUnlimited();

  // Sets path variables and derivatives to zero, and `tip_t_target` to the
  // Identity transform. `joint_limits`, `cart_limits`, and
  // `frame_cartesian_limits` are left unchanged.
  void SetZero();

  // Returns the number of elements of the joint position vector.
  size_t Size() const;

  // Returns true if all joint vectors have the same size.
  bool IsSizeConsistent() const;

  // Returns false if size is inconsistent, if `joint_limits`, `cart_limits`, or
  // any entry in `frame_cartesian_limits` is invalid, or if a path variable
  // (`s` or `s_c`) is < 0.0.
  bool IsValid() const;

  double s = 0.0;    // Path variable s (no need to be normalized).
  double s_c = 0.0;  // Path translational Cartesian arc length (in meters).
  eigenmath::VectorNd q;     // Joint position vector.
  eigenmath::VectorNd qp;    // First derivative w.r.t. path variable.
  eigenmath::VectorNd qpp;   // Second derivative w.r.t. path variable.
  eigenmath::VectorNd qppp;  // Third derivative w.r.t. path variable
                             // (outgoing/right).
  std::optional<eigenmath::VectorNd>
      qppp_in;               // Optional third derivative w.r.t. path variable
                             // (incoming/left) if discontinuous.
  JointLimits joint_limits;  // Maximum joint limits for this path sample.
  CartesianLimits cart_limits;  // Max Cartesian limits for this path sample.
  Pose3d tip_t_target =
      Pose3d::Identity();  // Offset between robot tip and target frame.
  std::vector<FrameCartesianPathLimits>
      frame_cartesian_limits;  // Cartesian limits for additional monitored
                               // frames. If empty, no additional per-frame
                               // Cartesian limits are enforced (only
                               // `cart_limits` and `joint_limits` apply).
  std::string segment_id;  // The id of the path segment this sample belongs to.
};

intrinsic_proto::topp::PathSample ToProto(const PathSample& path_sample);

absl::StatusOr<PathSample> FromProto(
    const intrinsic_proto::topp::PathSample& path_sample_proto);

}  // namespace topp
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_TRAJECTORY_PLANNING_TOPP_PATH_SAMPLE_H_
