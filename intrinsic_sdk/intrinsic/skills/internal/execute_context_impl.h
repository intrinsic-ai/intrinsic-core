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

#ifndef INTRINSIC_SKILLS_INTERNAL_EXECUTE_CONTEXT_IMPL_H_
#define INTRINSIC_SKILLS_INTERNAL_EXECUTE_CONTEXT_IMPL_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "grpcpp/channel.h"
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

// Implementation of ExecuteContext used by the skill service.
class ExecuteContextImpl : public ExecuteContext {
 public:
  ExecuteContextImpl(std::shared_ptr<SkillCanceller> canceller,
                     EquipmentPack equipment,
                     SkillLoggingContext logging_context,
                     motion_planning::MotionPlannerClient motion_planner,
                     world::ObjectWorldClient object_world,

                     GeometryLibrary& geometry_library,

                     std::shared_ptr<grpc::Channel> world_service_channel,
                     std::string context_id)
      : canceller_(canceller),
        equipment_(std::move(equipment)),
        logging_context_(logging_context),
        motion_planner_(std::move(motion_planner)),
        object_world_(std::move(object_world)),

        geometry_library_(geometry_library),

        world_service_channel_(std::move(world_service_channel)),
        context_id_(std::move(context_id)) {}

  absl::string_view context_id() const override { return context_id_; }

  SkillCanceller& canceller() const override { return *canceller_; }

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


  absl::StatusOr<std::shared_ptr<grpc::Channel>> GetWorldChannel()
      const override {
    return world_service_channel_;
  }

 private:
  std::shared_ptr<SkillCanceller> canceller_;
  EquipmentPack equipment_;
  SkillLoggingContext logging_context_;
  motion_planning::MotionPlannerClient motion_planner_;
  world::ObjectWorldClient object_world_;
  GeometryLibrary& geometry_library_;  

  std::shared_ptr<grpc::Channel> world_service_channel_;
  std::string context_id_;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_EXECUTE_CONTEXT_IMPL_H_
