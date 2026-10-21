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

#ifndef INTRINSIC_SKILLS_INTERNAL_EXECUTE_CONTEXT_VIEW_H_
#define INTRINSIC_SKILLS_INTERNAL_EXECUTE_CONTEXT_VIEW_H_

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/storage/geometry_library.h"  
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_canceller.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_logging_context.h"
#include "intrinsic/world/objects/object_world_client.h"

namespace intrinsic {
namespace skills {

// ExecuteContext that just stores references to the objects it provides.
//
// Can be used when an ExecuteContext is needed and the objects it provides are
// owned by some other object (such as a PreviewContext).
class ExecuteContextView : public ExecuteContext {
 public:
  ExecuteContextView(SkillCanceller& canceller, const EquipmentPack& equipment,
                     const SkillLoggingContext& logging_context,
                     motion_planning::MotionPlannerClient& motion_planner,
                     world::ObjectWorldClient& object_world

                     ,
                     GeometryLibrary& geometry_library

                     ,
                     std::string context_id)
      : canceller_(canceller),
        equipment_(equipment),
        logging_context_(logging_context),
        motion_planner_(motion_planner),
        object_world_(object_world)

        ,
        geometry_library_(geometry_library)

        ,
        context_id_(std::move(context_id)) {}

  absl::string_view context_id() const override { return context_id_; }

  SkillCanceller& canceller() const override { return canceller_; }

  const EquipmentPack& equipment() const override { return equipment_; }

  const SkillLoggingContext& logging_context() const override {
    return logging_context_;
  }

  motion_planning::MotionPlannerClient& motion_planner() override {
    return motion_planner_;
  }

  world::ObjectWorldClient& object_world() override { return object_world_; }


  GeometryLibrary& geometry_library() const override {
    return geometry_library_;
  }


 private:
  SkillCanceller& canceller_;
  const EquipmentPack& equipment_;
  const SkillLoggingContext& logging_context_;
  motion_planning::MotionPlannerClient& motion_planner_;
  world::ObjectWorldClient& object_world_;
  GeometryLibrary& geometry_library_;  
  std::string context_id_;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_EXECUTE_CONTEXT_VIEW_H_
