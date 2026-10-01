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

#ifndef INTRINSIC_PERCEPTION_SKILLS_CAPTURE_IMAGES_H_
#define INTRINSIC_PERCEPTION_SKILLS_CAPTURE_IMAGES_H_

#include <memory>

#include "absl/status/statusor.h"
#include "google/protobuf/message.h"
#include "intrinsic/perception/cameras/camera.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/util/thread/thread_pool.h"

namespace intrinsic::skills {

class CaptureImages : public SkillInterface {
 public:
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

  CaptureImages();
  ~CaptureImages() override = default;

 private:
  perception::GrpcCamera grpc_camera_;
  ThreadPool thread_pool_;
};

}  // namespace intrinsic::skills

#endif  // INTRINSIC_PERCEPTION_SKILLS_CAPTURE_IMAGES_H_
