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

#include "intrinsic/motion_planning/path_planning/planners/rrt_connect_path_planner.h"

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/motion_planning/path_planning/data_structures/path_planner_graph.h"
#include "intrinsic/motion_planning/path_planning/data_structures/planner_graph.pb.h"
#include "intrinsic/motion_planning/path_planning/data_structures/state_sampler.h"
#include "intrinsic/motion_planning/path_planning/data_structures/state_sampler_utils.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/path_planning/path_planner.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/path_planning_utils.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/rrt_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/rrt_connect.h"
#include "intrinsic/motion_planning/path_planning/planners/validation.h"
#include "intrinsic/motion_planning/path_planning/robot_chain.h"
#include "intrinsic/motion_planning/path_planning/validators.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

namespace {
using VertexDescriptor = PathPlannerGraph::VertexDescriptor;

proto::RrtPathPlannerConfig SetDefaults(
    const proto::RrtPathPlannerConfig& config) {
  static const proto::RrtPathPlannerConfig* default_config =
      new proto::RrtPathPlannerConfig(ParseTextProtoOrDie(R"pb(
        rrt_connect_config {
          max_iters: 1000000000
          step_size: 0.1
          timeout_seconds: 180
          iters_between_goal_checks: 50
          num_candidates: 100
          max_num_failures_stored: 100
        }
        intratree_spacing: 0.1
        intertree_weight: 0.1
        failure_weight: 0.2
        state_sampler_spec {
          name: "RrtScoringHeuristicStateSampler"
          config {
            [type.googleapis.com/
             intrinsic.proto.RrtScoringHeuristicStateSamplerConfig] {
              growing_tree_spacing: 0.1
              growing_tree_width: 5
              connecting_tree_spacing: 0
              connecting_tree_width: 0.1
              connecting_tree_weight: 0.1
              failure_spacing: 0
              failure_width: 20
              failure_weight: 0.2
              num_candidates: 100
              max_num_failures_stored: 100
            }
          }
        }
      )pb"));
  proto::RrtPathPlannerConfig out = *default_config;
  out.MergeFrom(config);
  return out;
}

absl::Status ConnectToGraphIfValid(const eigenmath::VectorXd& config,
                                   const EdgeValidator& edge_validator,
                                   double squared_distance,
                                   PathPlannerGraph* graph) {
  if (graph->Size() == 0) {
    // Nothing to do. No vertex in it to connect to.
    return absl::OkStatus();
  }
  INTR_ASSIGN_OR_RETURN(const auto nearest_neighbor_vertex,
                        graph->GetNearestNeighborForConfig(config));
  INTR_ASSIGN_OR_RETURN(const eigenmath::VectorXd nearest_neighbor_config,
                        graph->GetConfiguration(nearest_neighbor_vertex));
  if (nearest_neighbor_config.size() != config.size()) {
    return absl::InternalError(
        "Graph seems to contain elements of different size then the "
        "requested planning problem.");
  }
  // If distance reasonable, try to connect
  if ((nearest_neighbor_config - config).squaredNorm() < squared_distance) {
    INTR_ASSIGN_OR_RETURN(const bool valid,
                          edge_validator(config, nearest_neighbor_config));
    if (valid) {
      INTR_ASSIGN_OR_RETURN(
          const auto new_vertex,
          graph->AddVertex(config, proto::PathPlannerGraph::VALID));
      INTR_RETURN_IF_ERROR(graph->AddEdge(new_vertex, nearest_neighbor_vertex,
                                          proto::PathPlannerGraph::VALID));
    }
  }
  return absl::OkStatus();
}
}  // namespace

RrtConnectPathPlanner::RrtConnectPathPlanner(
    const proto::RrtPathPlannerConfig& config)
    : config_(SetDefaults(config)) {
  // Check config settings.
  CHECK_GT(config_.rrt_connect_config().max_iters(), 0)
      << "No negative  or 0 values for maximum number of iterations allowed in "
         "config file of RrtConnectPathPlanner.";
  CHECK_GT(config_.rrt_connect_config().timeout_seconds(), 0)
      << "Timeout in seconds needs to be a positive value greater than zero in "
         "config file of RrtConnectPathPlanner.";
  CHECK_GT(config_.rrt_connect_config().step_size(), 0)
      << "The step size needs to be a positive value greater than zero in the "
         "config file of RrtConnectPathPlanner.";
  CHECK_GE(config_.rrt_connect_config().iters_between_goal_checks(), -1)
      << "The numbers of iterations between goal checks needs to be a value >= "
         "-1 in the config file of RrtConnectPathPlanner.";
}

absl::StatusOr<std::vector<PointPath>> RrtConnectPathPlanner::Plan(
    const PointPath& path, const KinematicsSystemProxy& proxy,
    PathPlannerGraph* graph) const {
  const stats::ScopedSpan span("RrtConnectPathPlanner/Plan");
  VLOG(1) << "Planning with RrtConnectPathPlanner with config:\n" << config_;
  INTR_RETURN_IF_ERROR(ValidateWithinLimitsAndValidStartAndEnd(path, proxy));

  INTR_ASSIGN_OR_RETURN(
      PointValidator point_validator,
      CreatePointValidator(proxy, config_.point_validator_spec()));
  INTR_ASSIGN_OR_RETURN(
      EdgeValidator edge_validator,
      CreateEdgeValidator(proxy, config_.edge_validator_spec()));

  // Break up the input path into valid segments. We will be replanning the
  // gaps between the valid segments.
  //
  // Example:
  // Suppose we have a path (A -> B -> C) with invalid edge B -> C. This call
  // would produce segments ({A, B}, {C}) so only B -> C is replanned.
  INTR_ASSIGN_OR_RETURN(
      auto valid_path_segments,
      GetValidPathSegments(path, point_validator, edge_validator));
  // Check that start and end of the segments match the start and end of the
  // path. This is a hard CHECK because the validation above should imply this
  // case never happens.
  CHECK(!valid_path_segments.empty());
  CHECK_EQ(valid_path_segments.front().front(), 0);
  CHECK_EQ(valid_path_segments.back().back(), path.size() - 1);

  // Initialize the output with the first valid segment.
  PointPath output_path;
  for (int path_index : valid_path_segments.front()) {
    output_path.push_back(path.at(path_index));
  }

  // At each iteration, output_path ends with the last valid path segment. We
  // plan between that segment and the current one, and then append the current
  // valid segment to maintain this loop invariant.
  proto::PathPlannerGraphConfig graph_config =
      GetDefaultPathPlannerGraphConfig();
  for (int segment_index = 1; segment_index < valid_path_segments.size();
       segment_index++) {
    const auto& current_segment = valid_path_segments.at(segment_index);
    CHECK(!current_segment.empty());  // Guaranteed by GetValidPathSegments.
    const eigenmath::VectorXd& start = output_path.back();
    const eigenmath::VectorXd& end = path.at(current_segment.front());

    // This should never happen because if start == end, then the segment
    // between them is trivially valid since they themselves are valid.
    CHECK(!start.isApprox(end))
        << "Start: " << toString(start) << ", end: " << toString(end);

    // TODO(b/269342459): We currently only use parts of the graph to avoid long
    // run times and deal with validity checks in potentially dynamic
    // environments.
    DistanceFn distance_fn = [](const eigenmath::VectorXd& a,
                                const eigenmath::VectorXd& b) -> double {
      return (a - b).norm();
    };
    PathPlannerGraph default_graph(
        graph_config, RobotChain(proxy.GetBaseAndTipIds()), distance_fn);

    // The graph if provided for paths or hints. Make compatible with
    // nullptr pipeline.
    PointPath planned_segment;
    if (graph != nullptr) {
      // Planner graph provided. However, because we cannot guarantee the size
      // of the graph being minimal or valid, we check the incoming graph if
      // they provide an initial solution and check it for validity.
      auto start_exists = graph->ExistInGraph(start);
      auto goal_exists = graph->ExistInGraph(end);

      // If start and end do not exist in the graph, check if we can connect to
      // it.
      if (!start_exists.second) {
        INTR_RETURN_IF_ERROR(ConnectToGraphIfValid(
            start, edge_validator, kSquaredConnectingDistance, graph));
        start_exists = graph->ExistInGraph(start);
      }
      if (!goal_exists.second) {
        INTR_RETURN_IF_ERROR(ConnectToGraphIfValid(
            end, edge_validator, kSquaredConnectingDistance, graph));
        goal_exists = graph->ExistInGraph(end);
      }

      // Check if path already exist between start and goal
      if (start_exists.second && goal_exists.second) {
        bool path_hint_valid = true;
        VertexDescriptor start_vertex_descriptor_hint = start_exists.first;
        VertexDescriptor goal_vertex_descriptor_hint = goal_exists.first;
        auto vertex_path = graph->AStarSearch(start_vertex_descriptor_hint,
                                              goal_vertex_descriptor_hint);
        if (vertex_path.size() > 1) {
          // Check if path is valid: Do not assume that the planner graph is
          // correctly collision checked even though the connection is marked as
          // valid.
          INTR_ASSIGN_OR_RETURN(const auto config_path,
                                graph->GetConfigurationVector(vertex_path));
          VertexDescriptor start_vertex_descriptor;
          for (int i = 1; i < config_path.size(); ++i) {
            INTR_ASSIGN_OR_RETURN(
                const bool valid,
                edge_validator(config_path[i - 1], config_path[i]));

            if (!valid) {
              path_hint_valid = false;
            } else {  // Add segment to the new graph
              auto start_node_exists =
                  default_graph.ExistInGraph(config_path[i - 1]);
              if (!start_node_exists.second) {
                INTR_ASSIGN_OR_RETURN(
                    start_vertex_descriptor,
                    default_graph.AddVertex(config_path[i - 1],
                                            proto::PathPlannerGraph::VALID));
              }
              INTR_ASSIGN_OR_RETURN(
                  VertexDescriptor end_vertex_descriptor,
                  default_graph.AddVertex(config_path[i],
                                          proto::PathPlannerGraph::VALID));
              INTR_RETURN_IF_ERROR(default_graph.AddEdge(
                  start_vertex_descriptor, end_vertex_descriptor,
                  proto::PathPlannerGraph::VALID));
              start_vertex_descriptor = end_vertex_descriptor;
            }
          }
          if (path_hint_valid) {
            planned_segment = config_path;
          }
        }
      }
    }
    if (planned_segment.empty()) {
      // Initiate state sampler
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<StateSampler> state_sampler,
          CreateStateSampler(config_.state_sampler_spec(), proxy));

      INTR_ASSIGN_OR_RETURN(
          auto sampling_limits,
          GetSamplingLimitsForPlanningProblem(path, proxy.GetJointLimits()));
      INTR_RETURN_IF_ERROR(state_sampler->SetSamplingLimits(sampling_limits));

      ConstraintManifoldProjector projector =
          CreateConstraintManifoldProjector(proxy);

      INTR_ASSIGN_OR_RETURN(
          planned_segment,
          PlanBidirectionalRrtConnect(start, end, config_.rrt_connect_config(),
                                      point_validator, edge_validator,
                                      std::move(state_sampler), projector,
                                      &default_graph));
    }

    if (planned_segment.empty()) {
      return absl::InternalError(
          "RrtPathPlanner failed: internal RRTConnect failed.");
    }

    // Add the replanned gap between valid segments to the output. Since the
    // start and end are already included by the surrounding segments, they do
    // not need to be added.
    for (int i = 1; i < planned_segment.size() - 1; i++) {
      output_path.push_back(planned_segment.at(i));
    }
    // Now append to the output the valid segment we planned to. This maintains
    // the loop invariant of output_path always ending with the last valid
    // segment.
    for (int i = 0; i < current_segment.size(); i++) {
      output_path.push_back(path.at(current_segment.at(i)));
    }
  }

  return std::vector<PointPath>{output_path};
}

absl::Status RrtConnectPathPlanner::Precompute(
    const std::vector<PointPath>& paths_to_plan,
    const KinematicsSystemProxy& proxy, PathPlannerGraph* graph) const {
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<PathPlanner>> RrtConnectPathPlanner::Create(
    const std::optional<google::protobuf::Any>& any_config) {
  proto::RrtPathPlannerConfig config;
  if (any_config.has_value() && !any_config->UnpackTo(&config)) {
    return absl::InvalidArgumentError("Failed to unpack config.");
  }
  auto planner = std::make_unique<RrtConnectPathPlanner>(config);
  return std::unique_ptr<PathPlanner>(std::move(planner));
}

REGISTER_PATH_PLANNER(RrtConnectPathPlanner, "RrtConnectPathPlanner",
                      RrtConnectPathPlanner::Create);
}  // namespace intrinsic
