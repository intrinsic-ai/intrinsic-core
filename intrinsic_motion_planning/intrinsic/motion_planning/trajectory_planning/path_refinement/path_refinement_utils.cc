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

#include "intrinsic/motion_planning/trajectory_planning/path_refinement/path_refinement_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/interpolation.h"
#include "intrinsic/eigenmath/pose3_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/kinematics/chain.h"
#include "intrinsic/kinematics/state.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/check_joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/math/linspace.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/spline/bspline_utils.h"
#include "intrinsic/math/time_series_utils.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"

#include "intrinsic/motion_planning/proto/v1/motion_events.pb.h"

#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/topp_trajectory_result.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace topp {

absl::StatusOr<std::vector<double>> ComputeUniformlyDiscretizedPathVariables(
    double path_length, double sampling_step,
    bool force_odd_number_of_samples) {
  if (sampling_step <= 0.0) {
    return absl::InvalidArgumentError(
        "Sampling step must be strictly positive.");
  }
  if (path_length <= 0.0) {
    return absl::InvalidArgumentError("Path length must be strictly positive.");
  }
  // We need to assume that path_length is not an integer multiple of
  // sampling_step. To ensure uniform discretization, we compute the actual path
  // spacing with a modified sampling_step, coming from rounding to the nearest
  // integer multiple of sampling_step in path_length. In case the nearest
  // integer is zero, i.e., the sampling step is twice (or more) the path
  // length, we ensure that at least the two boundary points of the path are
  // sampled.
  int num_samples = 1 + std::round(std::max(path_length / sampling_step, 1.0));

  // If `force_odd_number_of_samples` is true, the number
  // of samples is forced to be odd. This is achieved by increasing the number
  // of samples by 1 if the original number is even.

  // This is for example a requirement for the support of `MotionEvents`.

  if (force_odd_number_of_samples && num_samples % 2 == 0) num_samples++;

  return Linspace(0.0, path_length, num_samples);
}

absl::Status FilterOutByDistanceToNeighbour(const double min_path_var_distance,
                                            std::vector<double>& path_vars) {
  if (path_vars.empty()) {
    return absl::InvalidArgumentError("Input vector must not be empty.");
  }
  if (min_path_var_distance < 0.0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Minimum distance between path variables must be non-negative. Got ",
        min_path_var_distance, "."));
  }
  if (path_vars.size() <= 2) {
    // Early return if there is only one or two samples or if the minimum
    // distance between samples is zero.
    return absl::OkStatus();
  }

  // Remove all samples that are too close to the previous sample. Filtered out
  // samples will be moved to the end of the vector. We leave the last sample
  // unchanged at the end of the vector.
  std::optional<double> previous_element = std::nullopt;
  std::vector<double>::iterator samples_to_remove_itr = std::remove_if(
      path_vars.begin(), path_vars.end() - 1, [&](double current_element) {
        const bool remove =
            previous_element.has_value() &&
            std::abs(current_element - previous_element.value()) <=
                min_path_var_distance;
        if (!remove) {
          previous_element = current_element;
        }
        return remove;
      });

  // Add the last sample to the filtered samples and remove the filtered out
  // samples, which are at the end of the vector.
  *samples_to_remove_itr = path_vars.back();
  path_vars.erase(std::next(samples_to_remove_itr), path_vars.end());

  return absl::OkStatus();
}

absl::Status AddTranslationalCartesianArcLengthsToPathAndTrajectory(
    const kinematics::Chain& chain, std::vector<PathSample>& path_samples,
    JointTrajectoryPVA& trajectory) {
  if (path_samples.size() != trajectory.size() || path_samples.size() < 2) {
    return absl::InvalidArgumentError(
        "The path samples and trajectory must have the same size and at least "
        "2 samples.");
  }
  // Now we augment the path and trajectory with the Cartesian path
  // parameterization.
  std::optional<Pose3d> previous_base_t_target;
  std::vector<double> cartesian_path_lengths;
  cartesian_path_lengths.reserve(path_samples.size());
  kinematics::State state(&chain);
  for (PathSample& path_sample : path_samples) {
    INTRINSIC_RT_RETURN_IF_ERROR(
        state.SetDofPositions(path_sample.q, /*check_limits=*/false));
    INTRINSIC_RT_ASSIGN_OR_RETURN(Pose3d base_t_tip,
                                  state.GetTransform(chain.GetTipId()));
    Pose3d base_t_target = base_t_tip * path_sample.tip_t_target;
    if (previous_base_t_target.has_value()) {
      cartesian_path_lengths.push_back(
          cartesian_path_lengths.back() +
          eigenmath::TranslationError(*previous_base_t_target, base_t_target));
    } else {
      cartesian_path_lengths.push_back(0.0);
    }
    path_sample.s_c = cartesian_path_lengths.back();
    previous_base_t_target = base_t_target;
  }
  INTR_ASSIGN_OR_RETURN(
      trajectory,
      JointTrajectoryPVA::Create(
          {trajectory.data().begin(), trajectory.data().end()},
          {trajectory.time_stamps().begin(), trajectory.time_stamps().end()},
          trajectory.joint_dynamic_limits_check_mode(),
          trajectory.interpolation_type(), std::move(cartesian_path_lengths)));
  return absl::OkStatus();
}


