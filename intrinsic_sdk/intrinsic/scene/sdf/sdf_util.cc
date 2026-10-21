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

#include "intrinsic/scene/sdf/sdf_util.h"

#include <array>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "gz/math/Color.hh"
#include "gz/math/Pose3.hh"
#include "gz/math/Vector2.hh"
#include "gz/math/Vector3.hh"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/affine_transform_of_geometry.h"
#include "intrinsic/geometry/api/axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/compute_axis_aligned_bounding_box_3d.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_options.h"
#include "intrinsic/geometry/api/material.h"
#include "intrinsic/geometry/api/shapes.h"
#include "intrinsic/geometry/shapes/box.h"
#include "intrinsic/geometry/shapes/capsule.h"
#include "intrinsic/geometry/shapes/cylinder.h"
#include "intrinsic/geometry/shapes/ellipsoid.h"
#include "intrinsic/geometry/shapes/mesh_file.h"
#include "intrinsic/geometry/shapes/sphere.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/sdf/custom_tags.h"
#include "intrinsic/scene/sdf/sdf_path_resolver.h"
#include "intrinsic/scene/sdf/separators.h"
#include "intrinsic/simulation/gazebo/type_conversion.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/options.h"
#include "sdf/Box.hh"
#include "sdf/Capsule.hh"
#include "sdf/Cylinder.hh"
#include "sdf/Element.hh"
#include "sdf/Ellipsoid.hh"
#include "sdf/Error.hh"
#include "sdf/Geometry.hh"
#include "sdf/Material.hh"
#include "sdf/Mesh.hh"
#include "sdf/Param.hh"
#include "sdf/Plane.hh"
#include "sdf/SemanticPose.hh"
#include "sdf/Sensor.hh"
#include "sdf/Sphere.hh"
#include "sdf/Types.hh"
#include "tinyxml2.h"

