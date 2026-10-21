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

// This file provides some handy utilities for extracting value types used in
// our system from SDF entities.

#ifndef INTRINSIC_SCENE_SDF_SDF_UTIL_H_
#define INTRINSIC_SCENE_SDF_SDF_UTIL_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/material.h"
#include "intrinsic/geometry/shapes/box.h"
#include "intrinsic/geometry/shapes/capsule.h"
#include "intrinsic/geometry/shapes/cylinder.h"
#include "intrinsic/geometry/shapes/ellipsoid.h"
#include "intrinsic/geometry/shapes/mesh_file.h"
#include "intrinsic/geometry/shapes/sphere.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "sdf/Box.hh"
#include "sdf/Capsule.hh"
#include "sdf/Cylinder.hh"
#include "sdf/Element.hh"
#include "sdf/Ellipsoid.hh"
#include "sdf/Geometry.hh"
#include "sdf/Material.hh"
#include "sdf/Mesh.hh"
#include "sdf/Plane.hh"
#include "sdf/SemanticPose.hh"
#include "sdf/Sensor.hh"
#include "sdf/Sphere.hh"
#include "sdf/Types.hh"

namespace intrinsic {
namespace sdf {

// Returns the children of parent with the specified tag.
std::vector<::sdf::ElementConstPtr> GetChildrenByTag(
    const ::sdf::ElementConstPtr& parent, const std::string& tag);

// If parent has exactly one child with the specified tag. Otherwise, returns
// NotFoundError or InvalidArgumentError if there are 0 or >1 children,
// respectively.
absl::StatusOr<::sdf::ElementConstPtr> GetChildWithTag(
    const ::sdf::ElementConstPtr& parent, const std::string& tag);

absl::StatusOr<std::string> GetAttributeAsString(
    const ::sdf::ElementConstPtr& element, const std::string& attribute);

absl::StatusOr<bool> GetAttributeAsBool(const ::sdf::ElementConstPtr& element,
                                        const std::string& attribute);

// ---------------------------------------------------------------------------
// Simple parsing functions
//
// The general principles with these functions is that each function:
// - Takes only a parent ElementConstPtr that provides all of the necessary
//   context (i.e. no other arguments).
// - Expects 0 or 1 instances of the attribute or child element.
// - Returns a single value.
// ---------------------------------------------------------------------------

// Unless documented otherwise, ParseChildAs<>() functions look for exactly
// one child with the specified tag and return the parsed result. If there is
// 0 or 2+ such children, these functions return errors.
template <class T>
absl::StatusOr<T> ParseChildAs(const ::sdf::ElementConstPtr& parent,
                               const std::string& tag) {
  INTR_ASSIGN_OR_RETURN(auto child, GetChildWithTag(parent, tag));

  T default_value = T();
  std::pair<T, bool> ret = child->Get<T>(/*key=*/"", default_value);
  if (!ret.second) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse ", tag, " from SDF."));
  }

