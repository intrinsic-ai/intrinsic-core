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

#ifndef INTRINSIC_SCENE_SDF_SCENE_OBJECT_TO_SDF_H_
#define INTRINSIC_SCENE_SDF_SCENE_OBJECT_TO_SDF_H_

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "google/protobuf/descriptor.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic {
namespace sdf {

// Options for converting a SceneObject to SDF.
struct SceneObjectToSdfOptions {
  // Path where geometries referenced in the SceneObject will be saved.
  // If empty, no geometries will be saved.
  std::string save_geopath;

  // Deserializer to use for resolving geometry references.
  // Required if `save_geopath` is non-empty.
  std::optional<const GeometryDeserializer*> geometry_deserializer =
      std::nullopt;

  // Whether to serialize SceneObject user_data to SDF raw text within a custom
  // tag.
  bool serialize_user_data;

  // File descriptor set to convert user_data to its text representation.
  // Only considered if `serialize_user_data` is set.
  google::protobuf::FileDescriptorSet user_data_fds = {};
};

// Converts a Scene Object proto to the corresponding SDFormat string.
//
// The resulting SDF will contain a single <model> with all the entities
// converted to their corresponding SDF elements (link, joint, sensor, frame).
//
// Geometries are resolved using the optional `options.geometry_deserializer`
// and are saved to `options.save_geopath` if provided.
absl::StatusOr<std::string> SceneObjectToSdf(
    const intrinsic_proto::scene_object::v1::SceneObject& scene_object,
    const SceneObjectToSdfOptions& options = {});

}  // namespace sdf
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_SCENE_OBJECT_TO_SDF_H_