namespace intrinsic {
namespace sdf {
namespace {

std::array<float, 4> SdfColorToArray(const ::gz::math::Color& color) {
  return {color.R(), color.G(), color.B(), color.A()};
}

// Some materials from
// https://github.com/gazebosim/gazebo-classic/blob/gazebo11/media/materials/scripts/gazebo.material
// are converted here for backwards compatibility with old SDFs.
std::optional<Material> MaterialScriptNameToMaterial(absl::string_view name) {
  if (name == "Gazebo/Grey") {
    return Material{
        .ambient = {.3, .3, .3, 1.0},
        .diffuse = {.7, .7, .7, 1.0},
        .specular = {0.01, 0.01, 0.01, 1.000000},
        .shininess = 1.500000,
    };
  }
  if (name == "Gazebo/Blue") {
    return Material{
        .ambient = {0, 0, 1, 1},
        .diffuse = {0, 0, 1, 1},
        .specular = {0.1, 0.1, 0.1, 1},
        .shininess = 1,
    };
  }
  if (name == "Gazebo/ZincYellow") {
    return Material{
        .ambient = {0.9725, 0.9529, 0.2078, 1},
        .diffuse = {0.9725, 0.9529, 0.2078, 1},
        .specular = {0.9725, 0.9529, 0.2078, 1},
        .shininess = 1,
    };
  }
  if (name == "Gazebo/Turquoise") {
    return Material{
        .ambient = {0, 1, 1, 1},
        .diffuse = {0, 1, 1, 1},
        .specular = {0.1, 0.1, 0.1, 1},
        .shininess = 1,
    };
  }
  if (name == "Gazebo/Orange") {
    return Material{
        .ambient = {1, 0.5088, 0.0468, 1},
        .diffuse = {1, 0.5088, 0.0468, 1},
        .specular = {0.5, 0.5, 0.5, 1},
        .shininess = 128,
    };
  }
  if (name == "Gazebo/Indigo") {
    return Material{
        .ambient = {0.33, 0.0, 0.5, 1},
        .diffuse = {0.33, 0.0, 0.5, 1},
        .specular = {0.1, 0.1, 0.1, 1},
        .shininess = 1,
    };
  }
  return std::nullopt;
}

template <typename T>
void MaybeParseCustomTagAs(const ::sdf::ElementConstPtr& element,
                           absl::string_view custom_element_tag,
                           T& data_to_load);

template <>
void MaybeParseCustomTagAs<eigenmath::Vector3d>(
    const ::sdf::ElementConstPtr& element, absl::string_view custom_element_tag,
    eigenmath::Vector3d& data_to_load) {
  if (element->HasElement(std::string(custom_element_tag))) {
    auto gz_data =
        element->Get<gz::math::Vector3d>(std::string(custom_element_tag));
    data_to_load = GzToIntrinsic(gz_data);
  }
}

template <>
void MaybeParseCustomTagAs<double>(const ::sdf::ElementConstPtr& element,
                                   absl::string_view custom_element_tag,
                                   double& data_to_load) {
  if (element->HasElement(std::string(custom_element_tag))) {
    data_to_load = element->Get<double>(std::string(custom_element_tag));
  }
}
}  // namespace

std::vector<::sdf::ElementConstPtr> GetChildrenByTag(
    const ::sdf::ElementConstPtr& parent, const std::string& tag) {
  std::vector<::sdf::ElementConstPtr> ret;
  for (::sdf::ElementConstPtr current = parent->FindElement(tag); current;
       current = current->GetNextElement(tag)) {
    ret.emplace_back(current);
  }
  return ret;
}

absl::StatusOr<::sdf::ElementConstPtr> GetChildWithTag(
    const ::sdf::ElementConstPtr& parent, const std::string& tag) {
  auto children = GetChildrenByTag(parent, tag);
  if (children.empty()) {
    return intrinsic::NotFoundErrorBuilder()
           << "parent <" << parent->GetName() << "> element has no \"" << tag
           << "\" children";
  }
  if (children.size() > 1) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "element has " << children.size() << "\"" << tag
           << "\" children, expected exactly 1";
  }
  return children[0];
}

absl::StatusOr<std::string> GetAttributeAsString(
    const ::sdf::ElementConstPtr& element, const std::string& attribute) {
  if (!element->HasAttribute(attribute)) {
    return intrinsic::NotFoundErrorBuilder()
           << "element does not have \"" << attribute << "\" attribute";
  }
  return element->GetAttribute(attribute)->GetAsString();
}

absl::StatusOr<bool> GetAttributeAsBool(const ::sdf::ElementConstPtr& element,
                                        const std::string& attribute) {
  if (!element->HasAttribute(attribute)) {
    return intrinsic::NotFoundErrorBuilder()
           << "element does not have \"" << attribute << "\" attribute";
  }
  ::sdf::ParamPtr param = element->GetAttribute(attribute);
  bool value = false;
  if (!param->Get(value)) {
    return absl::InternalError(absl::Substitute(
        "Failed getting bool value of attribute \"$0\"", attribute));
  }
  return value;
}

template <>
absl::StatusOr<eigenmath::Vector3d> ParseChildAs<eigenmath::Vector3d>(
    const ::sdf::ElementConstPtr& parent, const std::string& tag) {
  INTR_ASSIGN_OR_RETURN(auto child, GetChildWithTag(parent, tag));
  return ParseVector3(child);
}

absl::StatusOr<eigenmath::Vector2d> ParseVector2(
    const ::sdf::ElementConstPtr& element) {
  return GzToIntrinsic(element->Get<gz::math::Vector2d>());
}

absl::StatusOr<eigenmath::Vector3d> ParseVector3(
    const ::sdf::ElementConstPtr& element) {
  return GzToIntrinsic(element->Get<gz::math::Vector3d>());
}

absl::StatusOr<eigenmath::Vector3d> ParseSize3(
    const ::sdf::ElementConstPtr& element) {
  eigenmath::Vector3d result =
      GzToIntrinsic(element->Get<gz::math::Vector3d>());
  if (result[0] <= 0 || result[1] <= 0 || result[2] <= 0) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Invalid scale: " << result;
  }
  return result;
}

