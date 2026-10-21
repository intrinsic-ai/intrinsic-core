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

#include "intrinsic/motion_planning/path_planning/planners/validation.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_error_utils.h"
#include "intrinsic/motion_planning/motion_planner/motion_planning_utils.h"
#include "intrinsic/motion_planning/motion_planner/robot_specification.h"
#include "intrinsic/motion_planning/motion_planner/trajectory_segment.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy_util.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/path_planning/validators.h"
#include "intrinsic/motion_planning/proto/motion_specification_proto_utils.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_error.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_specification.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/motion_planning/skills/motion_planning_error_util.h"
#include "intrinsic/motion_planning/trajectory_planning/topp/path_sample.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_checker.h"
#include "intrinsic/world/collision/collision_context.pb.h"
#include "intrinsic/world/collision/util/make_rule_set.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"

namespace intrinsic {
namespace {

using ::intrinsic_proto::motion_planning::v1::ErrorContext;

absl::string_view kMotionPlanningErrorSuffix = "validation";

absl::Status update_status(absl::Status& status, std::string prepend_message,
                           ErrorContext::Type error_context) {
  intrinsic_proto::motion_planning::v1::MotionPlanningError
      motion_planning_error;
  std::string new_message =
      (prepend_message.empty()
           ? std::string(status.message())
           : absl::StrCat(prepend_message, ";", status.message()));
  absl::Status updated_status = absl::Status(status.code(), new_message);
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (absl::StrContains(type_url, "MotionPlanningError")) {
          static_cast<void>(motion_planning_error.ParseFromString(
              payload));  // TODO: Handle return value
          motion_planning_error.mutable_collision_error()->set_error_context(
              error_context);
          updated_status.SetPayload(type_url,
                                    motion_planning_error.SerializeAsCord());
        } else {
          updated_status.SetPayload(type_url, payload);
        }
      });
  return updated_status;
};
}  // namespace

using intrinsic_proto::motion_planning::v1::MotionSegment;

absl::StatusOr<bool> ValidatePath(
    const EdgeValidator& edge_validator, const PointValidator& point_validator,
    const std::vector<eigenmath::VectorXd>& path) {
  // Edge case.
  if (path.size() == 1) return point_validator(path.front());

  // Check the edges.
  for (int i = 1; i < path.size(); i++) {
    INTR_ASSIGN_OR_RETURN(bool valid_edge,
                          edge_validator(path.at(i - 1), path.at(i)));
    if (!valid_edge) return false;
  }
  return true;
}

absl::Status ValidateWithinLimitsAndValidStartAndEnd(
    const PointPath& path, const KinematicsSystemProxy& proxy) {
  if (path.size() < 2) {
    return absl::InvalidArgumentError(
        "The input path must contain at least two joint configurations.");
  }

  // Early return if initial or goal state are outside of limits.
  INTR_ASSIGN_OR_RETURN(bool is_front_within_limits,
                        proxy.IsWithinLimits(path.front()));
  if (!is_front_within_limits) {
    const std::string error_message = absl::StrFormat(
        "The initial configuration [%s] (in rad) of the path is not "
        "within joint limits.",
        toString(path.front()));
    const auto& joint_positions = path.front();
    return CreateStatusWithJointLimitError(
        error_message, joint_positions, kMotionPlanningErrorSuffix,
        ErrorContext::INITIAL_CONF_VALIDATION,
        absl::StatusCode::kInvalidArgument);
  }
  INTR_ASSIGN_OR_RETURN(bool is_back_within_limits,
                        proxy.IsWithinLimits(path.back()));
  if (!is_back_within_limits) {
    const std::string error_message = absl::StrFormat(
        "The goal configuration [%s] (in rad) of the path is not "
        "within joint limits.",
        toString(path.back()));
    const auto& joint_positions = path.back();
    return CreateStatusWithJointLimitError(
        error_message, joint_positions, kMotionPlanningErrorSuffix,
        ErrorContext::GOAL_CONF_VALIDATION, absl::StatusCode::kInvalidArgument);
  }
  // Check for collisions.
  INTR_RETURN_IF_ERROR(ValidateConfiguration(proxy, path.front()))
      .With([](absl::Status&& status) {
        return update_status(status, "Invalid initial joint configuration.",
                             ErrorContext::INITIAL_STATE_COLLISION);
      });

  INTR_RETURN_IF_ERROR(ValidateConfiguration(proxy, path.back()))
      .With([](absl::Status&& status) {
        return update_status(status, "Invalid goal joint configuration.",
                             ErrorContext::GOAL_STATE_COLLISION);
      });
  return absl::OkStatus();
}

