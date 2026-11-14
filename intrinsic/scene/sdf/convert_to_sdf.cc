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

#include "intrinsic/scene/sdf/convert_to_sdf.h"

#include <functional>
#include <string>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "intrinsic/eigenmath/rotation_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/geometric_transform.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_fingerprint.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/compatibility/io.h"
#include "intrinsic/geometry/internal/io/save_mesh_to_stl_file.h"
#include "intrinsic/geometry/internal/util/scale_shape.h"
#include "intrinsic/geometry/proto/v1/geometry_storage_refs.pb.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/math/almost_equals.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/sdf/custom_tags.h"
#include "intrinsic/scene/sdf/sdf_util.h"
#include "intrinsic/scene/sdf/separators.h"
#include "intrinsic/scene/sdf/xml_utils.h"
#include "intrinsic/scene/user_data_keys.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/full_precision.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/physics_component.pb.h"
#include "intrinsic/world/proto/sensor_component.pb.h"
#include "ortools/base/file.h"
#include "ortools/base/path.h"

namespace intrinsic {
namespace sdf {
namespace {

constexpr char kSdfVisual[] = "visual";
constexpr char kSdfCollision[] = "collision";

// Returns the full path where the file was saved.
//
// If the file named with the same fingerprint already exists, it is assumed
// it has the same content. This is done for performance reasons to avoid
// re-writing meshes (e.g., for large worlds).
absl::StatusOr<std::string> SaveToUniquePath(
    absl::string_view base_dir, absl::string_view fingerprint,
    absl::string_view extension,
    const std::function<absl::Status(const std::string&)>& save_fn) {
  std::string full_path =
      file::JoinPath(base_dir, absl::StrCat(fingerprint, extension));
  if (file::Exists(full_path, file::Defaults()).ok()) {
    return full_path;
  }
  INTR_RETURN_IF_ERROR(save_fn(full_path));
  return full_path;
}

struct HorizontalFovLimits {
  double min;
  double max;
};
absl::StatusOr<HorizontalFovLimits> GetSDFHorizontalFovLimits() {
  static std::optional<std::pair<double, double>> hfov_minmax =
      []() -> std::optional<std::pair<double, double>> {
    ::sdf::ElementPtr elem = std::make_shared<::sdf::Element>();
    if (!::sdf::initFile("camera.sdf", elem)) return std::nullopt;
    ::sdf::ElementPtr horizontal_fov =
        elem->GetElementDescription("horizontal_fov");
    if (horizontal_fov == nullptr) return std::nullopt;
    ::sdf::ParamPtr hfov_param = horizontal_fov->GetValue();
    if (hfov_param == nullptr) return std::nullopt;
    double hfov_min, hfov_max;
    if (std::optional<std::string> min_str = hfov_param->GetMinValueAsString();
        min_str.has_value()) {
      if (!absl::SimpleAtod(*min_str, &hfov_min)) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
    if (std::optional<std::string> max_str = hfov_param->GetMaxValueAsString();
        max_str.has_value()) {
      if (!absl::SimpleAtod(*max_str, &hfov_max)) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
    return std::make_pair(hfov_min, hfov_max);
  }();
  INTR_RET_CHECK(hfov_minmax.has_value())
      << "Failed to determine horizontal fov limits in SDF spec.";
  return HorizontalFovLimits{.min = hfov_minmax->first,
                             .max = hfov_minmax->second};
}

std::string BuildNoiseSdfString(
    const intrinsic_proto::world::SensorComponent::Noise& noise_proto) {
  std::string noise_type;
  switch (noise_proto.type()) {
    case intrinsic_proto::world::SensorComponent::Noise::TYPE_GAUSSIAN:
      noise_type = "gaussian";
      break;
    case intrinsic_proto::world::SensorComponent::Noise::
        TYPE_GAUSSIAN_QUANTIZED:
      noise_type = "gaussian_quantized";
      break;
    default:
      noise_type = "none";
      break;
  }

  return absl::Substitute(
      "<noise type='$0'>"
      "<mean>$1</mean>"
      "<stddev>$2</stddev>"
      "<bias_mean>$3</bias_mean>"
      "<bias_stddev>$4</bias_stddev>"
      "<dynamic_bias_stddev>$5</dynamic_bias_stddev>"
      "<dynamic_bias_correlation_time>$6</dynamic_bias_correlation_time>"
      "<precision>$7</precision>"
      "</noise>",
      noise_type, FullPrecision(noise_proto.mean()),
      FullPrecision(noise_proto.stddev()),
      FullPrecision(noise_proto.bias_mean()),
      FullPrecision(noise_proto.bias_stddev()),
      FullPrecision(noise_proto.dynamic_bias_stddev()),
      FullPrecision(noise_proto.dynamic_bias_correlation_time()),
      FullPrecision(noise_proto.precision()));
}

bool HasUnitInertia(const eigenmath::Matrix3d& inertia) {
  return (inertia - eigenmath::Matrix3d::Identity()).squaredNorm() < 1e-6;
}

std::string Vector3ProtoToSdf(const intrinsic_proto::Vector3& v) {
  return absl::Substitute("$0 $1 $2", FullPrecision(v.x()),
                          FullPrecision(v.y()), FullPrecision(v.z()));
}

absl::StatusOr<std::string> PrimitiveShapeToSdf(
    const intrinsic_proto::geometry::v1::PrimitiveShape& shape) {
  switch (shape.shape_case()) {
    case intrinsic_proto::geometry::v1::PrimitiveShape::kBox:
      return absl::Substitute("<box><size>$0</size></box>",
                              Vector3ProtoToSdf(shape.box().size()));
    case intrinsic_proto::geometry::v1::PrimitiveShape::kSphere:
      return absl::Substitute("<sphere><radius>$0</radius></sphere>",
                              FullPrecision(shape.sphere().radius()));
    case intrinsic_proto::geometry::v1::PrimitiveShape::kCylinder:
      return absl::Substitute(
          "<cylinder><radius>$0</radius><length>$1</length></cylinder>",
          FullPrecision(shape.cylinder().radius()),
          FullPrecision(shape.cylinder().length()));
    case intrinsic_proto::geometry::v1::PrimitiveShape::kCapsule:
      return absl::Substitute(
          "<capsule><radius>$0</radius><length>$1</length></capsule>",
          FullPrecision(shape.capsule().radius()),
          FullPrecision(shape.capsule().length()));
    case intrinsic_proto::geometry::v1::PrimitiveShape::kEllipsoid:
      return absl::Substitute("<ellipsoid><radii>$0</radii></ellipsoid>",
                              Vector3ProtoToSdf(shape.ellipsoid().radii()));
    case intrinsic_proto::geometry::v1::PrimitiveShape::kFrustum:
      return absl::InvalidArgumentError("SDFormat does not support frustum");
    case intrinsic_proto::geometry::v1::PrimitiveShape::SHAPE_NOT_SET:
      return "";
  }
  return "";
}

// Returns SDFormat string for a mesh.
//
// Mesh is saved under the path set in `options`, if provided, as `mesh_name`
// filename. Else, the URI specified via `fallback_uri` is used. This can be
// useful if the client code does not care about actual mesh contents, or they
// can be fetched separately.
absl::StatusOr<std::string> GeometryToMeshSdf(
    const std::string& mesh_name, const Geometry& geometry,
    const eigenmath::Vector3d& scale, const LinkOptions& options,
    const bool is_visual, absl::string_view fallback_uri = "") {
  if (options.save_geopath.empty()) {
    if (fallback_uri.empty()) {
      return "";
    }
    std::string result =
        absl::Substitute("<mesh><uri>$0</uri>", EscapeXml(fallback_uri));
    if (!scale.isApprox(eigenmath::Vector3d::Ones())) {
      absl::StrAppend(&result, "<scale>", Vec3ToString(scale), "</scale>");
    }
    absl::StrAppend(&result, "</mesh>");
    return result;
  }

  if (is_visual) {
    // Prefers renderable for visual geometry.
    if (auto renderable = geometry.GetRenderable(); renderable != nullptr) {
      INTR_ASSIGN_OR_RETURN(
          std::string full_path,
          SaveToUniquePath(
              options.save_geopath, GenerateFingerprint(*renderable), ".glb",
              [&renderable](const std::string& path) {
                return file::SetContents(path, renderable->GetGLBString(),
                                         file::Defaults());
              }));
      std::string result =
          absl::StrCat("<mesh><uri>model://", EscapeXml(full_path), "</uri>");
      if (!scale.isApprox(eigenmath::Vector3d::Ones())) {
        absl::StrAppend(&result, "<scale>", Vec3ToString(scale), "</scale>");
      }
      absl::StrAppend(&result, "</mesh>");
      return result;
    }
  }

  const auto& exact = geometry.GetExactGeometry();
  if (exact.HasMesh()) {
    INTR_ASSIGN_OR_RETURN(auto mesh_ref, exact.GetMesh());
    INTR_ASSIGN_OR_RETURN(const std::string fingerprint,
                          GenerateFingerprint(geometry));
    INTR_ASSIGN_OR_RETURN(
        std::string full_path,
        SaveToUniquePath(options.save_geopath, fingerprint, ".stl",
                         [&mesh_ref](const std::string& path) {
                           return geo::SaveMeshToBinaryStlFile(
                               path, mesh_ref.Value());
                         }));
    std::string result;
    if (!is_visual && options.convex_decomposition_options.enabled) {
      std::string voxel_sdf;
      std::optional<uint32_t> resolution =
          options.convex_decomposition_options.resolution;
      if (exact.options()
              .simulation_convex_decomposition_resolution.has_value()) {
        resolution = exact.options().simulation_convex_decomposition_resolution;
      }
      if (resolution.has_value()) {
        voxel_sdf = absl::Substitute("<voxel_resolution>$0</voxel_resolution>",
                                     *resolution);
      }
      result = absl::Substitute(
          "<mesh "
          "optimization='convex_decomposition'>"
          "<convex_decomposition>"
          "<max_convex_hulls>$0</max_convex_hulls>$1"
          "</convex_decomposition>",
          options.convex_decomposition_options.max_convex_hulls, voxel_sdf);
    } else {
      result = "<mesh>";
    }
    absl::StrAppend(&result, "<uri>model://", EscapeXml(full_path), "</uri>");
    if (!scale.isApprox(eigenmath::Vector3d::Ones())) {
      absl::StrAppend(&result, "<scale>", Vec3ToString(scale), "</scale>");
    }
    absl::StrAppend(&result, "</mesh>");
    return result;
  }
  return "";
}

std::string MaterialToSdf(
    const std::optional<intrinsic_proto::geometry::v1::MaterialProperties>&
        props) {
  if (!props.has_value()) {
    return "";
  }
  std::string color_sdf;
  if (props->has_base_color()) {
    const auto& c = props->base_color();
    float alpha = c.has_alpha() ? c.alpha().value() : 1.0f;
    color_sdf = absl::Substitute(
        "<diffuse>$0 $1 $2 $3</diffuse><ambient>$0 $1 $2 $3</ambient>",
        FullPrecision(c.red()), FullPrecision(c.green()),
        FullPrecision(c.blue()), FullPrecision(alpha));
  }
  std::string pbr_sdf;
  if (props->has_metalness() || props->has_roughness() ||
      props->has_transmission()) {
    pbr_sdf = "<pbr><metal>";
    if (props->has_metalness()) {
      absl::SubstituteAndAppend(&pbr_sdf, "<metalness>$0</metalness>",
                                FullPrecision(props->metalness()));
    }
    if (props->has_roughness()) {
      absl::SubstituteAndAppend(&pbr_sdf, "<roughness>$0</roughness>",
                                FullPrecision(props->roughness()));
    }
    if (props->has_transmission()) {
      absl::SubstituteAndAppend(&pbr_sdf,
                                "<transmission_factor>$0</transmission_factor>",
                                FullPrecision(props->transmission()));
    }
    absl::StrAppend(&pbr_sdf, "</metal></pbr>");
  }

  if (color_sdf.empty() && pbr_sdf.empty()) {
    return "";
  }

  return absl::Substitute("<material>$0 $1</material>", color_sdf, pbr_sdf);
}

// Returns SDFormat xml string for the geometry.
// `name` is the geometry name, unique within the link scope.
// `mesh_name` is the filename for the mesh, if valid. This should be unique
// among all links to avoid name collision.
//
// Mesh de-duplication is currently not supported.
//
// TODO: combine some of these args for better readability.
absl::StatusOr<std::string> ProcessSingleGeometrySetItem(
    const std::string& name, const std::string& mesh_name,
    const std::string& geometry_type, const Geometry& geometry,
    const intrinsic_proto::geometry::v1::GeometricTransform& ref_t_shape_proto,
    const LinkOptions& options, absl::string_view fallback_uri = "") {
  INTR_ASSIGN_OR_RETURN((const auto [ref_t_shape, scale]),
                        geo::GetPoseAndScale(ref_t_shape_proto));

  // Emits material property only for visual geometry.
  const std::string material_sdf =
      geometry_type == kSdfVisual
          ? MaterialToSdf(geometry.material_properties())
          : "";

  // Handles primitives.
  if (const auto& exact = geometry.GetExactGeometry();
      exact.HasPrimitiveShapes() && !exact.GetPrimitiveShapes().empty()) {
    std::string result;
    const auto& primitive_shapes = exact.GetPrimitiveShapes();
    for (int i = 0; i < primitive_shapes.size(); ++i) {
      const auto& shape = primitive_shapes[i];
      std::string prim_name =
          primitive_shapes.size() > 1 ? absl::StrCat(name, "_", i) : name;
      Pose3d combined_pose = ref_t_shape * Pose3d(shape.ref_t_shape());
      std::string pose_sdf;
      if (!combined_pose.isApprox(Pose3d::Identity())) {
        pose_sdf = Pose3ToPoseString(combined_pose);
      }

      geo::PrimitiveShapePtr shape_to_use = shape.shape();
      if (!scale.isApprox(eigenmath::Vector3d::Ones())) {
        INTR_ASSIGN_OR_RETURN(shape_to_use, ScaleShape(shape.shape(), scale));
      }
      INTR_ASSIGN_OR_RETURN(
          intrinsic_proto::geometry::v1::PrimitiveShape shape_proto,
          geo::geometry_details::ToProto(shape_to_use));
      INTR_ASSIGN_OR_RETURN(std::string shape_sdf,
                            PrimitiveShapeToSdf(shape_proto));

      absl::SubstituteAndAppend(
          &result, "<$0 name=\"$1\">$2<geometry>$3</geometry>$4</$0>",
          geometry_type, EscapeXml(prim_name), pose_sdf, shape_sdf,
          material_sdf);
    }
    return result;
  }

  // Handles mesh.
  INTR_ASSIGN_OR_RETURN(
      std::string mesh_sdf,
      GeometryToMeshSdf(mesh_name, geometry, scale, options,
                        geometry_type == kSdfVisual, fallback_uri));
  if (mesh_sdf.empty()) {
    return "";
  }

  std::string pose_sdf;
  if (!ref_t_shape.isApprox(Pose3d::Identity())) {
    pose_sdf = Pose3ToPoseString(ref_t_shape);
  }

  return absl::Substitute("<$0 name=\"$1\">$2<geometry>$3</geometry>$4</$0>",
                          geometry_type, EscapeXml(name), pose_sdf, mesh_sdf,
                          material_sdf);
}

absl::StatusOr<std::string> ProcessGeometrySet(
    const std::string& link_name, const std::string& geometry_type,
    const intrinsic_proto::world::GeometryComponent::GeometrySet& gs,
    const LinkOptions& options) {
  // `GeometrySet` contains deprecated `geometries` field. If this proto was
  // read from an old (pre geo v1) installed SceneObject asset, then
  // `named_geometries` (aka geo v1) may not be set. If it came from a
  // WorldObject, it should have been already migrated to `named_geometries`.
  // Either way, handle this case by converting to v1.

  const GeometryDeserializer* geometry_deserializer =
      options.geometry_deserializer.value_or(nullptr);

  std::string result;
  if (!gs.named_geometries().empty()) {
    std::vector<std::string> names;
    for (const auto& [name, tg] : gs.named_geometries()) {
      names.push_back(name);
    }
    // Sorts for stable output.
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
      const auto& tg = gs.named_geometries().at(name);
      const std::string mesh_name = absl::StrCat(link_name, "_", name);

      std::string fallback_uri;
      if (tg.geometry().has_geo_ref()) {
        const auto& geo_ref = tg.geometry().geo_ref();
        if (geometry_type == kSdfVisual && !geo_ref.renderable_ref().empty()) {
          fallback_uri = geo_ref.renderable_ref();
        } else if (!geo_ref.exact_geometry_ref().empty()) {
          fallback_uri = geo_ref.exact_geometry_ref();
        }
      }

      // If we have a geo_ref but no deserializer, we can only proceed if we
      // have a fallback_uri and we don't need to save the geometry.
      if (!tg.geometry().has_inline_geometry_data() &&
          geometry_deserializer == nullptr) {
        if (fallback_uri.empty()) {
          return absl::InvalidArgumentError(
              "GeometryDeserializer is required for geometry storage refs.");
        }
        // If we have a fallback_uri, we can still generate the mesh SDF.
        // We use a dummy geometry since it won't be used for saving.
        INTR_ASSIGN_OR_RETURN(std::string item_sdf,
                              ProcessSingleGeometrySetItem(
                                  name, mesh_name, geometry_type, Geometry(),
                                  tg.ref_t_shape(), options, fallback_uri));
        absl::StrAppend(&result, item_sdf);
        continue;
      }

      INTR_ASSIGN_OR_RETURN(auto geometry,
                            ToGeometry(tg.geometry(), geometry_deserializer));
      INTR_ASSIGN_OR_RETURN(std::string item_sdf,
                            ProcessSingleGeometrySetItem(
                                name, mesh_name, geometry_type, geometry,
                                tg.ref_t_shape(), options, fallback_uri));
      absl::StrAppend(&result, item_sdf);
    }
  } else {
    // Handles deprecated `geometries` field.
    for (int i = 0; i < gs.geometries_size(); ++i) {
      const auto& g = gs.geometries(i);
      std::string name = absl::StrCat(geometry_type, "_v0_", i);
      std::string mesh_name = absl::StrCat(link_name, "_", name);

      auto geo_options = intrinsic::GeometryOptions::Default();
      if (g.has_options()) {
        if (g.options().has_simulation_convex_decomposition_resolution()) {
          geo_options.simulation_convex_decomposition_resolution =
              g.options().simulation_convex_decomposition_resolution();
        }
      }

      intrinsic_proto::geometry::v1::GeometricTransform gt_proto;
      if (g.has_ref_t_shape_aff()) {
        *gt_proto.mutable_matrix4d() = g.ref_t_shape_aff();
      }

      std::optional<Geometry> geometry = std::nullopt;
      std::string fallback_uri;
      if (g.has_geometry_storage_refs()) {
        const auto& geo_ref = g.geometry_storage_refs();
        if (geometry_type == kSdfVisual && !geo_ref.renderable_ref().empty()) {
          fallback_uri = geo_ref.renderable_ref();
        } else if (!geo_ref.geometry_ref().empty()) {
          fallback_uri = geo_ref.geometry_ref();
        }

        if (geometry_deserializer == nullptr) {
          if (fallback_uri.empty()) {
            return absl::InvalidArgumentError(
                "GeometryDeserializer is required for geometry storage refs.");
          }
          INTR_ASSIGN_OR_RETURN(std::string item_sdf,
                                ProcessSingleGeometrySetItem(
                                    name, mesh_name, geometry_type, Geometry(),
                                    gt_proto, options, fallback_uri));
          absl::StrAppend(&result, item_sdf);
          continue;
        }
        INTR_ASSIGN_OR_RETURN(geometry,
                              geometry_deserializer->GetGeometry(
                                  g.geometry_storage_refs(), geo_options));
      } else if (g.has_primitive_set() &&
                 !g.primitive_set().primitives().empty()) {
        INTR_ASSIGN_OR_RETURN(geometry, geometry_compatibility::ToGeometry(
                                            g.primitive_set().primitives()));
      }

      if (geometry.has_value()) {
        INTR_ASSIGN_OR_RETURN(std::string item_sdf,
                              ProcessSingleGeometrySetItem(
                                  name, mesh_name, geometry_type, *geometry,
                                  gt_proto, options, fallback_uri));
        absl::StrAppend(&result, item_sdf);
      }
    }
  }
  return result;
}

}  // namespace

std::string FixSDFNameForReservedCharacters(absl::string_view name) {
  std::string fixed_name{name};
  int count =
      absl::StrReplaceAll({{kWorldSdfDefaultSeparator, kSdfNamePartsSeparator},
                           {kSdfNameSeparator, kSdfNamePartsSeparator}},
                          &fixed_name);
  LOG_IF(WARNING, count > 0)
      << "Fixed invalid SDF name [" << name << "] to [" << fixed_name << "].";
  return fixed_name;
}

std::string Vec3ToString(eigenmath::Vector3d vec3) {
  return absl::Substitute("$0 $1 $2", FullPrecision(vec3.x()),
                          FullPrecision(vec3.y()), FullPrecision(vec3.z()));
}

std::string Pose3ToPoseString(const Pose3d& pose3,
                              absl::string_view relative_to) {
  if (pose3.isApprox(Pose3d::Identity()) && relative_to.empty()) {
    return "";
  }

  double roll;
  double pitch;
  double yaw;
  eigenmath::QuaternionToRPY(pose3.quaternion().normalized(), &roll, &pitch,
                             &yaw);
  std::string relative_to_str =
      relative_to.empty()
          ? ""
          : absl::Substitute(" relative_to=\"$0\"", EscapeXml(relative_to));
  return absl::Substitute("<pose$0>$1 $2 $3 $4 $5 $6</pose>", relative_to_str,
                          FullPrecision(pose3.translation().x()),
                          FullPrecision(pose3.translation().y()),
                          FullPrecision(pose3.translation().z()),
                          FullPrecision(roll), FullPrecision(pitch),
                          FullPrecision(yaw));
}

absl::StatusOr<std::string> MotionTypeToString(
    intrinsic_proto::world::KinematicsComponent::MotionType type) {
  switch (type) {
    case intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED:
      return "fixed";
    case intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_REVOLUTE:
      return "revolute";
    case intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_PRISMATIC:
      return "prismatic";
    default:
      return intrinsic::InternalErrorBuilder().LogError()
             << "MotionType is invalid.";
  }
}

absl::StatusOr<std::string> CommonCameraPropertiesToString(
    const intrinsic_proto::world::SensorComponent::CommonCameraProperties&
        props,
    absl::string_view sensor_name,
    std::optional<absl::string_view> trigger_topic) {
  std::string result = "<camera>";

  if (trigger_topic.has_value()) {
    CHECK(!trigger_topic->empty());
    absl::StrAppend(&result, "<triggered>1</triggered>", "<trigger_topic>",
                    *trigger_topic, "</trigger_topic>");
  }

  // Check if horizontal fov is in bounds.
  INTR_ASSIGN_OR_RETURN(HorizontalFovLimits hfov_limits,
                        GetSDFHorizontalFovLimits());
  if (props.horizontal_fov() < hfov_limits.min ||
      props.horizontal_fov() > hfov_limits.max) {
    return InvalidArgumentErrorBuilder()
           << "Horizontal fov " << props.horizontal_fov()
           << " is outside of the allowed range in SDF [" << hfov_limits.min
           << ", " << hfov_limits.max << "].";
  }
  absl::StrAppend(&result, "<horizontal_fov>",
                  FullPrecision(props.horizontal_fov()), "</horizontal_fov>");

  const char* format_str = "";
  switch (props.image().format()) {
    case intrinsic_proto::world::SensorComponent::Image::FORMAT_L8:
      format_str = "L8";
      break;
    case intrinsic_proto::world::SensorComponent::Image::FORMAT_R8G8B8:
      format_str = "R8G8B8";
      break;
    case intrinsic_proto::world::SensorComponent::Image::FORMAT_B8G8R8:
      format_str = "B8G8R8";
      break;
    case intrinsic_proto::world::SensorComponent::Image::FORMAT_BAYER_RGGB8:
      format_str = "BAYER_RGGB8";
      break;
    case intrinsic_proto::world::SensorComponent::Image::FORMAT_BAYER_BGGR8:
      format_str = "BAYER_BGGR8";
      break;
    case intrinsic_proto::world::SensorComponent::Image::FORMAT_BAYER_GBRG8:
      format_str = "BAYER_GBRG8";
      break;
    case intrinsic_proto::world::SensorComponent::Image::FORMAT_BAYER_GRBG8:
      format_str = "BAYER_GRBG8";
      break;
    default:
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Image format is unspecified for camera or depth camera "
                "sensor named '"
             << sensor_name << "'.";
  }

  absl::StrAppend(&result, "<image>", "<width>", props.image().width(),
                  "</width>", "<height>", props.image().height(), "</height>",
                  "<format>", format_str, "</format>", "</image>");

  absl::StrAppend(&result, "<clip>", "<near>",
                  FullPrecision(props.clip().near()), "</near>", "<far>",
                  FullPrecision(props.clip().far()), "</far>", "</clip>");

  switch (props.noise().type()) {
    case intrinsic_proto::world::SensorComponent::Noise::TYPE_UNSPECIFIED:
      // Noise is not always used.
      break;
    case intrinsic_proto::world::SensorComponent::Noise::TYPE_GAUSSIAN:
      absl::StrAppend(&result, "<noise>", "<type>gaussian</type>", "<mean>",
                      FullPrecision(props.noise().mean()), "</mean>",
                      "<stddev>", FullPrecision(props.noise().stddev()),
                      "</stddev>", "</noise>");
      break;
    default:
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Noise type " << props.noise().type()
             << "is not supported in camera or depth camera sensor.";
  }
  if (props.has_intrinsics()) {
    absl::StrAppend(&result, "<lens>", "<intrinsics>", "<fx>",
                    FullPrecision(props.intrinsics().fx()), "</fx>", "<fy>",
                    FullPrecision(props.intrinsics().fy()), "</fy>", "<cx>",
                    FullPrecision(props.intrinsics().cx()), "</cx>", "<cy>",
                    FullPrecision(props.intrinsics().cy()), "</cy>",
                    "</intrinsics>", "</lens>");
  }

  absl::StrAppend(
      &result, "<distortion>", "<k1>", FullPrecision(props.distortion().k1()),
      "</k1>", "<k2>", FullPrecision(props.distortion().k2()), "</k2>", "<k3>",
      FullPrecision(props.distortion().k3()), "</k3>", "<p1>",
      FullPrecision(props.distortion().p1()), "</p1>", "<p2>",
      FullPrecision(props.distortion().p2()), "</p2>", "</distortion>");

  absl::StrAppend(&result, "<visibility_mask>", props.visibility_mask(),
                  "</visibility_mask>", "</camera>");
  return result;
}

absl::StatusOr<std::string> ForceTorqueSpecToString(
    const intrinsic_proto::world::SensorComponent::ForceTorque& spec,
    absl::string_view sensor_name) {
  const char* frame_str = "";
  switch (spec.frame()) {
    case intrinsic_proto::world::SensorComponent::ForceTorque::FRAME_CHILD:
      frame_str = "child";
      break;
    case intrinsic_proto::world::SensorComponent::ForceTorque::FRAME_PARENT:
      frame_str = "parent";
      break;
    case intrinsic_proto::world::SensorComponent::ForceTorque::FRAME_SENSOR:
      frame_str = "sensor";
      break;
    default:
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Frame is unspecified for force torque sensor named '"
             << sensor_name << "'.";
  }

  const char* direction_str = "";
  switch (spec.measure_direction()) {
    case intrinsic_proto::world::SensorComponent::ForceTorque::
        MEASURE_DIRECTION_CHILD_TO_PARENT:
      direction_str = "child_to_parent";
      break;
    case intrinsic_proto::world::SensorComponent::ForceTorque::
        MEASURE_DIRECTION_PARENT_TO_CHILD:
      direction_str = "parent_to_child";
      break;
    default:
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Measure direction is unspecified for force torque sensor "
                "named '"
             << sensor_name << "'.";
  }

  std::string result = absl::StrCat("<force_torque>", "<frame>", frame_str,
                                    "</frame>", "<measure_direction>",
                                    direction_str, "</measure_direction>");

  // Read noise parameters from the device spec.
  if (spec.has_force_noise()) {
    absl::StrAppend(
        &result, "<force>", "<x>", BuildNoiseSdfString(spec.force_noise().x()),
        "</x>", "<y>", BuildNoiseSdfString(spec.force_noise().y()), "</y>",
        "<z>", BuildNoiseSdfString(spec.force_noise().z()), "</z>", "</force>");
  }
  if (spec.has_torque_noise()) {
    absl::StrAppend(&result, "<torque>", "<x>",
                    BuildNoiseSdfString(spec.torque_noise().x()), "</x>", "<y>",
                    BuildNoiseSdfString(spec.torque_noise().y()), "</y>", "<z>",
                    BuildNoiseSdfString(spec.torque_noise().z()), "</z>",
                    "</torque>");
  }
  absl::StrAppend(&result, "</force_torque>");
  return result;
}

absl::StatusOr<std::string> LidarSpecToString(
    const intrinsic_proto::world::SensorComponent::Lidar& spec) {
  std::string result =
      absl::StrCat("<lidar>", "<scan>", "<horizontal>", "<samples>",
                   spec.horizontal().samples(), "</samples>", "<resolution>",
                   FullPrecision(spec.horizontal().resolution()),
                   "</resolution>", "<min_angle>",
                   FullPrecision(spec.horizontal().min_angle()), "</min_angle>",
                   "<max_angle>", FullPrecision(spec.horizontal().max_angle()),
                   "</max_angle>", "</horizontal>");

  // <vertical> which is optional.
  if (spec.has_vertical()) {
    absl::StrAppend(&result, "<vertical>", "<samples>",
                    spec.vertical().samples(), "</samples>", "<resolution>",
                    FullPrecision(spec.vertical().resolution()),
                    "</resolution>", "<min_angle>",
                    FullPrecision(spec.vertical().min_angle()), "</min_angle>",
                    "<max_angle>", FullPrecision(spec.vertical().max_angle()),
                    "</max_angle>", "</vertical>");
  }
  absl::StrAppend(&result, "</scan>");

  // <range> and <noise>
  absl::StrAppend(&result, "<range>", "<min>",
                  FullPrecision(spec.range().min_distance()), "</min>", "<max>",
                  FullPrecision(spec.range().max_distance()), "</max>",
                  "<resolution>", FullPrecision(spec.range().resolution()),
                  "</resolution>", "</range>");

  absl::StrAppend(&result, "<noise>", "<type>gaussian</type>", "<mean>",
                  FullPrecision(spec.noise().mean()), "</mean>", "<stddev>",
                  FullPrecision(spec.noise().stddev()), "</stddev>", "</noise>",
                  "</lidar>");
  return result;
}

absl::StatusOr<std::string> InertialSpecToString(
    const intrinsic_proto::world::PhysicsComponent& physics,
    const InertialOptions& options, const InertialProperties& properties) {
  const double mass = physics.mass_kg();
  eigenmath::Matrix3d inertia = eigenmath::Matrix3d::Identity();
  if (physics.has_inertia()) {
    INTR_ASSIGN_OR_RETURN(auto inertia_xd,
                          intrinsic_proto::FromProto(physics.inertia()));
    if (inertia_xd.rows() != 3 || inertia_xd.cols() != 3) {
      return absl::InvalidArgumentError("Invalid inertia matrix");
    }
    inertia = eigenmath::Matrix3d(inertia_xd);
  }

  const bool has_unit_inertia = HasUnitInertia(inertia);
  bool use_auto_inertia = false;
  if (options.enable_auto_inertial) {
    // Set //inertial/@auto to true if a link is non-static, has at least
    // one collision geometry, and the physics component contains
    // default unit inertial values, i.e. mass = 1kg and unit inertia matrix.
    use_auto_inertia = !properties.is_static && properties.has_collision_geo &&
                       has_unit_inertia;
  }
  if (use_auto_inertia) {
    std::string result = "<inertial auto='true'>";
    if (!AlmostEquals(mass, 1.0)) {
      // If mass is not the default value of 1kg, append the <mass> sdf tag to
      // indicate that we want mass based auto inertial calculation. If <mass>
      // does not exist, density will be used for auto inertial calculation
      // instead.
      absl::StrAppend(&result, "<mass>", FullPrecision(mass), "</mass>");
    }
    // Set default density. This is used only if //inertial/@auto is set to
    // true. If not specified, SDF will output a warning.
    absl::StrAppend(&result, "<density>", options.default_density,
                    "</density>");
    absl::StrAppend(&result, "</inertial>");
    return result;
  }

  Pose3d this_T_center_of_mass = Pose3d::Identity();
  if (physics.has_this_t_center_of_mass()) {
    INTR_ASSIGN_OR_RETURN(
        this_T_center_of_mass,
        intrinsic_proto::FromProto(physics.this_t_center_of_mass()));
  }

  double mass_to_use = mass;
  eigenmath::Matrix3d inertia_to_use = inertia;

  // For empty links, auto inertial computation cannot be enabled.
  // Instead, these links should have minimal influence on the physics
  // simulation so set mass and inertia matrix to something small.
  if (options.enable_auto_inertial && !properties.has_collision_geo &&
      has_unit_inertia) {
    mass_to_use = 1e-3;
    inertia_to_use << 1e-6, 0, 0, 0, 1e-6, 0, 0, 0, 1e-6;
  }

  const auto inertia_sdf = [&inertia_to_use]() -> std::string {
    if (HasUnitInertia(inertia_to_use)) {
      return {};
    }
    return absl::Substitute(
        "<inertia><ixx>$0</ixx><ixy>$1</ixy><ixz>$2</ixz>"
        "<iyy>$3</iyy><iyz>$4</iyz><izz>$5</izz></inertia>",
        FullPrecision(inertia_to_use(0, 0)),
        FullPrecision(inertia_to_use(0, 1)),
        FullPrecision(inertia_to_use(0, 2)),
        FullPrecision(inertia_to_use(1, 1)),
        FullPrecision(inertia_to_use(1, 2)),
        FullPrecision(inertia_to_use(2, 2)));
  }();

  const auto mass_sdf = [&mass_to_use]() -> std::string {
    if (AlmostEquals(mass_to_use, 1.0)) {
      return {};
    }

    return absl::Substitute("<mass>$0</mass>", FullPrecision(mass_to_use));
  }();

  return absl::Substitute("<inertial>$0$1$2</inertial>",
                          Pose3ToPoseString(this_T_center_of_mass), inertia_sdf,
                          mass_sdf);
}

absl::StatusOr<std::string> JointSpecToString(
    const intrinsic_proto::world::KinematicsComponent& kinematics,
    const JointProperties& properties,
    const google::protobuf::Map<std::string, std::string>& user_data_map) {
  INTR_ASSIGN_OR_RETURN(const std::string type,
                        MotionTypeToString(kinematics.motion_type()));

  std::string result = absl::StrCat(
      "<joint name='", EscapeXml(properties.joint_name), "' type='", type, "'>",
      "<parent>", EscapeXml(properties.parent_name), "</parent>", "<child>",
      EscapeXml(properties.child_name), "</child>",
      Pose3ToPoseString(properties.child_t_joint, properties.pose_relative_to));

  if (kinematics.motion_type() !=
      intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED) {
    eigenmath::Vector3d axis = eigenmath::Vector3d::UnitZ();
    if (kinematics.has_axis()) {
      axis = intrinsic_proto::FromProto(kinematics.axis());
    }

    double lower = -std::numeric_limits<double>::infinity();
    double upper = std::numeric_limits<double>::infinity();
    if (kinematics.system_limits().has_fixed_limits()) {
      lower = kinematics.system_limits().fixed_limits().lower();
      upper = kinematics.system_limits().fixed_limits().upper();
    }

    absl::StrAppend(&result, "<axis>", "<xyz>", Vec3ToString(axis), "</xyz>",
                    "<limit>", "<lower>", FullPrecision(lower), "</lower>",
                    "<upper>", FullPrecision(upper), "</upper>");

    if (kinematics.system_limits().effort() > 0 &&
        std::isfinite(kinematics.system_limits().effort())) {
      absl::StrAppend(&result, "<effort>",
                      FullPrecision(kinematics.system_limits().effort()),
                      "</effort>");
    }
    if (kinematics.system_limits().velocity() > 0 &&
        std::isfinite(kinematics.system_limits().velocity())) {
      absl::StrAppend(&result, "<velocity>",
                      FullPrecision(kinematics.system_limits().velocity()),
                      "</velocity>");
    }
    if (kinematics.system_limits().acceleration() > 0 &&
        std::isfinite(kinematics.system_limits().acceleration())) {
      absl::SubstituteAndAppend(
          &result, "<$0>$1</$0>", kAccelerationCustomElement,
          FullPrecision(kinematics.system_limits().acceleration()));
    }
    if (kinematics.system_limits().jerk() > 0 &&
        std::isfinite(kinematics.system_limits().jerk())) {
      absl::SubstituteAndAppend(
          &result, "<$0>$1</$0>", kJerkCustomElement,
          FullPrecision(kinematics.system_limits().jerk()));
    }
    absl::StrAppend(&result, "</limit>");

    if (kinematics.damping() > 0 || kinematics.friction() > 0) {
      // Emits dynamics only if they differ from the defaults.
      // https://github.com/gazebosim/sdformat/blob/sdf15/sdf/1.12/joint.sdf#L78-L83
      absl::StrAppend(&result, "<dynamics>", "<damping>",
                      FullPrecision(kinematics.damping()), "</damping>",
                      "<friction>", FullPrecision(kinematics.friction()),
                      "</friction>", "</dynamics>");
    }
    absl::StrAppend(&result, "</axis>");
  }

  // The kGazeboJointPhysics key is deprecated as it is no longer used
  // during the sdf -> scene object conversion process.
  if (auto itr = user_data_map.find(::intrinsic::sdf::kGazeboJointPhysics);
      itr != user_data_map.end()) {
    absl::StrAppend(&result, itr->second);
  }

  // Appends sensors attached to the joint.
  absl::StrAppend(&result, properties.sensors_sdf, "</joint>");
  return result;
}

absl::StatusOr<std::string> LinkSpecToString(
    const intrinsic_proto::world::GeometryComponent& geometry_component,
    const intrinsic_proto::world::PhysicsComponent& physics,
    const LinkProperties& properties, const LinkOptions& options) {
  if (properties.name.empty()) {
    return absl::InvalidArgumentError("Link name cannot be empty.");
  }

  if (!options.save_geopath.empty() &&
      !options.geometry_deserializer.has_value()) {
    return absl::InvalidArgumentError(
        "geometry_deserializer must be set if save_geopath is non-empty.");
  }

  if (const auto& d = options.convex_decomposition_options; d.enabled) {
    if (d.max_convex_hulls <= 0) {
      return intrinsic::InvalidArgumentErrorBuilder()
             << "max_convex_hulls option must be positive, got: "
             << d.max_convex_hulls;
    }

    if (const auto& r = d.resolution; r.has_value() && r.value() <= 0) {
      return intrinsic::InvalidArgumentErrorBuilder()
             << "convex_decomposition_options.resolution option must be "
                "positive, got: "
             << r.value();
    }
  }

  const auto& g = geometry_component.named_geometries();
  const auto visual_it = g.find(kKindVisualGeometry);
  const auto collision_it = g.find(kKindCollisionGeometry);
  bool has_collision_geo = false;
  if (collision_it != g.end()) {
    has_collision_geo = !collision_it->second.named_geometries().empty() ||
                        collision_it->second.geometries_size() > 0;
  }

  const InertialProperties inertial_props = {
      .has_collision_geo = has_collision_geo,
      .is_static = properties.is_static,
  };

  INTR_ASSIGN_OR_RETURN(
      std::string physics_sdf,
      InertialSpecToString(physics, options.inertial_options, inertial_props));

  std::string visual_sdf;
  if (visual_it != g.end()) {
    INTR_ASSIGN_OR_RETURN(visual_sdf,
                          ProcessGeometrySet(properties.name, kSdfVisual,
                                             visual_it->second, options));
  }

  std::string collision_sdf;
  if (collision_it != g.end()) {
    INTR_ASSIGN_OR_RETURN(collision_sdf,
                          ProcessGeometrySet(properties.name, kSdfCollision,
                                             collision_it->second, options));
  }

  return absl::Substitute(
      "<link name=\"$0\">$1 $2 $3 $4 $5</link>", EscapeXml(properties.name),
      Pose3ToPoseString(properties.parent_t_link, properties.parent_name),
      physics_sdf, visual_sdf, collision_sdf, properties.sensors_sdf);
}

}  // namespace sdf
}  // namespace intrinsic
