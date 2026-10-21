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

#include "intrinsic/motion_planning/path_planning/planners/joint_shortcutter.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/motion_planning/path_planning/analytic_geometry/intersections.h"
#include "intrinsic/motion_planning/path_planning/data_structures/path_planner_graph.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/path_planner.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/path_planning_utils.h"
#include "intrinsic/motion_planning/path_planning/planners/shortcutter_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/validators.h"
#include "intrinsic/motion_planning/path_planning/validators_config.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

PointPath RemoveCollinearPoints(const PointPath& path) {
  PointPath output;
  output.push_back(path.front());

  for (int ii = 1; ii + 1 < path.size(); ++ii) {
    if (analytical_geometry::AreThreeVectorXdCollinear(output.back(), path[ii],
                                                       path[ii + 1])) {
      // These three points are linear. Skip the middle point.
    } else {
      output.push_back(path[ii]);
    }
  }
  output.push_back(path.back());
  return output;
}

namespace {

// Shortcuts with linear search.
absl::StatusOr<std::vector<PointPath>> ShortcutLinear(
    const PointPath& path, const KinematicsSystemProxy& proxy,
    const PointValidator& point_validator,
    const EdgeValidator& edge_validator) {
  PointPath output;
  output.push_back(path.front());

  // This is the point which we are considering shortcutting.
  eigenmath::VectorXd middle_point = path.at(1);

  for (size_t i = 2; i < path.size(); ++i) {
    INTR_ASSIGN_OR_RETURN(bool is_valid,
                          edge_validator(output.back(), path.at(i)));
    if (!is_valid) {
      output.push_back(middle_point);
    }
    middle_point = path.at(i);
  }
  output.push_back(middle_point);
  return std::vector<PointPath>{output};
}

// Shortcuts with binary search.
absl::StatusOr<std::vector<PointPath>> ShortcutBinarySearch(
    const PointPath& path, const KinematicsSystemProxy& proxy,
    const PointValidator& point_validator,
    const EdgeValidator& edge_validator) {
  PointPath output;
  output.push_back(path.front());

  // Binary search assuming that point i is the last point in the path.
  for (int i = 0; i + 1 < path.size();) {
    int lower = i;
    // Pick upper so that the first point searched is path.size() - 1.
    int upper = (path.size() - 1) * 2 - lower;
    while (lower + 1 < upper && lower < path.size() - 1) {
      int center = (upper + lower) / 2;
      INTR_ASSIGN_OR_RETURN(bool is_valid,
                            edge_validator(output.back(), path.at(center)));
      if (is_valid) {
        lower = center;
      } else {
        upper = center;
      }
    }
    if (lower == i) {
      // TODO(b/438811921): Should we just return an error here? This could
      // result in returning a path with collisions.
      LOG(ERROR) << "Segment to next point is not free! "
                 << "Assuming it is to make progress.";
      lower = i + 1;
    }
    // lower is now the largest index such that the edge
    // (output.back(), path.at(lower)) is valid. This is the next point needed
    // on the path.
    output.push_back(path.at(lower));
    i = lower;
  }

  return std::vector<PointPath>{output};
}

}  // namespace

JointShortcutter::JointShortcutter(const proto::JointShortcutterConfig& config)
    : config_(config) {}

absl::StatusOr<std::vector<PointPath>> JointShortcutter::Plan(
    const PointPath& path_in, const KinematicsSystemProxy& proxy,
    PathPlannerGraph* graph) const {
  const stats::ScopedSpan span("JointShortcutter/Plan");
  if (path_in.size() < 2) {
    return absl::InvalidArgumentError(
        "JointShortcutter requires a path with at least 2 points.");
  }
  if (path_in.size() == 2) {
    // There are two points to shortcut.
    return std::vector<PointPath>{path_in};
  }

  const absl::Time start_time = absl::Now();

  INTR_ASSIGN_OR_RETURN(
      PointValidator point_validator,
      CreatePointValidator(proxy, config_.point_validator_spec()));
  INTR_ASSIGN_OR_RETURN(
      EdgeValidator edge_validator,
      CreateEdgeValidator(proxy, config_.edge_validator_spec()));

  PointPath path;
  if (config_.combine_collinear_segments()) {
    path = RemoveCollinearPoints(path_in);
    VLOG(1) << "Joint shortcutter node count after removing collinear points: "
            << path.size();
  } else {
    path = path_in;
  }

  std::vector<PointPath> out;
  if (config_.use_binary_search()) {
    INTR_ASSIGN_OR_RETURN(
        out,
        ShortcutBinarySearch(path, proxy, point_validator, edge_validator));
  } else {
    INTR_ASSIGN_OR_RETURN(
        out, ShortcutLinear(path, proxy, point_validator, edge_validator));
  }

  const double elapsed_time_seconds =
      absl::ToDoubleSeconds(absl::Now() - start_time);
  VLOG(1) << "Joint shortcutter planning time: " << elapsed_time_seconds
          << " s";
  if (ABSL_VLOG_IS_ON(1)) {
    const PointPath& config_path = out.front();
    const double path_length = ComputePointPathLength(config_path);
    VLOG(1) << "Joint shortcutter path length: " << path_length;
    VLOG(1) << "Joint shortcutter path node count: " << config_path.size();
  }
  return out;
}

absl::StatusOr<std::unique_ptr<PathPlanner>> JointShortcutter::Create(
    const std::optional<google::protobuf::Any>& any_config) {
  proto::JointShortcutterConfig config;
  if (any_config.has_value() && !any_config->UnpackTo(&config)) {
    return absl::InvalidArgumentError("Failed to unpack config.");
  }
  auto planner = std::make_unique<JointShortcutter>(config);
  return std::unique_ptr<PathPlanner>(std::move(planner));
}

REGISTER_PATH_PLANNER(JointShortcutter, "JointShortcutter",
                      JointShortcutter::Create);

}  // namespace intrinsic