absl::StatusOr<Pose3d> ParsePose3(const ::sdf::ElementConstPtr& element) {
  auto [parsed_pose, success] =
      element->Get<gz::math::Pose3d>("", gz::math::Pose3d());
  if (!success) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse pose ", element->ToString("")));
  }
  return GzToIntrinsic(parsed_pose);
}

absl::StatusOr<Pose3d> ParseSemanticPose(
    const ::sdf::SemanticPose& semantic_pose, const std::string& resolve_to) {
  ::gz::math::Pose3d resolved_pose;
  if (const auto errors = semantic_pose.Resolve(resolved_pose, resolve_to);
      !errors.empty()) {
    auto error_builder = DataLossErrorBuilder().LogError();
    error_builder << absl::Substitute(
        "Failed to resolve semantic pose originally relative to '$0', "
        "resolving to '$1' resulted in $2 errors",
        semantic_pose.RelativeTo(), resolve_to, errors.size());

    for (const auto& error : errors) {
      error_builder << absl::Substitute("Error: $0 at xml path $1",
                                        error.Message(),
                                        error.XmlPath().value_or(""));
    }
    return error_builder;
  }
  if (!resolved_pose.IsFinite()) {
    return absl::InvalidArgumentError(
        absl::Substitute("Semantic pose relative to $0 is not finite",
                         semantic_pose.RelativeTo()));
  }
  return GzToIntrinsic(resolved_pose);
}

absl::StatusOr<intrinsic::shapes::Box> ParseBox(const ::sdf::Box& box_sdf) {
  const eigenmath::Vector3d size = GzToIntrinsic(box_sdf.Size());
  if (size.minCoeff() <= 0) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "Smallest dimension of a box should be bigger than zero, got: "
           << size;
  }
  return intrinsic::shapes::Box(size);
}

absl::StatusOr<intrinsic::shapes::Cylinder> ParseCylinder(
    const ::sdf::Cylinder& cylinder_sdf) {
  const double length = cylinder_sdf.Length();
  // Avoids degenerate geometries by verifying that length is strictly positive.
  if (length <= 0.0) {
    return absl::InvalidArgumentError(
        absl::Substitute("<cylinder> has non-positive <length>: $0", length));
  }
  const double radius = cylinder_sdf.Radius();
  // Avoids degenerate geometries by verifying that radius is strictly positive.
  if (radius <= 0.0) {
    return absl::InvalidArgumentError(
        absl::Substitute("<cylinder> has non-positive <radius>: $0", radius));
  }
  return intrinsic::shapes::Cylinder(length, radius);
}

absl::StatusOr<intrinsic::shapes::Capsule> ParseCapsule(
    const ::sdf::Capsule& capsule_sdf) {
  const double length = capsule_sdf.Length();
  // Avoids degenerate geometries by verifying that length is strictly positive.
  if (length <= 0.0) {
    return absl::InvalidArgumentError(
        absl::Substitute("<capsule> has non-positive <length>: $0", length));
  }
  const double radius = capsule_sdf.Radius();
  // Avoids degenerate geometries by verifying that radius is strictly positive.
  if (radius <= 0.0) {
    return absl::InvalidArgumentError(
        absl::Substitute("<capsule> has non-positive <radius>: $0", radius));
  }
  return intrinsic::shapes::Capsule(length, radius);
}

absl::StatusOr<intrinsic::shapes::Ellipsoid> ParseEllipsoid(
    const ::sdf::Ellipsoid& ellipsoid_sdf) {
  const eigenmath::Vector3d radii = GzToIntrinsic(ellipsoid_sdf.Radii());
  // Avoids degenerate geometries by verifying that all radii are strictly
  // positive.
  if (radii.minCoeff() <= 0) {
    return ::intrinsic::InvalidArgumentErrorBuilder()
           << "<ellipsoid> has non positive <radii> " << radii;
  }

  return intrinsic::shapes::Ellipsoid(radii);
}

