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

#include "intrinsic/motion_planning/path_planning/validators.h"

#include <sys/stat.h>

#include <functional>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/motion_planning/path_planning/kinematics_system_proxy.h"
#include "intrinsic/motion_planning/path_planning/path_planner_definitions.h"
#include "intrinsic/motion_planning/path_planning/validators_config.pb.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/collision/collision_types.h"

namespace intrinsic {

namespace {

template <typename ProtoType>
absl::StatusOr<ProtoType> CreateValidatorConfigFromAnyConfig(
    const proto::EdgeValidatorSpecification& spec) {
  ProtoType proto;
  if (!spec.config().UnpackTo(&proto)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to unpack config to specified type. Failure occurred for ",
        spec.name()));
  }
  return proto;
}

using NodeValidator =
    std::function<absl::StatusOr<JointConfigurationValidationResult>(
        const eigenmath::VectorXd&)>;

}  // namespace

absl::Status AddConfigsForBinarySearch(const eigenmath::VectorXd& q1,
                                       const eigenmath::VectorXd& q2,
                                       const double max_step_size,
                                       std::vector<eigenmath::VectorXd>& qs) {
  if (max_step_size <= 0.0) {
    return absl::InvalidArgumentError(
        absl::StrCat("AddConfigsForBinarySearch requires max_step_size > 0 "
                     "(found max_step_size == ",
                     max_step_size, ")"));
  }
  const eigenmath::VectorXd dq = q2 - q1;
  const double dist = dq.norm();
  // The minimum number of equally-spaced points between q1 and q2 where the
  // spacing is <= max_step_size. This count does not include q1 or q2.
  const int points_between = (int)std::ceil(dist / max_step_size) - 1;
  if (points_between <= 0) {
    // q1 and q2 are within max_step_size dist apart. No need to check configs
    // in between.
    return absl::OkStatus();
  }
  // The actual step size we will use to generate the points. step_size <=
  // max_step_size.
  const double step_size = dist / (points_between + 1);
  const eigenmath::VectorXd dq_unit = dq / dist;
  qs.reserve(qs.size() + points_between);

  // We need to generate a binary-search ordering of the points between q1 and
  // q2. Starting with the simple linear ordering of these points, we assign
  // each point an index in the range [1,points_between]. Our first
  // binary-search point is the middle point of that range; we add that point to
  // `qs`, then we create 2 sub-ranges not including that point, and add the
  // sub-ranges to a queue. We repeat this process on each element in the queue
  // until there are no more ranges in the queue (which means we have processed
  // all elements).
  //
  // NOTE(torresl): This is the simplest approach I could find for generating a
  // binary search ordering of a sequence of elements. This problem is
  // equivalent to a breadth-first-traversal of a balanced binary tree (which
  // also requires a queue).
  std::vector<std::pair<int, int>> range_queue;
  range_queue.reserve(points_between);
  range_queue.emplace_back(1, points_between);
  int queue_ix = 0;
  const eigenmath::VectorXd step_vec = dq_unit * step_size;
  while (queue_ix < range_queue.size()) {
    auto [first, last] = range_queue.at(queue_ix);
    ++queue_ix;
    if (first > last) {
      continue;
    }
    const int middle_ix = first + ((last - first) / 2);
    qs.push_back(q1 + step_vec * middle_ix);

    range_queue.emplace_back(first, middle_ix - 1);
    range_queue.emplace_back(middle_ix + 1, last);
  }

  return absl::OkStatus();
}

