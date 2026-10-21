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

#ifndef INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_EXPORTER_H_
#define INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_EXPORTER_H_

#include "absl/status/statusor.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/scene/proto/v1/export.pb.h"

namespace intrinsic {

// Exports a SceneObject according to the specified format in ExportRequest.
// Supports SDF export, optionally bundling mesh geometries into a zip archive.
absl::StatusOr<intrinsic_proto::scene_object::v1::ExportResponse>
ExportSceneObject(
    const intrinsic_proto::scene_object::v1::ExportRequest& request,
    const GeometryLibrary& geometry_library);

}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_EXPORTER_H_