absl::StatusOr<intrinsic::shapes::Sphere> ParseSphere(
    const ::sdf::Sphere& sphere_sdf) {
  const double radius = sphere_sdf.Radius();
  // Avoids degenerate geometries by verifying that radius is strictly positive.
  if (radius <= 0.0) {
    return absl::InvalidArgumentError(
        absl::Substitute("<sphere> has non-positive <radius>: $0", radius));
  }
  return intrinsic::shapes::Sphere(radius);
}

absl::StatusOr<intrinsic::shapes::MeshFile> ParseMesh(
    const ::sdf::Mesh& mesh_sdf, const UriResolver& uri_resolver,
    bool parse_as_collision) {
  const eigenmath::Vector3d scale = GzToIntrinsic(mesh_sdf.Scale());
  const std::string uri = mesh_sdf.Uri();

  INTR_ASSIGN_OR_RETURN(std::string filename, uri_resolver(uri));

  if (!file::Exists(filename, file::Defaults()).ok()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Mesh file doesn't exist: " << filename;
  }
  // Legacy URDF parses collision geometry mesh files as convex
  // TODO(b/172061561): Clarify if is_convex is still used and meaningful.
  return intrinsic::shapes::MeshFile(filename,
                                     /*is_convex=*/parse_as_collision, scale);
}