absl::StatusOr<EdgeValidator> DefaultEdgeValidator(
    const KinematicsSystemProxy& proxy, double collision_resolution) {
  if (collision_resolution <= 0.0) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Default edge validator requires collision_resolution > 0 (found ",
        collision_resolution, ")"));
  }
  auto edge_validator = [&proxy, collision_resolution](
                            const eigenmath::VectorXd& q1,
                            const eigenmath::VectorXd& q2) {
    const NodeValidator node_validator =
        [&proxy](const eigenmath::VectorXd& p_to_check) {
          return proxy.IsValid(p_to_check, /*collision_debug=*/nullptr);
        };

    std::vector<eigenmath::VectorXd> qs;
    qs.push_back(q1);
    qs.push_back(q2);
    absl::Status status =
        AddConfigsForBinarySearch(q1, q2, collision_resolution, qs);
    CHECK_OK(status);
    absl::StatusOr<bool> result = proxy.AreAllValid(qs);
    CHECK_OK(result);
    return result.value();
  };

  return edge_validator;
}

PointValidator DefaultPointValidator(const KinematicsSystemProxy& proxy) {
  PointValidator validator =
      [&proxy](const eigenmath::VectorXd& configuration) {
        if (!proxy.IsWithinLimits(configuration).value()) return false;
        JointConfigurationValidationResult validation_status =
            proxy.IsValid(configuration, /*collision_debug=*/nullptr).value();
        return ((validation_status.collision_status ==
                 MarginPairConflictStatus::kClear) &&
                (validation_status.within_limits_status ==
                 WithinLimitsStatus::kWithinLimits) &&
                (validation_status.constraint_satisfaction_status ==
                 ConstraintSatisfactionStatus::kAllConstraintsSatisfied));
      };
  return validator;
}

absl::StatusOr<EdgeValidator> CreateEdgeValidator(
    const KinematicsSystemProxy& proxy,
    const proto::EdgeValidatorSpecification& spec) {
  bool use_default_spec = false;
  if (spec.name() == "DefaultEdgeValidator") {
    use_default_spec = true;
  } else if (spec.name() == "AllowSoftMarginViolationValidator") {
    LOG(WARNING) << "The \"AllowSoftMarginViolationValidator\" validator spec "
                    "is deprecated. Defaulting to equivalent spec "
                    "\"DefaultEdgeValidator\".";
    use_default_spec = true;
  }
  if (!use_default_spec) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unknown edge validator type: ", spec.name()));
  }
  INTR_ASSIGN_OR_RETURN(
      auto config,
      CreateValidatorConfigFromAnyConfig<proto::EdgeValidatorConfig>(spec));
  return DefaultEdgeValidator(proxy, config.resolution());
}

absl::StatusOr<PointValidator> CreatePointValidator(
    const KinematicsSystemProxy& proxy,
    const proto::PointValidatorSpecification& spec) {
  bool use_default_spec = false;
  if (spec.name() == "DefaultPointValidator") {
    use_default_spec = true;
  } else if (spec.name() == "AllowSoftMarginViolationValidator") {
    LOG(WARNING) << "The \"AllowSoftMarginViolationValidator\" validator spec "
                    "is deprecated. Defaulting to equivalent spec "
                    "\"DefaultPointValidator\".";
    use_default_spec = true;
  } else if (spec.name() == "PreventSoftViolationValidator") {
    return absl::InvalidArgumentError(
        "The \"PreventSoftViolationValidator\" validator spec is not "
        "supported.");
  }
  if (!use_default_spec) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unknown point validator type: ", spec.name()));
  }
  return DefaultPointValidator(proxy);
}

absl::StatusOr<proto::EdgeValidatorSpecification> GetEdgeValidatorSpecification(
    const double resolution) {
  if (resolution <= 0.0) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Resolution must be greater than zero (resolution=%f)", resolution));
  }
  proto::EdgeValidatorConfig config;
  config.set_resolution(resolution);
  proto::EdgeValidatorSpecification spec;
  spec.set_name("DefaultEdgeValidator");
  if (!spec.mutable_config()->PackFrom(config)) {
    return absl::InternalError("Failed to pack EdgeValidatorConfig");
  }
  return spec;
}
proto::PointValidatorSpecification GetDefaultPointValidatorSpecification() {
  proto::PointValidatorSpecification spec;
  spec.set_name("DefaultPointValidator");
  return spec;
}