  return ret.first;
}

template <>
absl::StatusOr<eigenmath::Vector3d> ParseChildAs<eigenmath::Vector3d>(
    const ::sdf::ElementConstPtr& parent, const std::string& tag);

// Return the element value as a Vector2d.
// Returns an error in case of an invalid value.
absl::StatusOr<eigenmath::Vector2d> ParseVector2(
    const ::sdf::ElementConstPtr& element);

// Return the element value as a Vector3d.
// Returns an error in case of an invalid value.
absl::StatusOr<eigenmath::Vector3d> ParseVector3(
    const ::sdf::ElementConstPtr& element);

// Return the element value as a Vector3d, requiring that the elements are
// positive.
// Returns an error in case of an invalid value.
absl::StatusOr<eigenmath::Vector3d> ParseSize3(
    const ::sdf::ElementConstPtr& element);

// Return the element value as a Pose3d.
// Returns an error in case of an invalid value.
absl::StatusOr<Pose3d> ParsePose3(const ::sdf::ElementConstPtr& element);

// Converts an sdf SemanticPose to an Intrinsic Pose by first resolving the
// semantic_pose to `resolve_to` frame, or to its default resolve_to frame; then
// converts the result pose to an Intrinsic Pose3d. Default resolve_to
// conventions are:
// - for model: resolve to parent model/world frame
// - for link: resolve to parent model frame
// - for joint: resolve to child link frame
// - for link children: resolve to parent link frame
// Returns an error in case of a failed resolution
absl::StatusOr<Pose3d> ParseSemanticPose(
    const ::sdf::SemanticPose& semantic_pose,
    const std::string& resolve_to = "");

// Convert a ::sdf::Box to a intrinsic::shapes::Box based on size
// Returns an error if the box's size has any non positive dimension
absl::StatusOr<intrinsic::shapes::Box> ParseBox(const ::sdf::Box& box_sdf);

// Convert a ::sdf::Cylinder to a intrinsic::shapes::Cylinder based on
// length and radius. Returns an error if the cylinder has non-positive length
// or non-positive radius.
absl::StatusOr<intrinsic::shapes::Cylinder> ParseCylinder(
    const ::sdf::Cylinder& cylinder_sdf);

// Convert a ::sdf::Capsule to a intrinsic::shapes::Capsule based on length
// and radius.
// Returns an error if the Capsule has non-positive length or non-positive
// radius.
absl::StatusOr<intrinsic::shapes::Capsule> ParseCapsule(
    const ::sdf::Capsule& capsule_sdf);

// Convert a ::sdf::Ellipsoid to a intrinsic::shapes::Ellipsoid based on
// radii. Returns an error if the Ellipsoid has non-positive radii.
absl::StatusOr<intrinsic::shapes::Ellipsoid> ParseEllipsoid(
    const ::sdf::Ellipsoid& ellipsoid_sdf);

// Convert a ::sdf::Sphere to a intrinsic::shapes::Sphere based on radius.
// Returns an error if the sphere has non-positive radius.
absl::StatusOr<intrinsic::shapes::Sphere> ParseSphere(
    const ::sdf::Sphere& sphere_sdf);

// Convert a ::sdf::Mesh to a intrinsic::shapes::MeshFile based on given
// url_resolver, taking into account the scale and file of the ::sdf::Mesh
// Returns an error if the mesh file resolved is not found
absl::StatusOr<intrinsic::shapes::MeshFile> ParseMesh(
    const ::sdf::Mesh& mesh_sdf, const UriResolver& uri_resolver,
    bool parse_as_collision);

// Converts a `::sdf::Geometry` to an Intrinsic `TransformedGeometry`.
// If `material` has value, a renderable is generated with the material
// properties. If `material` is not provided and geometry is a mesh file, the
// material in the mesh file is used to generate a renderbale. If
// `parse_as_collision` is false, the conversion returns an error when a
// transparent material is encountered.
absl::StatusOr<std::optional<TransformedGeometry>> ParseGeometry(
    const ::sdf::Geometry& geometry_sdf, const UriResolver& uri_resolver,
    std::optional<intrinsic::Material> material, bool parse_as_collision);

// Convert a ::sdf::Material to a intrinsic::Material
// If a script is specified, try to search for a supported script. Otherwise use
// the stored ambient, diffuse, emissive and specular data directly
// Returns an error if the script specified is not supported
absl::StatusOr<intrinsic::Material> ParseMaterial(
    const ::sdf::Material& material_sdf);

// Gets Intrinsic CartesianLimits from custom tags in the sdf Element.
absl::StatusOr<intrinsic::CartesianLimits> ParseCartesianLimits(
    const ::sdf::ElementConstPtr& element);

// Gets an Intrinsic IkSolver and an optional tip link name from a custom tag in
// the sdf Element.
absl::StatusOr<std::pair<std::string, std::optional<std::string>>>
ParseIkSolver(const ::sdf::ElementConstPtr& element);

// Return the element value as a double, requiring that it is
// non-negative. Returns an error in case of an invalid value.
absl::StatusOr<double> ParseNonNegativeScalar(
    const ::sdf::ElementConstPtr& element);

::sdf::SensorType SensorTypeFromString(absl::string_view sensor_type_str);

std::string GetSensorTypeString(::sdf::SensorType sensor_type);

// Converts SDF errors to an absl internal error status.
absl::Status ToStatus(const ::sdf::Errors& sdf_errors);

// Returns if the name is reserved in SDFormat spec.
bool IsReservedSDFName(absl::string_view name);

}  // namespace sdf
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_SDF_UTIL_H_