absl::Status ValidateConfiguration(const KinematicsSystemProxy& proxy,
                                   const eigenmath::VectorXd& configuration) {
  CollisionCheckingDebug debug;
  INTR_ASSIGN_OR_RETURN(
      JointConfigurationValidationResult joint_configuration_status,
      proxy.IsValid(configuration, &debug));
  if (!static_cast<bool>(joint_configuration_status)) {
    INTR_ASSIGN_OR_RETURN(std::string collision_debug_string,
                          proxy.PrintCollisionCheckingDebug(debug));
    std::string message =
        absl::StrFormat("Configuration [%s] (in rad) is not valid. Reason: %s.",
                        toString(configuration), collision_debug_string);
    // Clip too long error messages to avoid gRPC issues. This can be removed
    // once we have a more general solution for b/232245718.
    if (message.size() > 3500) {
      message = absl::StrCat(message.substr(0, 3500), "... [",
                             message.size() - 3500, " more characters]");
    }
    return CreateStatusWithCollisionError(
        message, debug, configuration, proxy, kMotionPlanningErrorSuffix,
        absl::StatusCode::kInvalidArgument, ErrorContext::STATE_IN_COLLISION);
  }
  return absl::OkStatus();
}

absl::StatusOr<bool> CheckEdgeWithFunction(
    const eigenmath::VectorXd& start, const eigenmath::VectorXd& end,
    double resolution, const PointValidator& point_validator) {
  int num_steps = 1;
  eigenmath::VectorXd big_step = end - start;
  eigenmath::VectorXd small_step = big_step / 2;
  eigenmath::VectorXd current_spot = start + small_step;

  auto big_step_distance = big_step.norm();
  // Stop checking when you're below the minimum step size threshold.
  while (big_step_distance > resolution) {
    for (int i = 0; i < num_steps; ++i) {
      INTR_ASSIGN_OR_RETURN(bool valid_point, point_validator(current_spot));

      if (!valid_point) {
        return false;
      }

      // Go to the next big step, skipping over points already
      // visited at the previous larger step level.
      current_spot += big_step;
    }

    // Shrink the big step and small step, and repeat.
    big_step = small_step;
    small_step /= 2;
    num_steps *= 2;
    current_spot = start + small_step;
    big_step_distance = big_step.norm();
  }
  return true;
}

absl::StatusOr<std::vector<std::vector<int>>> GetValidPathSegments(
    const PointPath& path, const PointValidator& point_validator,
    const EdgeValidator& edge_validator) {
  // Strategy is to walk the points in the path and accumulate a valid subpath.
  // When an invalid point or edge is found, we store the accumulated subpath
  // and begin constructing a new one.

  // Used as the output.
  std::vector<std::vector<int>> output_subpaths;
  // Used as the accumulator.
  std::vector<int> accumulated_subpath;

  // Loop invariants:
  //   * all points and edges in `accumulated_subpath` are valid
  //   * if `accumulated_subpath` is nonempty, the last point in it will be
  //     path.at(i-1) at the beginning of the loop,
  //   * in iteration i, path.at(i) will either be added to the accumulator or
  //     it will be discarded.
  for (int i = 0; i < path.size(); i++) {
    // If we have a non-empty accumulator we need to figure out if we should
    // push it to the output. This would happen if the current point we are
    // trying to add is invalid, or the edge to the current point is invalid.
    if (!accumulated_subpath.empty()) {
      INTR_ASSIGN_OR_RETURN(
          bool edge_is_valid,
          edge_validator(path.at(accumulated_subpath.back()), path.at(i)));
      if (!edge_is_valid) {
        // Broken edge, we have to push the accumulator, and clear it.
        output_subpaths.push_back(accumulated_subpath);
        accumulated_subpath.clear();
      }
    }

    // Decide whether the current point is valid to use.
    INTR_ASSIGN_OR_RETURN(bool current_point_is_valid,
                          point_validator(path.at(i)));
    if (current_point_is_valid) {
      accumulated_subpath.push_back(i);
    }
  }

  if (!accumulated_subpath.empty()) {
    output_subpaths.push_back(accumulated_subpath);
  }

  return output_subpaths;
}

