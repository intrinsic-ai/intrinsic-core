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

#ifndef INTRINSIC_MOTION_PLANNING_SKILLS_PREPLAN_MOTION_H_
#define INTRINSIC_MOTION_PLANNING_SKILLS_PREPLAN_MOTION_H_

#include <memory>

#include "absl/status/statusor.h"
#include "google/protobuf/message.h"
#include "intrinsic/motion_planning/skills/preplan_motion.pb.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"

namespace intrinsic {
namespace skills {

class PreplanMotionSkill : public SkillInterface {
 public:
  static constexpr char kEquipmentSlot[] = "robot";

  static std::unique_ptr<SkillInterface> CreateSkill();
  ~PreplanMotionSkill() override = default;

  // Skill implementation
  absl::StatusOr<intrinsic_proto::skills::Footprint> GetFootprint(
      GetFootprintRequest const& request,
      GetFootprintContext& context) const override;

  absl::StatusOr<std::unique_ptr<google::protobuf::Message>> Execute(
      ExecuteRequest const& request, ExecuteContext& context) override;

  absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> Preview(
      PreviewRequest const& request, PreviewContext& context) override;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SKILLS_PREPLAN_MOTION_H_
