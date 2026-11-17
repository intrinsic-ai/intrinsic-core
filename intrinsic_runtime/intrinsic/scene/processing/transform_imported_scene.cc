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

#include "intrinsic/scene/processing/transform_imported_scene.h"

#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/processing/transform_scene_object.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace scene_object {

using ::intrinsic_proto::Quaternion;
using ::intrinsic_proto::Vector3;
using ::intrinsic_proto::scene_object::v1::ImportedScene;

absl::StatusOr<ImportedScene> TransformImportedScene(
    const ImportedScene& scene, const Quaternion& rotation_proto,
    const Vector3& translation_proto) {
  const eigenmath::Quaterniond rotation = FromProto(rotation_proto);
  const eigenmath::Vector3d translation = FromProto(translation_proto);

  INTR_ASSIGN_OR_RETURN(eigenmath::SO3d so3d_rotation,
                        eigenmath::SO3d::FromQuaternion(rotation));

  const Pose3d delta_pose(so3d_rotation.quaternion(), translation);
  if (delta_pose.isApprox(Pose3d::Identity())) {
    return scene;
  }

  ImportedScene transformed_scene = scene;

  if (transformed_scene.has_scene_objects()) {
    for (auto& [so_id, so] :
         *transformed_scene.mutable_scene_objects()->mutable_objects()) {
      INTR_ASSIGN_OR_RETURN(
          so, TransformSceneObject(so, delta_pose),
          _ << absl::Substitute("Failed to transform scene object '$0'.",
                                so_id));
    }
  }

  if (transformed_scene.has_instance_updates()) {
    for (auto& reparent_update :
         *transformed_scene.mutable_instance_updates()->mutable_updates()) {
      if (!reparent_update.has_reparent() ||
          !reparent_update.reparent().has_set_relative_pose()) {
        continue;
      }

      auto& pose_proto = *reparent_update.mutable_reparent()
                              ->mutable_set_relative_pose()
                              ->mutable_new_pose();

      INTR_ASSIGN_OR_RETURN(Pose3d pose, FromProto(pose_proto));

      // Apply conjugation: dP * pose * dP^-1.
      // We apply this uniformly to all updates rather than just the root. This
      // ensures that when the full transform chain (T_0 * T_1 * ... * T_n) is
      // evaluated, the inner translation terms cancel out at every entity,
      // leaving only the desired global offset at the root. This approach keeps
      // the logic frame-agnostic and avoids the need for manual hierarchy
      // traversal.
      const Pose3d transformed_pose = delta_pose * pose * delta_pose.inverse();
      pose_proto = ToProto(transformed_pose);
    }
  }

  return transformed_scene;
}

}  // namespace scene_object
}  // namespace intrinsic
