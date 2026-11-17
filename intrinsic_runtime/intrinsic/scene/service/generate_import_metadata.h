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

#ifndef INTRINSIC_SCENE_SERVICE_GENERATE_IMPORT_METADATA_H_
#define INTRINSIC_SCENE_SERVICE_GENERATE_IMPORT_METADATA_H_

#include "absl/status/statusor.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"

namespace intrinsic {

// Generate the import metadata proto, for use during SceneObject import. This
// includes mesh stats, etc.
absl::StatusOr<intrinsic_proto::scene_object::v1::ImportResultMetadata>
GenerateImportMetadata(
    const intrinsic_proto::scene_object::v1::ImportedScene& scene,
    const GeometryDeserializer& geo_deserializer);

}  // namespace intrinsic
#endif  // INTRINSIC_SCENE_SERVICE_GENERATE_IMPORT_METADATA_H_
