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

#ifndef INTRINSIC_SKILLS_INTERNAL_GET_FOOTPRINT_CONTEXT_IMPL_H_
#define INTRINSIC_SKILLS_INTERNAL_GET_FOOTPRINT_CONTEXT_IMPL_H_

#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/geometry_library.h"  
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/world_object.h"

namespace intrinsic {
namespace skills {

// Implementation of GetFootprintContext as used by the skill service.
class GetFootprintContextImpl : public GetFootprintContext {
 public:
  GetFootprintContextImpl(EquipmentPack equipment,
                          motion_planning::MotionPlannerClient motion_planner,
                          world::ObjectWorldClient object_world

                          ,
                          GeometryLibrary& geometry_library

                          ,
                          std::string context_id)
      : equipment_(std::move(equipment)),
        motion_planner_(std::move(motion_planner)),
        object_world_(std::move(object_world))

        ,
        geometry_library_(geometry_library)

        ,
        context_id_(std::move(context_id)) {}

  absl::string_view context_id() const override { return context_id_; }

  motion_planning::MotionPlannerClient& motion_planner() override {
    return motion_planner_;
  }

  world::ObjectWorldClient& object_world() override { return object_world_; }


  GeometryLibrary& geometry_library() const override {
    return geometry_library_;
  }


  absl::StatusOr<world::KinematicObject> GetKinematicObjectForEquipment(
      absl::string_view equipment_name) override;
  absl::StatusOr<world::WorldObject> GetObjectForEquipment(
      absl::string_view equipment_name) override;
  absl::StatusOr<world::Frame> GetFrameForEquipment(
      absl::string_view equipment_name, absl::string_view frame_name) override;

 private:
  EquipmentPack equipment_;
  motion_planning::MotionPlannerClient motion_planner_;
  world::ObjectWorldClient object_world_;
  GeometryLibrary& geometry_library_;  
  std::string context_id_;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_GET_FOOTPRINT_CONTEXT_IMPL_H_