absl::StatusOr<ValidateConfigPathResult> ValidateConfigPath(
    const KinematicsSystemProxy& proxy,
    absl::Span<const eigenmath::VectorXd> config_path,
    const proto::EdgeValidatorSpecification& spec) {
  INTR_ASSIGN_OR_RETURN(
      const std::vector<InvalidJointConfigurationInfo> invalid_results,
      FindInvalidConfigurationsInPath(proxy, config_path, spec,
                                      /*max_invalid_results=*/1));

  if (invalid_results.empty()) {
    return ValidateConfigPathResult{.valid = true};
  }

  return ValidateConfigPathResult{
      .valid = false, .invalid_config_info = invalid_results.front()};
}

absl::StatusOr<std::vector<InvalidJointConfigurationInfo>>
FindInvalidConfigurationsInPath(
    const KinematicsSystemProxy& proxy,
    absl::Span<const eigenmath::VectorXd> config_path,
    const proto::EdgeValidatorSpecification& spec,
    const std::optional<int> max_invalid_results) {
  if (config_path.empty()) {
    return std::vector<InvalidJointConfigurationInfo>();
  }
  INTR_ASSIGN_OR_RETURN(
      const proto::EdgeValidatorConfig config,
      CreateValidatorConfigFromAnyConfig<proto::EdgeValidatorConfig>(spec));
  // `qs_to_check` will contain ALL the configurations we check for validity,
  // including configurations BETWEEN the nodes in config_path. If one of these
  // in-between configs is found to be invalid, we need to populate
  // `invalid_index` with an index on `config_path`, not `qs_to_check`. We use
  // `source_path_indices` for this:
  // qs_to_check[ii] corresponds to config_path[source_path_indices[ii]].
  std::vector<eigenmath::VectorXd> qs_to_check;
  std::vector<int> source_path_indices;
  // Add the configurations at the nodes of the path.
  for (int ii = 0; ii < config_path.size(); ++ii) {
    qs_to_check.push_back(config_path[ii]);
    source_path_indices.push_back(ii);
  }

  // Add the configurations between the nodes (required to satisfy
  // config.resolution).
  for (int ii = 0; ii + 1 < config_path.size(); ++ii) {
    // Add the configs between ii and ii+1 to qs_to_check.
    INTR_RETURN_IF_ERROR(
        AddConfigsForBinarySearch(config_path[ii], config_path[ii + 1],
                                  config.resolution(), qs_to_check));
    // We associate all configs between ii and ii+1 with source node ii.
    while (source_path_indices.size() < qs_to_check.size()) {
      source_path_indices.push_back(ii);
    }
  }
  INTR_RET_CHECK_EQ(qs_to_check.size(), source_path_indices.size());

  INTR_ASSIGN_OR_RETURN(
      std::vector<int> invalid_check_indices,
      proxy.FindInvalidConfigurations(qs_to_check, max_invalid_results));

  std::vector<InvalidJointConfigurationInfo> invalid_results;
  invalid_results.reserve(invalid_check_indices.size());
  for (int i = 0; i < invalid_check_indices.size(); ++i) {
    const int invalid_check_index = invalid_check_indices[i];

    // Given an invalid index on `qs_to_check`, find the corresponding index on
    // `config_path` and return that in `invalid_index`.
    INTR_RET_CHECK_GE(invalid_check_index, 0);
    INTR_RET_CHECK_LT(invalid_check_index, source_path_indices.size());
    const int invalid_path_index = source_path_indices.at(invalid_check_index);

    // qs_to_check contains all the nodes of config_path, followed by
    // configurations between the nodes. Therefore, if invalid_check_index >=
    // config_path.size(), we know that the collision was found not on a path
    // node, but at an edge.
    const bool is_edge_collision = invalid_check_index >= config_path.size();

    invalid_results.push_back(InvalidJointConfigurationInfo{
        .is_edge_collision = is_edge_collision,
        .joint_configuration = qs_to_check.at(invalid_check_index),
        .index = invalid_path_index});
  }
  return invalid_results;
}

}  // namespace intrinsic
