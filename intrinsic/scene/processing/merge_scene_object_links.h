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

#ifndef INTRINSIC_SCENE_PROCESSING_MERGE_SCENE_OBJECT_LINKS_H_
#define INTRINSIC_SCENE_PROCESSING_MERGE_SCENE_OBJECT_LINKS_H_

#include "absl/status/statusor.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic {
namespace scene_object {

// Merges all links in a link-only `scene_object` into a single root link
// entity, transforming each child link's geometries into the root link's
// coordinate frame.
//
// Returns `scene_object` unchanged if it contains only a single root link.
// Returns `absl::InvalidArgumentError` if `scene_object` has no root entity,
// contains any non-link entity (e.g. `Joint`, `Frame`, or `Sensor`), or has an
// invalid or cyclic entity parent chain.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
MergeSceneObjectLinks(
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_PROCESSING_MERGE_SCENE_OBJECT_LINKS_H_
