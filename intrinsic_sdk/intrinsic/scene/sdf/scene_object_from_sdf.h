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

// Functions for converting SDFormat (SDF) models, worlds, and files to
// Intrinsic `SceneObject` protos.
//
// SDF `<visual><material>` properties are folded into the generated renderable
// rather than stored as `Geometry.material_overrides`.

#ifndef INTRINSIC_SCENE_SDF_SCENE_OBJECT_FROM_SDF_H_
#define INTRINSIC_SCENE_SDF_SCENE_OBJECT_FROM_SDF_H_

#include <optional>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.pb.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/object_properties.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/scene/sdf/sim_spec_from_sdf.h"
#include "sdf/Model.hh"
#include "sdf/Root.hh"
#include "sdf/World.hh"

namespace intrinsic {
namespace scene_object {

// Options for converting a SDF file to a scene object.
struct SceneObjectFromSdfOptions {
  // File descriptor set to convert user_data from its text representation.
  google::protobuf::FileDescriptorSet user_data_fds = {};
};

// Converts a SDFormat Root to an Intrinsic Scene Object. A
// `geometry_serializer` is required to store the geometry in callers's
// preferred storage.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfRoot(const ::sdf::Root& sdf_root,
                       const sdf::UriResolver& uri_resolver,
                       GeometrySerializer& geometry_serializer,
                       const SceneObjectFromSdfOptions& options = {});

// Converts a SDFormat World to an Intrinsic Scene Object. A
// `geometry_serializer` is required to store the geometry in callers's
// preferred storage.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfWorld(const ::sdf::World& sdf_world,
                        const sdf::UriResolver& uri_resolver,
                        GeometrySerializer& geometry_serializer,
                        const SceneObjectFromSdfOptions& options = {});

// Converts a SDFormat Model to an Intrinsic Scene Object. A
// `geometry_serializer` is required to store the geometry in callers's
// preferred storage.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfModel(const ::sdf::Model& sdf_model,
                        const sdf::UriResolver& uri_resolver,
                        GeometrySerializer& geometry_serializer,
                        const SceneObjectFromSdfOptions& options = {});

// Creates a scene object based on `sdf_file`. Only SDF files with a single
// <model> or a single <world> with single <model> is supported. Geometries
// found in the sdf file are stored with `geometry_serializer`. URIs with
// "model://", and "file://" schema in `sdf_file` are resolved relative to the
// directory of `sdf_file` in addition to libsdformat's default resolution
// locations.
// Returns an error in any of the following cases:
// - Content of `sdf_file` is not a valid SDF according to
// https://sdformat.org/spec
// - SDF contains more than one <model> or nested <model>
// - SDF <model> to SceneObject conversion fails due to unsupported features,
// invalid data or geometry serialization errors.
absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromSdfFile(absl::string_view sdf_file,
                       const sdf::UriResolver& uri_resolver,
                       GeometrySerializer& geometry_serializer,
                       const SceneObjectFromSdfOptions& options = {});

// Extracts CartesianLimits from the custom sdf tag
// `sdf::kCartesianLimitsCustomElement` in
// intrinsic/scene/sdf/custom_tags.h.
// Returns std::nullopt when the model does not contain Cartesian limits.
// Forwards parsing errors.
absl::StatusOr<
    std::optional<intrinsic_proto::scene_object::v1::CartesianLimits>>
CartesianLimitsFromSdfModel(const ::sdf::Model& model);

// Extracts ik solvers from the custom sdf tag
// `sdf::kIkSolverCustomElement` in
// intrinsic/scene/sdf/custom_tags.h in any order.
// Returns empty set when the model does not contain the tag.
// Forwards parsing errors.
absl::StatusOr<std::vector<intrinsic_proto::scene_object::v1::IkSolver>>
IkSolversFromSdfModel(const ::sdf::Model& model);

// Extracts control frequency from the custom sdf tag
// `sdf::kControlFrequencyHz` in
// intrinsic/scene/sdf/custom_tags.h.
// Returns std::nullopt when the model does not contain the tag.
// Forwards parsing errors.
absl::StatusOr<std::optional<double>> ControlFrequencyHzFromSdfModel(
    const ::sdf::Model& model);

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_SCENE_OBJECT_FROM_SDF_H_
