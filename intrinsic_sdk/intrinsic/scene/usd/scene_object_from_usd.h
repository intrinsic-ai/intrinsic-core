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

#ifndef INTRINSIC_SCENE_USD_SCENE_OBJECT_FROM_USD_H_
#define INTRINSIC_SCENE_USD_SCENE_OBJECT_FROM_USD_H_

#include <pxr/usd/usd/stage.h>

#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"

namespace intrinsic {
namespace usd {

// Converts a UsdStage to an Intrinsic Scene Object. Any geometry will be
// written to storage using the given `geometry_serializer`. This calls
// `PreprocessUsdStage` internally, so it may modify the UsdStage.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromUsdStage(const pxr::UsdStageRefPtr& stage,
                        GeometrySerializer& geometry_serializer);

// Converts a USD file stored in memory to an Intrinsic Scene Object. The
// file_name must have a supported USD suffix: .usd, .usda, .usdc, or .usdz.
// Any geometry will be written to storage using the given `geometry_serializer`
//
// Brief explanation of the supported file types:
// - file.usda: Single USD file stored in text format
// - file.usdc: Single USD file stored in binary format
// - file.usd: Single USD file stored in -either- text or binary (the importer
// auto-detects which one)
// - file.usdz: A self-contained, uncompressed zip-file of the scene. May
// contain multiple USD files that reference each other. The USD lib
// automatically handles resolving references between files..
//
// Docs:
// https://docs.nvidia.com/learn-openusd/latest/stage-setting/usd-file-formats.html
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromUsdFileData(absl::string_view file_name,
                           absl::string_view file_contents,
                           GeometrySerializer& geometry_serializer);

// This function is the same as the above but takes the path of a USD file
// to read (.usd, .usda, .usdc, or .usdz) instead of the file contents.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromUsdFile(absl::string_view file_path,
                       GeometrySerializer& geometry_serializer);

// These are the USD file extensions that we support for
// SceneObjectFromUsdFileData.
// We support:
// - usdz
// - usda
// - usdc
// - usd
// See `SceneObjectFromUsdFileData` for more info.
absl::flat_hash_set<std::string> SupportedUsdExtensions();

namespace internal {

// Modifies the USD stage to put it in a form that can be easily parsed by
// us. Current operations:
// 1. If there are no rigid bodies in the scene, it adds a
// toplevel one so that we can still parse the file's geometry.
//
// This is called internally by the `SceneObjectFrom...` functions and does not
// need to be called manually.
absl::Status PreprocessUsdStage(const pxr::UsdStageRefPtr& stage);

}  // namespace internal

}  // namespace usd
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_USD_SCENE_OBJECT_FROM_USD_H_