absl::StatusOr<absl::Status> MakeValidationErrorStatus(
    const InvalidJointConfigurationInfo& invalid_config_info,
    const KinematicsSystemProxy& proxy) {
  const char* error_msg = invalid_config_info.is_edge_collision
                              ? "Edge validation failed."
                              : "Configuration is not valid.";
  const absl::StatusCode error_type = invalid_config_info.is_edge_collision
                                          ? absl::StatusCode::kInternal
                                          : absl::StatusCode::kInvalidArgument;

  CollisionCheckingDebug collision_debug;
  INTR_ASSIGN_OR_RETURN(
      const JointConfigurationValidationResult validation_result,
      proxy.IsValid(invalid_config_info.joint_configuration, &collision_debug));

  if (validation_result.collision_status ==
      MarginPairConflictStatus::kInvalid) {
    const absl::StatusOr<std::string> collision_debug_str =
        proxy.PrintCollisionCheckingDebug(collision_debug);
    return CreateStatusWithCollisionError(
        absl::StrCat(error_msg,
                     collision_debug_str.value_or(" (No collision output)")),
        collision_debug, invalid_config_info.joint_configuration, proxy,
        kMotionPlanningErrorSuffix, error_type,
        ErrorContext::REFINEMENT_VALIDATION);
  }

  if (validation_result.within_limits_status ==
      WithinLimitsStatus::kViolatedLimits) {
    INTR_ASSIGN_OR_RETURN(const JointLimits limits,
                          ToJointLimits(proxy.GetJointLimits()));
    return CreateStatusWithJointLimitError(
        absl::StrCat(error_msg, " Joint limits were violated."),
        invalid_config_info.joint_configuration, kMotionPlanningErrorSuffix,
        ErrorContext::REFINEMENT_VALIDATION, error_type, limits);
  }

  if (validation_result.constraint_satisfaction_status ==
      ConstraintSatisfactionStatus::kViolatedConstraints) {
    return absl::Status(error_type,
                        absl::StrCat(error_msg, " Constraint was violated."));
  }

  return absl::Status(error_type, error_msg);
}

absl::StatusOr<PathValidationResult> ValidatePathSamples(
    absl::Span<const topp::PathSample> path_samples,
    absl::Span<const std::unique_ptr<KinematicsSystemProxy>> proxies,
    const absl::flat_hash_map<std::string, std::vector<int>>&
        path_segments_id_to_proxy_index_map,
    const double collision_check_spacing) {
  INTR_ASSIGN_OR_RETURN(
      const std::vector<InvalidPathSampleInfo> invalid_results,
      FindInvalidPathSamples(path_samples, proxies,
                             path_segments_id_to_proxy_index_map,
                             collision_check_spacing,
                             /*max_invalid_results=*/1));

  if (invalid_results.empty()) {
    return PathValidationResult();
  }

  const InvalidPathSampleInfo& failed_sample = invalid_results.front();
  const int proxy_index = failed_sample.proxy_index;
  const KinematicsSystemProxy& proxy = *proxies[proxy_index];

  LOG(INFO) << absl::StrFormat(
      "Validation failed (proxy %d of %d), (config %d of %d)", proxy_index + 1,
      proxies.size(), failed_sample.invalid_config_info.index + 1,
      path_samples.size());

  INTR_ASSIGN_OR_RETURN(
      const absl::Status validation_status,
      MakeValidationErrorStatus(failed_sample.invalid_config_info, proxy));

  return PathValidationResult{.validation_status = validation_status};
}