absl::Status AddEventKeypointTime(
    const ToppTrajectoryResult& topp_trajectory_result,
    absl::Span<intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events) {
  for (intrinsic_proto::motion_planning::v1::MotionEvent& motion_event :
       motion_events) {
    INTR_ASSIGN_OR_RETURN(
        const double t_event_seconds,
        topp_trajectory_result.squared_path_velocity->GetTimeForPathVariable(
            motion_event.keypoint().keypoint_joint_path_variable_from_start()));
    INTR_ASSIGN_OR_RETURN(
        *motion_event.mutable_keypoint()->mutable_keypoint_time_from_start(),
        FromAbslDuration(absl::Seconds(t_event_seconds)));
  }
  return absl::OkStatus();
}

absl::Status AddEventKeypointCartesianArcLength(
    absl::Span<const PathSample> path_samples,
    absl::Span<intrinsic_proto::motion_planning::v1::MotionEvent>
        motion_events) {
  if (path_samples.size() < 2) {
    return absl::InvalidArgumentError("There must be at least 2 path samples.");
  }

  for (intrinsic_proto::motion_planning::v1::MotionEvent& motion_event :
       motion_events) {
    const int lower_index = GetLowerIndexForValue(
        absl::MakeConstSpan(path_samples),
        motion_event.keypoint().keypoint_joint_path_variable_from_start(),
        [](double a, const PathSample& b) -> bool { return a < b.s; });

    if (lower_index == path_samples.size() - 1) {
      motion_event.mutable_keypoint()
          ->set_keypoint_cartesian_arc_length_from_start_meters(
              path_samples[lower_index].s_c);
      continue;
    }

    // Assume a linear interpolation of s_c between two samples according to the
    // path variable. Alternatively we could use cubic spline constructed for
    // the points surrounding the s_e.
    const double s_c_event = eigenmath::InterpolateLinear(
        eigenmath::Percentage(
            motion_event.keypoint().keypoint_joint_path_variable_from_start(),
            path_samples[lower_index].s, path_samples[lower_index + 1].s),
        path_samples[lower_index].s_c, path_samples[lower_index + 1].s_c);

    motion_event.mutable_keypoint()
        ->set_keypoint_cartesian_arc_length_from_start_meters(s_c_event);
  }

  return absl::OkStatus();
}


absl::StatusOr<double> ComputePathLength(
    absl::Span<const PathSegment> path_segments) {
  if (path_segments.empty()) {
    return absl::InvalidArgumentError("Path segments must not be empty.");
  }

  double path_length = 0.0;
  for (const PathSegment& path_segment : path_segments) {
    if (path_segment.joint_configurations.empty()) {
      return absl::InvalidArgumentError(
          "Path segments must all contain joint configurations.");
    }

    if (path_segment.joint_configurations.front().size() == 0) {
      return absl::InvalidArgumentError(
          "Path segments must contain non-empty joint configurations.");
    }

    for (int i = 0; i < path_segment.joint_configurations.size() - 1; ++i) {
      if (path_segment.joint_configurations[i].size() !=
          path_segment.joint_configurations[i + 1].size()) {
        return absl::InvalidArgumentError(
            "Size mismatch. Path segments contain joint configurations of "
            "different sizes.");
      }

      path_length += (path_segment.joint_configurations[i + 1] -
                      path_segment.joint_configurations[i])
                         .norm();
    }
  }
  return path_length;
}

absl::StatusOr<double> ComputePathLength(
    const absl::Span<const eigenmath::VectorNd> path_waypoints) {
  if (path_waypoints.size() < 2) {
    return absl::InvalidArgumentError(
        "The input `path_waypoints` must have size larger or equal than 2.");
  }
  double path_length = 0.0;
  for (size_t i = 0; i < path_waypoints.size() - 1; ++i) {
    if (path_waypoints[i].size() != path_waypoints[i + 1].size()) {
      return absl::InvalidArgumentError(
          "Size mismatch.The path waypoints must have the same size.");
    }
    path_length += (path_waypoints[i + 1] - path_waypoints[i]).norm();
  }
  return path_length;
}

absl::StatusOr<double> ComputePathLength(
    absl::Span<const PathSample> path_samples) {
  if (path_samples.size() < 2) {
    return absl::InvalidArgumentError(
        "The input `path_samples` must have size larger or equal than 2.");
  }
  double path_length = 0.0;
  for (size_t i = 0; i < path_samples.size() - 1; ++i) {
    if (path_samples[i + 1].q.size() != path_samples[i].q.size()) {
      return absl::InvalidArgumentError(
          "Size mismatch. Path samples contain joint configurations of "
          "different sizes.");
    }
    path_length += (path_samples[i + 1].q - path_samples[i].q).norm();
  }
  return path_length;
}

}  // namespace topp
}  // namespace intrinsic
