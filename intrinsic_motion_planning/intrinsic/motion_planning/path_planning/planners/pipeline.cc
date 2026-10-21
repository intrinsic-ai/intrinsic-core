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

#include "intrinsic/motion_planning/path_planning/planners/pipeline.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/path_planning/path_planner.h"
#include "intrinsic/motion_planning/path_planning/planners/pipeline_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/rrt_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/planners/shortcutter_configs.pb.h"
#include "intrinsic/motion_planning/path_planning/validators.h"
#include "intrinsic/util/proto/get_text_proto.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"

ABSL_FLAG(std::string, pipeline_path_planner_config_path, "",
          "Path to a PipelinePathPlannerConfig pbtxt.");

namespace intrinsic {

namespace {

constexpr double kCollisionCheckSpacing =
    MotionPlannerFlags().default_collision_check_spacing;

proto::PipelinePathPlannerConfig DefaultPipelineConfig() {
  proto::PipelinePathPlannerConfig pipeline_config;
  {
    proto::RrtPathPlannerConfig rrt_config;
    *rrt_config.mutable_edge_validator_spec() =
        GetEdgeValidatorSpecification(kCollisionCheckSpacing).value();
    *rrt_config.mutable_point_validator_spec() =
        GetDefaultPointValidatorSpecification();
    auto* spec = pipeline_config.add_specs();
    spec->set_name("RrtConnectPathPlanner");
    static_cast<void>(spec->mutable_config()->PackFrom(
        rrt_config));  // TODO: Handle return value
  }
  {
    proto::JointShortcutterConfig shortcutter_config;
    shortcutter_config.set_use_binary_search(true);
    *shortcutter_config.mutable_edge_validator_spec() =
        GetEdgeValidatorSpecification(kCollisionCheckSpacing).value();
    *shortcutter_config.mutable_point_validator_spec() =
        GetDefaultPointValidatorSpecification();
    auto* spec = pipeline_config.add_specs();
    spec->set_name("JointShortcutter");
    static_cast<void>(spec->mutable_config()->PackFrom(
        shortcutter_config));  // TODO: Handle return value
  }
  return pipeline_config;
}
}  // namespace

// Cache the config so it is only read once.
intrinsic::proto::PipelinePathPlannerConfig GetDefaultPipelineConfig() {
  static const intrinsic::proto::PipelinePathPlannerConfig* config = []() {
    intrinsic::proto::PipelinePathPlannerConfig* config =
        new intrinsic::proto::PipelinePathPlannerConfig();

    const std::string pipeline_path_planner_config_path =
        absl::GetFlag(FLAGS_pipeline_path_planner_config_path);

    // Use a reasonable default.
    if (pipeline_path_planner_config_path.empty()) {
      *config = DefaultPipelineConfig();
      LOG(INFO) << "No pipeline path planning config was passed. "
                << "Using the following default:\n"
                << *config;
    } else {
      auto status =
          intrinsic::GetTextProto(pipeline_path_planner_config_path, *config);
      if (!status.ok()) {
        LOG(QFATAL)
            << "Failed to parse the specified path planner config. "
            << "If you get a 'Could not find type...' error, this likely means "
            << "that some planner is not being linked into the running binary. "
            << "Error: " << status;
      }
    }

    CHECK(!config->specs().empty());
    return config;
  }();

  return *config;
}

absl::StatusOr<std::vector<std::unique_ptr<PathPlanner>>>
CreatePlanningPipeline(const proto::PipelinePathPlannerConfig& config) {
  std::vector<std::unique_ptr<PathPlanner>> planners;

  for (const auto& planner_spec : config.specs()) {
    std::unique_ptr<PathPlanner> sub_planner;
    if (planner_spec.has_config()) {
      INTR_ASSIGN_OR_RETURN(sub_planner,
                            PathPlannerFactory::Create(planner_spec.name(),
                                                       planner_spec.config()));
    } else {
      INTR_ASSIGN_OR_RETURN(
          sub_planner,
          PathPlannerFactory::Create(planner_spec.name(), std::nullopt));
    }
    planners.emplace_back(std::move(sub_planner));
  }

  return planners;
}

}  // namespace intrinsic