absl::StatusOr<std::vector<InvalidPathSampleInfo>> FindInvalidPathSamples(
    absl::Span<const topp::PathSample> path_samples,
    absl::Span<const std::unique_ptr<KinematicsSystemProxy>> proxies,
    const absl::flat_hash_map<std::string, std::vector<int>>&
        path_segments_id_to_proxy_index_map,
    const double collision_check_spacing,
    const std::optional<int> max_invalid_results) {
  if (path_samples.empty()) {
    return absl::InvalidArgumentError("At least one path sample is required.");
  }
  if ((path_samples.size() == 2) && (path_samples[0].q == path_samples[1].q)) {
    // If this is a zero-motion trajectory, assume that it's valid.
    return std::vector<InvalidPathSampleInfo>();
  }
  if (path_segments_id_to_proxy_index_map.empty()) {
    return absl::InvalidArgumentError("The map of path segment IDs is empty.");
  }
  // Check that all proxy_ids in `path_segments_id_to_proxy_index_map` are
  // valid.
  for (const auto& [path_segment_id, proxy_ids] :
       path_segments_id_to_proxy_index_map) {
    for (int proxy_index : proxy_ids) {
      if (proxy_index < 0 || proxy_index >= proxies.size()) {
        return absl::OutOfRangeError(absl::StrCat(
            "Proxy index out of range. Max index is ", proxies.size() - 1,
            ", got ", proxy_index, " for path segment ", path_segment_id, "."));
      }
    }
  }

  // For each proxy p_i, we collect all the contiguous paths that use p_i for
  // validity checks. This will allow us to group validity checks by proxy.
  struct ProxyCheckPath {
    std::vector<eigenmath::VectorXd> configs;
    // This stores stores the index in `path_samples` that corresponds to the
    // first and last elements in `configs`. We can use these to convert an
    // index in `configs` into an index in `path_samples` for error reporting.
    int start_index_in_original_path = -1;
    int last_index_in_original_path = -1;
  };
  struct ProxyCheckBatch {
    std::vector<ProxyCheckPath> paths;
  };
  // There will always be one ProxyCheckBatch per proxy.
  std::vector<ProxyCheckBatch> proxy_check_batches;
  proxy_check_batches.resize(proxies.size());

  // For each `q` in `path_samples`, find out which proxies {p_i,...} we should
  // use to check q's validity. Then, add q to the ProxyCheckBatch for each p_i.
  for (int path_sample_ix = 0; path_sample_ix < path_samples.size();
       ++path_sample_ix) {
    const topp::PathSample& path_sample = path_samples[path_sample_ix];
    if (!path_sample.IsValid()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid path sample in segment ", path_sample.segment_id, "."));
    }
    if (auto it =
            path_segments_id_to_proxy_index_map.find(path_sample.segment_id);
        it != path_segments_id_to_proxy_index_map.end()) {
      const std::vector<int>& proxy_indexes = it->second;
      for (int proxy_index : proxy_indexes) {
        ProxyCheckBatch& batch = proxy_check_batches.at(proxy_index);
        ProxyCheckPath* path = nullptr;
        if (!batch.paths.empty() &&
            batch.paths.back().last_index_in_original_path + 1 ==
                path_sample_ix) {
          // This path sample is a continuation of the last path in the batch.
          path = &batch.paths.back();
        } else {
          // This path sample is the start of a new path in the batch.
          path = &batch.paths.emplace_back();
          path->start_index_in_original_path = path_sample_ix;
        }
        path->configs.push_back(path_sample.q);
        path->last_index_in_original_path = path_sample_ix;
      }
    } else {
      return absl::NotFoundError(absl::StrCat(
          "Invalid path segment ID. Segment ", path_sample.segment_id,
          " is not registered in the given map."));
    }
  }

  // For each ProxyCheckBatch `proxy_check_batch` in `proxy_check_batches`,
  // check the validity of each path of configurations in
  // `proxy_check_batch.paths`.
  INTR_ASSIGN_OR_RETURN(const proto::EdgeValidatorSpecification edge_spec,
                        GetEdgeValidatorSpecification(collision_check_spacing));

  const int max_invalid_results_value =
      max_invalid_results.value_or(std::numeric_limits<int>::max());

  // Store here the collision info of the path samples that are found invalid by
  // any proxy.
  std::vector<InvalidPathSampleInfo> invalid_samples_info;

  // Keeps track of invalid path samples we've found so far. Multiple invalid
  // configurations along the same edge (mapping to the same path sample index)
  // and the same configuration flagged by multiple proxies are counted once.
  std::vector<bool> invalid_path_sample_found(path_samples.size(), false);

  // Loop over every proxy, every batch and every collision found in each batch,
  // to identify unique collision sample indices. Early exit if we reach the max
  // collision limit.
  bool max_collisions_reached = false;
  for (int proxy_id = 0; proxy_id < proxies.size() && !max_collisions_reached;
       ++proxy_id) {
    if (proxies.at(proxy_id) == nullptr) {
      continue;
    }
    const KinematicsSystemProxy& proxy = *proxies[proxy_id];
    const ProxyCheckBatch& proxy_check_batch = proxy_check_batches[proxy_id];
    for (const ProxyCheckPath& proxy_check_path : proxy_check_batch.paths) {
      if (max_collisions_reached) break;

      // Always pass `max_invalid_results` instead of a dynamically reducing
      // limit, e.g. `current_limit = max_invalid_results_value -
      // invalid_samples_info.size()`, otherwise subsequent proxies may
      // early-exit on `current_limit` collisions which may later be flagged
      // as duplicates, thus preventing the proxies to explore the rest of the
      // path and uncover extra collisions.
      INTR_ASSIGN_OR_RETURN(
          const std::vector<InvalidJointConfigurationInfo>
              invalid_configurations_info,
          FindInvalidConfigurationsInPath(proxy, proxy_check_path.configs,
                                          edge_spec, max_invalid_results));

      for (const InvalidJointConfigurationInfo& invalid_config_info :
           invalid_configurations_info) {
        const int global_index = proxy_check_path.start_index_in_original_path +
                                 invalid_config_info.index;

        if (invalid_path_sample_found[global_index]) continue;

        InvalidJointConfigurationInfo config_info_copy = invalid_config_info;
        config_info_copy.index = global_index;

        invalid_samples_info.push_back(InvalidPathSampleInfo{
            .invalid_config_info = config_info_copy, .proxy_index = proxy_id});
        invalid_path_sample_found[global_index] = true;

        if (invalid_samples_info.size() >= max_invalid_results_value) {
          max_collisions_reached = true;
          break;
        }
      }
    }
  }

  // Sort based on index.
  std::sort(invalid_samples_info.begin(), invalid_samples_info.end(),
            [](const InvalidPathSampleInfo& a, const InvalidPathSampleInfo& b) {
              return a.invalid_config_info.index < b.invalid_config_info.index;
            });

  INTR_RET_CHECK_LE(invalid_samples_info.size(), max_invalid_results_value)
      << "FindInvalidConfigurationsInPath: returned more collisions than "
         "max_invalid_results.";

  return invalid_samples_info;
}

