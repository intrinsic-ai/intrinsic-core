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

#ifndef INTRINSIC_PERCEPTION_SKILLS_CALIBRATION_SAMPLE_CALIBRATION_POSES_H_
#define INTRINSIC_PERCEPTION_SKILLS_CALIBRATION_SAMPLE_CALIBRATION_POSES_H_

#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/proto/footprint.pb.h"

namespace intrinsic {
namespace skills {

// Skill that performs the sampling for hand-eye calibration for a single robot
// and a single camera.
class SampleCalibrationPoses : public SkillInterface {
 public:
  // Constants for required equipment
  static constexpr char kRobotEquipmentSlot[] = "robot";

  static constexpr int kMinNumSamples = 4;

  // Skill implementation
  absl::StatusOr<std::unique_ptr<google::protobuf::Message>> Execute(
      const ExecuteRequest& request, ExecuteContext& context) override;

  absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> Preview(
      const PreviewRequest& request, PreviewContext& context) override;

  absl::StatusOr<intrinsic_proto::skills::Footprint> GetFootprint(
      const GetFootprintRequest& request,
      GetFootprintContext& context) const override;

  // Constructors and factories.
  static std::unique_ptr<SkillInterface> CreateSkill();
  explicit SampleCalibrationPoses(
      std::unique_ptr<icon::ChannelFactory> icon_channel_factory)
      : icon_channel_factory_(std::move(icon_channel_factory)) {}
  ~SampleCalibrationPoses() override = default;

 private:
  // Factory used to instantiate the connection to ICON. This is injected so
  // that tests can use FakeChannelFactory.
  std::unique_ptr<icon::ChannelFactory> icon_channel_factory_;

  template <typename URBG>
  absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
  ExecuteWithRandomEngine(const ExecuteRequest& request,
                          ExecuteContext& context, bool multithreading,
                          URBG&& engine);
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_SKILLS_CALIBRATION_SAMPLE_CALIBRATION_POSES_H_