absl::StatusOr<std::optional<TransformedGeometry>> ParseGeometry(
    const ::sdf::Geometry& geometry, const UriResolver& uri_resolver,
    std::optional<Material> material, bool parse_as_collision) {
  const bool check_for_transparency = !parse_as_collision;
  switch (geometry.Type()) {
    case ::sdf::GeometryType::EMPTY:
      return std::nullopt;
    case ::sdf::GeometryType::BOX: {
      const auto* box_sdf = geometry.BoxShape();
      INTR_ASSIGN_OR_RETURN(const auto box_intrinsic, ParseBox(*box_sdf));
      return ToGeometry(box_intrinsic, material, check_for_transparency);
    }
    case ::sdf::GeometryType::CYLINDER: {
      const auto* cylinder_sdf = geometry.CylinderShape();
      INTR_ASSIGN_OR_RETURN(const auto cylinder_intrinsic,
                            ParseCylinder(*cylinder_sdf));
      return ToGeometry(cylinder_intrinsic, material, check_for_transparency);
    }
    case ::sdf::GeometryType::SPHERE: {
      const auto* sphere_sdf = geometry.SphereShape();
      INTR_ASSIGN_OR_RETURN(const auto sphere_intrinsic,
                            ParseSphere(*sphere_sdf));
      return ToGeometry(sphere_intrinsic, material, check_for_transparency);
    }
    case ::sdf::GeometryType::PLANE: {
      // Infinite collision geometry is not supported.
      // Reference: https://sdformat.org/spec/1.12/geometry/#geometry_plane
      // TODO(b/213940341): Create a plane data structure in intrinsic::shapes
      // that supports collision as infinite shape.
      return absl::InvalidArgumentError(
          "Plane geometry is not supported. Use box instead.");
    }
    case ::sdf::GeometryType::CAPSULE: {
      const auto* capsule_sdf = geometry.CapsuleShape();
      INTR_ASSIGN_OR_RETURN(const auto capsule_intrinsic,
                            ParseCapsule(*capsule_sdf));
      return ToGeometry(capsule_intrinsic, material, check_for_transparency);
    }
    case ::sdf::GeometryType::ELLIPSOID: {
      const auto* ellipsoid_sdf = geometry.EllipsoidShape();
      INTR_ASSIGN_OR_RETURN(const auto ellipsoid_intrinsic,
                            ParseEllipsoid(*ellipsoid_sdf));
      return ToGeometry(ellipsoid_intrinsic, material, check_for_transparency);
    }
    case ::sdf::GeometryType::MESH: {
      const auto* mesh_sdf = geometry.MeshShape();
      INTR_ASSIGN_OR_RETURN(
          const auto mesh_intrinsic,
          ParseMesh(*mesh_sdf, uri_resolver, parse_as_collision));

      GeometryOptions geo_options = GeometryOptions::Default();
      if (const auto* convex_decomposition = mesh_sdf->ConvexDecomposition();
          convex_decomposition != nullptr) {
        geo_options.simulation_convex_decomposition_resolution =
            convex_decomposition->VoxelResolution();
      }

      INTR_ASSIGN_OR_RETURN(
          auto geo,
          ToGeometry(mesh_intrinsic, material, check_for_transparency,
                     geo_options),
          _ << absl::Substitute("Please validate mesh file '$0'",
                                mesh_sdf->FilePath()));

      // TODO(b/373459500): Consider making this an error.
      if (auto mesh = geo.shape().GetExactGeometry().GetMesh(); mesh.ok()) {
        if (mesh->Value().vertex_count() == 0) {
          LOG(WARNING) << "Mesh(" << mesh_sdf->FilePath()
                       << ") has 0 vertices.";
        }
      }

      INTR_ASSIGN_OR_RETURN(AxisAlignedBoundingBox3d bbox,
                            ComputeAxisAlignedBoundingBox3d(geo.shape()));
      if (bbox.IsEmpty()) {
        LOG(WARNING)
            << "Mesh(" << mesh_sdf->FilePath()
            << ") has an empty bounding box. Is it supposed to be empty?";
      } else if (bbox.GetDiagonal().squaredNorm() > (100 * 100)) {
        LOG(WARNING)
            << "Mesh(" << mesh_sdf->FilePath()
            << ") has a bounding box diagonal greater than 100 meters, "
               "this can point to an issue with scale";
      }

      return std::move(geo);
    }
    case ::sdf::GeometryType::CONE:
    case ::sdf::GeometryType::HEIGHTMAP:
    case ::sdf::GeometryType::POLYLINE: {
      auto child = geometry.Element()->GetFirstElement();

      return intrinsic::UnimplementedErrorBuilder()
             << "unhandled geometry type: <" << child->GetName() << ">";
    }
  }
}

absl::StatusOr<intrinsic::Material> ParseMaterial(
    const ::sdf::Material& material_sdf) {
  intrinsic::Material material_intrinsic;
  if (material_sdf.ScriptName().empty()) {
    material_intrinsic.ambient = SdfColorToArray(material_sdf.Ambient());
    material_intrinsic.diffuse = SdfColorToArray(material_sdf.Diffuse());
    material_intrinsic.emission = SdfColorToArray(material_sdf.Emissive());
    material_intrinsic.specular = SdfColorToArray(material_sdf.Specular());
  } else {
    if (auto material_opt =
            MaterialScriptNameToMaterial(material_sdf.ScriptName())) {
      material_intrinsic = *material_opt;
    } else {
      return intrinsic::UnimplementedErrorBuilder()
             << "Material script named '" << material_sdf.ScriptName()
             << "' is not supported.";
    }
  }

  return material_intrinsic;
}