absl::StatusOr<CheckLimitsAndCollisionsForPointPathResult>
CheckLimitsAndCollisionsForConfiguration(
    const KinematicsSystemProxy& proxy,
    const eigenmath::VectorXd& configuration) {
  absl::Status validate_result = ValidateConfiguration(proxy, configuration);

  return CheckLimitsAndCollisionsForPointPathResult{
      .is_valid = validate_result.ok(),
      .debug_message = validate_result.ToString()};
}

absl::StatusOr<CheckLimitsAndCollisionsForPointPathResult>
CheckLimitsAndCollisionsForPointPath(const KinematicsSystemProxy& proxy,
                                     const PointPath& path,
                                     const double collision_check_spacing) {
  // Validate the start and the end first.
  absl::Status validate_result =
      ValidateWithinLimitsAndValidStartAndEnd(path, proxy);
  if (!validate_result.ok()) {
    return CheckLimitsAndCollisionsForPointPathResult{
        .is_valid = false, .debug_message = validate_result.ToString()};
  }

  // Now set up and validate the remaining points/edges.
  // Note, we can possibly expose the edge validator spec later if that helps.
  INTR_ASSIGN_OR_RETURN(EdgeValidator edge_validator,
                        DefaultEdgeValidator(proxy, collision_check_spacing));
  // Note, for finely discretized paths, this turns into a linear
  // search rather than doing a binary search pattern that we normally use for
  // an edge. We can fix this if it becomes an issue.
  for (int i = 0; i < path.size() - 1; i++) {
    INTR_ASSIGN_OR_RETURN(auto valid_edge,
                          edge_validator(path.at(i), path.at(i + 1)));
    if (!valid_edge) {
      // Exit on first collision.
      return CheckLimitsAndCollisionsForPointPathResult{
          .is_valid = false,
          .debug_message =
              absl::StrCat("Invalid path segment (most likely due to "
                           "collisions) detected between point ",
                           i, " [", toString(path.at(i)), "] and point ", i + 1,
                           " [", toString(path.at(i + 1)), "].")};
    }
  }

  return CheckLimitsAndCollisionsForPointPathResult{.is_valid = true,
                                                    .debug_message = ""};
}

