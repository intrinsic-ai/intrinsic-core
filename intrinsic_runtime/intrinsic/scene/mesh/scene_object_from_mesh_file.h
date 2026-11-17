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

#ifndef INTRINSIC_SCENE_MESH_SCENE_OBJECT_FROM_MESH_FILE_H_
#define INTRINSIC_SCENE_MESH_SCENE_OBJECT_FROM_MESH_FILE_H_

#include <optional>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic::scene_object {

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromMeshFile(absl::string_view filename,
                        std::optional<double> scale_factor,
                        GeometrySerializer& geo_serializer);

}

#endif  // INTRINSIC_SCENE_MESH_SCENE_OBJECT_FROM_MESH_FILE_H_
