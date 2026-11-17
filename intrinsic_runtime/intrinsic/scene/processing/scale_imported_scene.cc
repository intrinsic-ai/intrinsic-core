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

#include "intrinsic/scene/processing/scale_imported_scene.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/point.pb.h"
#include "intrinsic/math/proto/quaternion.pb.h"
#include "intrinsic/math/proto/vector3.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/processing/scale_scene_object.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/imported_scene_updates.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace scene_object {

using ::intrinsic_proto::scene_object::v1::ImportedScene;

absl::StatusOr<ImportedScene> ScaleImportedScene(
    const ImportedScene& scene, const intrinsic_proto::Vector3& scale) {
  if (auto s = FromProto(scale); s.x() <= 0 || s.y() <= 0 || s.z() <= 0) {
    return absl::InvalidArgumentError(
        absl::Substitute("To scale a ImportedScene, scale must be all "
                         "positive. Scale $0 is invalid",
                         scale));
  }

  ImportedScene scene_copy = scene;
  if (scene_copy.has_scene_objects()) {
    for (auto& [so_id, so] :
         *scene_copy.mutable_scene_objects()->mutable_objects()) {
      INTR_ASSIGN_OR_RETURN(
          so, scene_object::ScaleSceneObject(so, scale),
          _ << absl::Substitute("Failed to scale scene object '$0'.", so_id));
    }
  }
  if (scene_copy.has_instance_updates()) {
    for (auto& reparent_update :
         *scene_copy.mutable_instance_updates()->mutable_updates()) {
      if (!reparent_update.has_reparent() ||
          !reparent_update.reparent().has_set_relative_pose()) {
        continue;
      }
      auto& pose_proto = *reparent_update.mutable_reparent()
                              ->mutable_set_relative_pose()
                              ->mutable_new_pose();
      INTR_ASSIGN_OR_RETURN(Pose3d pose, FromProto(pose_proto));
      bool has_rotation =
          !pose.quaternion().isApprox(pose.quaternion().Identity());
      bool uniform_scale = scale.x() == scale.y() && scale.x() == scale.z();
      if (has_rotation && !uniform_scale) {
        return absl::InvalidArgumentError(absl::Substitute(
            "Cannot apply scale $0 scene with reparent update "
            "$1. Scaling a pose with rotation non uniformly is not possible.",
            scale, reparent_update));
      }
      const eigenmath::Vector3d scaled_translation =
          pose.translation().cwiseProduct(FromProto(scale));
      *pose_proto.mutable_position() = ToProto(scaled_translation);
    }
  }

  return scene_copy;
}
}  // namespace scene_object
}  // namespace intrinsic