absl::StatusOr<bool> CheckLimitsAndCollisionsForPathSegments(
    const object_world::ObjectWorld& object_world,
    const object_world::KinematicObject& robot,
    const intrinsic_proto::world::CollisionCheckerConfig&
        collision_checker_config,
    absl::Span<const PathSegment> path_segments,
    const double collision_check_spacing) {
  if (path_segments.empty()) {
    return absl::InvalidArgumentError("Path segments cannot be empty.");
  }
  for (int i = 0; i < path_segments.size(); i++) {
    // Do not set any constraints since it is not stored in the path segment.
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<KinematicsSystemProxy> local_proxy_without_constraint,
        CreateKinematicsProxyWithConfig(object_world, robot, path_segments[i],
                                        collision_checker_config));
    INTR_RETURN_IF_ERROR(local_proxy_without_constraint->SetJointLimits(
        JointLimitsXd::Create(path_segments[i].joint_limits)));
    INTR_ASSIGN_OR_RETURN(
        const CheckLimitsAndCollisionsForPointPathResult result,
        CheckLimitsAndCollisionsForPointPath(
            *local_proxy_without_constraint,
            PointPath(path_segments[i].joint_configurations.begin(),
                      path_segments[i].joint_configurations.end()),
            collision_check_spacing));
    if (!result.is_valid) {
      LOG(INFO) << "Invalid path found in segment " << i << ": "
                << result.debug_message;
      return false;
    }
  }
  return true;
}

PathValidator CreateDefaultPathValidator(const KinematicsSystemProxy& proxy,
                                         const double collision_check_spacing) {
  return [&proxy, collision_check_spacing](
             const PointPath& path) -> absl::StatusOr<bool> {
    INTR_ASSIGN_OR_RETURN(EdgeValidator edge_validator,
                          DefaultEdgeValidator(proxy, collision_check_spacing));
    if (path.size() < 2) {
      return absl::InvalidArgumentError(absl::StrCat(
          "PathValidator requires at least two points in the path, but was "
          "given a path with ",
          path.size(), " points."));
    }
    for (size_t i = 1; i < path.size(); ++i) {
      INTR_ASSIGN_OR_RETURN(bool edge_is_valid,
                            edge_validator(path[i - 1], path[i]));
      if (!edge_is_valid) {
        return false;
      }
    }
    return true;
  };
}

}  // namespace intrinsic
