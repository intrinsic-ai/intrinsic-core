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

#include "intrinsic/motion_planning/path_planning/planners/pipeline_path_planner.h"

#include <iterator>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/motion_planning/path_planning/data_structures/path_planner_graph.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/path_planner.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline_configs.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

absl::StatusOr<std::unique_ptr<PipelinePathPlanner>>
PipelinePathPlanner::Create(const proto::PipelinePathPlannerConfig& config) {
  INTR_ASSIGN_OR_RETURN(auto planners, CreatePlanningPipeline(config));
  return std::make_unique<PipelinePathPlanner>(config, std::move(planners));
}

PipelinePathPlanner::PipelinePathPlanner(
    const proto::PipelinePathPlannerConfig& config,
    std::vector<std::unique_ptr<PathPlanner>>&& planners)
    : config_(config), planners_(std::move(planners)) {}

absl::StatusOr<std::vector<PointPath>> PipelinePathPlanner::Plan(
    const PointPath& path, const KinematicsSystemProxy& proxy,
    PathPlannerGraph* graph) const {
  const stats::ScopedSpan span("PipelinePathPlanner/Plan");
  std::vector<PointPath> current_paths = {path};
  for (const auto& planner : planners_) {
    std::vector<PointPath> next_paths;
    // For each current path, generate the new paths and accumulate them in
    // next_paths.
    for (const auto& current_path : current_paths) {
      INTR_ASSIGN_OR_RETURN(auto next_paths_for_path,
                            planner->Plan(current_path, proxy, graph));
      next_paths.insert(next_paths.end(),
                        std::make_move_iterator(next_paths_for_path.begin()),
                        std::make_move_iterator(next_paths_for_path.end()));
    }
    // Update current_paths with next_paths.
    //
    // If we are only expanding one path, drop everything except the first
    // results.
    if (next_paths.empty()) {
      return next_paths;
    } else if (config_.expand_all_paths()) {
      current_paths = std::move(next_paths);
    } else {
      current_paths = {next_paths.front()};
    }
  }
  return current_paths;
}

absl::Status PipelinePathPlanner::Precompute(
    const std::vector<PointPath>& paths_to_plan,
    const KinematicsSystemProxy& proxy, PathPlannerGraph* graph) const {
  for (const auto& planner : planners_) {
    INTR_RETURN_IF_ERROR(planner->Precompute(paths_to_plan, proxy, graph));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<PathPlanner>> PipelinePathPlanner::Create(
    const std::optional<google::protobuf::Any>& any_config) {
  proto::PipelinePathPlannerConfig config;
  if (any_config.has_value() && !any_config->UnpackTo(&config)) {
    return absl::InvalidArgumentError("Failed to unpack config.");
  }

  return PipelinePathPlanner::Create(config);
}

}  // namespace intrinsic