absl::StatusOr<intrinsic::CartesianLimits> ParseCartesianLimits(
    const ::sdf::ElementConstPtr& element) {
  LOG(INFO) << "Parsing element " << element->ToString("");
  if (element->GetName() != kCartesianLimitsCustomElement) {
    return absl::InvalidArgumentError(
        absl::Substitute("Cannot parse sdf element as CartesianLimits.\n$0",
                         element->ToString("")));
  }

  intrinsic::CartesianLimits cartesian_limits;
  MaybeParseCustomTagAs(element, kMinTranslationalPositionCustomElement,
                        cartesian_limits.min_translational_position);
  MaybeParseCustomTagAs(element, kMaxTranslationalPositionCustomElement,
                        cartesian_limits.max_translational_position);
  MaybeParseCustomTagAs(element, kMinTranslationalVelocityCustomElement,
                        cartesian_limits.min_translational_velocity);
  MaybeParseCustomTagAs(element, kMaxTranslationalVelocityCustomElement,
                        cartesian_limits.max_translational_velocity);
  MaybeParseCustomTagAs(element, kMinTranslationalAccelerationCustomElement,
                        cartesian_limits.min_translational_acceleration);
  MaybeParseCustomTagAs(element, kMaxTranslationalAccelerationCustomElement,
                        cartesian_limits.max_translational_acceleration);
  MaybeParseCustomTagAs(element, kMinTranslationalJerkCustomElement,
                        cartesian_limits.min_translational_jerk);
  MaybeParseCustomTagAs(element, kMaxTranslationalJerkCustomElement,
                        cartesian_limits.max_translational_jerk);
  MaybeParseCustomTagAs(element, kMaxRotationalVelocityCustomElement,
                        cartesian_limits.max_rotational_velocity);
  MaybeParseCustomTagAs(element, kMaxRotationalAccelerationCustomElement,
                        cartesian_limits.max_rotational_acceleration);
  MaybeParseCustomTagAs(element, kMaxRotationalJerkCustomElement,
                        cartesian_limits.max_rotational_jerk);

  return cartesian_limits;
}

absl::StatusOr<std::pair<std::string, std::optional<std::string>>>
ParseIkSolver(const ::sdf::ElementConstPtr& element) {
  LOG(INFO) << "Parsing element " << element->ToString("");

  if (element->GetName() != kIkSolverCustomElement) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Cannot parse sdf element as IkSolver.\n$0", element->ToString("")));
  }
  ::sdf::Errors errors;
  std::string ik_solver = element->Get<std::string>(errors);

  if (!errors.empty()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to parse", kIkSolverCustomElement, "with errors: ",
        absl::StrJoin(errors, ", ",
                      [](std::string* str, const ::sdf::Error& error) {
                        absl::StrAppend(str, error.Message());
                      })));
  }

  if (ik_solver.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat(kIkSolverCustomElement, "is empty"));
  }

  std::optional<std::string> tip_link_name;
  if (element->HasAttribute(std::string(kIkSolverLinkNameAttribute))) {
    tip_link_name =
        element->GetAttribute(std::string(kIkSolverLinkNameAttribute))
            ->GetAsString();
  }

  return std::make_pair(ik_solver, tip_link_name);
}

absl::StatusOr<double> ParseNonNegativeScalar(
    const ::sdf::ElementConstPtr& element) {
  auto value = element->Get<double>();
  if (value < 0) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid value: ", value));
  }
  return value;
}

::sdf::SensorType SensorTypeFromString(absl::string_view sensor_type_str) {
  ::sdf::Sensor sensor;
  sensor.SetType(std::string(sensor_type_str));
  return sensor.Type();
}

std::string GetSensorTypeString(::sdf::SensorType sensor_type) {
  ::sdf::Sensor sensor;
  sensor.SetType(sensor_type);
  return sensor.TypeStr();
}

absl::Status ToStatus(const ::sdf::Errors& sdf_errors) {
  if (sdf_errors.empty()) {
    return absl::OkStatus();
  }
  return absl::InternalError(
      absl::StrJoin(sdf_errors, "\n", absl::StreamFormatter()));
}

// Copied from sdformat/src/Utils.hh since that is not a public header.
bool IsReservedSDFName(absl::string_view name) {
  return name == "world" ||
         (name.size() >= 4 && name.starts_with("__") && name.ends_with("__"));
}

}  // namespace sdf
}  // namespace intrinsic
