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

#include "intrinsic/motion_planning/path_planning/planners/joint_interpolation_path_planner.h"

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/motion_planning/path_planning/data_structures/path_planner_graph.h"
#include "intrinsic/motion_planning/path_planning/data_structures/planner_graph.pb.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/path_planner.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/planners/joint_interpolation_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/validation.h"
#include "intrinsic/motion_planning/path_planning/validators.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {
using VertexDescriptor = PathPlannerGraph::VertexDescriptor;

proto::JointInterpolationPathPlannerConfig SetDefaults(
    const proto::JointInterpolationPathPlannerConfig& config) {
  static const proto::JointInterpolationPathPlannerConfig* default_config =
      new proto::JointInterpolationPathPlannerConfig(ParseTextProtoOrDie(R"pb(
        timeout_seconds: 90
      )pb"));
  proto::JointInterpolationPathPlannerConfig out = *default_config;
  out.MergeFrom(config);
  return out;
}

}  // namespace

JointInterpolationPathPlanner::JointInterpolationPathPlanner(
    const proto::JointInterpolationPathPlannerConfig& config)
    : config_(SetDefaults(config)) {
  // Check config settings.
  CHECK_GT(config_.timeout_seconds(), 0)
      << "Timeout in seconds needs to be a positive value greater than zero in "
         "config file of JointInterpolationPathPlanner.";
}

absl::StatusOr<std::vector<PointPath>> JointInterpolationPathPlanner::Plan(
    const PointPath& path, const KinematicsSystemProxy& proxy,
    PathPlannerGraph* graph) const {
  const stats::ScopedSpan span("JointInterpolationPathPlanner/Plan");
  VLOG(1) << "Planning with JointInterpolationPathPlanner with config:\n"
          << config_;
  INTR_RETURN_IF_ERROR(ValidateWithinLimitsAndValidStartAndEnd(path, proxy));

  INTR_ASSIGN_OR_RETURN(
      PointValidator point_validator,
      CreatePointValidator(proxy, config_.point_validator_spec()));
  INTR_ASSIGN_OR_RETURN(
      EdgeValidator edge_validator,
      CreateEdgeValidator(proxy, config_.edge_validator_spec()));

  absl::Time start_time = absl::Now();
  for (int i = 1; i < path.size(); i++) {
    double elapsed_time_seconds =
        absl::ToDoubleSeconds(absl::Now() - start_time);
    if (elapsed_time_seconds > 0.0 &&
        elapsed_time_seconds > config_.timeout_seconds()) {
      return absl::DeadlineExceededError(absl::StrFormat(
          "Planner could not find a path in the given time of "
          "%.3f seconds. Try to increase the motion planning time out or "
          "simplify the collision geometry of the world.",
          config_.timeout_seconds()));
    }
    const eigenmath::VectorXd& start = path.at(i - 1);
    const eigenmath::VectorXd& end = path.at(i);
    VLOG_EVERY_N_SEC(1, 180)
        << "Still planning after " << i << " segments. Evaluating edge from "
        << toString(start) << " to " << toString(end);
    INTR_ASSIGN_OR_RETURN(bool edge_is_valid, edge_validator(start, end));
    if (!edge_is_valid) {
      return absl::NotFoundError(absl::StrFormat(
          "Unable to plan joint interpolation path from %s to %s. Reason: Path "
          "is not fulfilling path constraints.",
          toString(start), toString(end)));
    }
  }

  return std::vector<PointPath>{path};
}

absl::Status JointInterpolationPathPlanner::Precompute(
    const std::vector<PointPath>& paths_to_plan,
    const KinematicsSystemProxy& proxy, PathPlannerGraph* graph) const {
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<PathPlanner>>
JointInterpolationPathPlanner::Create(
    const std::optional<google::protobuf::Any>& any_config) {
  proto::JointInterpolationPathPlannerConfig config;
  if (any_config.has_value() && !any_config->UnpackTo(&config)) {
    return absl::InvalidArgumentError("Failed to unpack config.");
  }
  auto planner = std::make_unique<JointInterpolationPathPlanner>(config);
  return std::unique_ptr<PathPlanner>(std::move(planner));
}

REGISTER_PATH_PLANNER(JointInterpolationPathPlanner,
                      "JointInterpolationPathPlanner",
                      JointInterpolationPathPlanner::Create);
}  // namespace intrinsic
