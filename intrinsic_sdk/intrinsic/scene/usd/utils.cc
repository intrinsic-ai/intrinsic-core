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

#include "intrinsic/scene/usd/utils.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/plug/registry.h>
#include <pxr/base/vt/value.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/property.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usdGeom/imageable.h>

#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/substitute.h"
#include "intrinsic/util/status/ret_check.h"
#include "tools/cpp/runfiles/runfiles.h"

namespace intrinsic::usd {

namespace fs = std::filesystem;
using bazel::tools::cpp::runfiles::Runfiles;

namespace {

// Matrices with a determinant below this magnitude are considered singular and
// cannot be inverted with reasonable numeric accuracy.
constexpr double kSingularMatrixTolerance = 1e-12;

void DumpPrimitiveRecursiveInternal(pxr::UsdPrim prim, int depth,
                                    std::stringstream& ss) {
  std::string indent = std::string(2 * depth, ' ');

  // Name, type, path
  ss << indent << prim.GetName().GetString() << " <"
     << prim.GetTypeName().GetString() << "> @ " << prim.GetPath().GetString()
     << std::endl;

  // Properties (either UsdAttribute or UsdRelationship)
  for (const pxr::UsdProperty& prop : prim.GetProperties()) {
    pxr::UsdAttribute attribute = prop.As<pxr::UsdAttribute>();
    if (attribute.IsValid()) {
      ss << indent << "  - " << prop.GetName().GetString() << " <"
         << attribute.GetTypeName().GetAsToken().GetString() << ">"
         << std::endl;
    }
    pxr::UsdRelationship rel = prop.As<pxr::UsdRelationship>();
    if (rel.IsValid()) {
      ss << indent << "  - " << prop.GetName().GetString() << " [Relationship]"
         << std::endl;
    }
  }

  // Recurse
  for (const pxr::UsdPrim& child : prim.GetChildren()) {
    DumpPrimitiveRecursiveInternal(child, depth + 1, ss);
  }
}

};  // namespace

// Fallback for compilers or build systems that do not define
// BAZEL_CURRENT_REPOSITORY.
#ifndef BAZEL_CURRENT_REPOSITORY
#define BAZEL_CURRENT_REPOSITORY ""
#endif

absl::Status InitializeOpenUsdLibrary() {
  // Register our custom plugins path with the plugin registry.
  // If we do not do this, OpenUSD will throw during runtime.
  LOG(INFO) << "Initializing OpenUSD library";

  std::string error;
  std::unique_ptr<Runfiles> runfiles(
      Runfiles::Create(program_invocation_name, &error));
  if (!runfiles) {
    LOG(ERROR) << "Runfiles::Create failed: " << error;
    return absl::InternalError(error);
  }
  const std::string plugins_path =
      runfiles->Rlocation("openusd/usd", BAZEL_CURRENT_REPOSITORY);
  if (plugins_path.empty() || !fs::exists(plugins_path)) {
    return absl::InternalError(
        "Failed to initialize the OpenUSD library - could not find path to "
        "the required plugin files.");
  }
  // Convert plugins_path to an absolute path, as required by RegisterPlugins().
  std::error_code error_code;
  auto absolute_plugins_path = fs::absolute(plugins_path, error_code);
  if (error_code) {
    return absl::InternalError(absl::Substitute(
        "Failed to initialize the OpenUSD library - could not convert $0 "
        "to an absolute path for resolving plugins: $1.",
        plugins_path, error_code.message()));
  }
  LOG(INFO) << "Registering OpenUSD plugins: " << absolute_plugins_path;
  pxr::PlugRegistry::GetInstance().RegisterPlugins(
      absolute_plugins_path.string());

  return absl::OkStatus();
}

absl::StatusOr<pxr::UsdStageRefPtr> LoadStageFromString(
    const std::string& contents) {
  // Create an anonymous (in-memory) layer
  pxr::SdfLayerRefPtr root_layer = pxr::SdfLayer::CreateAnonymous();
  if (!root_layer) {
    return absl::InternalError("Failed to create anonymous SdfLayer");
  }

  // Import the string data into the layer
  bool success = root_layer->ImportFromString(contents);
  if (!success) {
    return absl::InternalError("Failed to import the USD string to the layer");
  }

  auto stage = pxr::UsdStage::Open(root_layer);
  if (!stage) {
    return absl::InternalError("Failed to open a UsdStage with the layer");
  }

  return stage;
}

absl::StatusOr<pxr::UsdPrim> GetRootPrim(const pxr::UsdStageRefPtr& stage) {
  const pxr::UsdPrim default_prim = stage->GetDefaultPrim();
  if (default_prim.IsValid()) {
    return default_prim;
  }

  // No default prim found. Fall back to using the pseudo-root prim. This prim
  // is the parent of all other prims in the stage.
  const pxr::UsdPrim pseudo_root_prim = stage->GetPseudoRoot();
  if (!pseudo_root_prim.IsValid()) {
    // Should not happen. A valid Stage should always have a pseudo-root prim.
    return absl::InvalidArgumentError(
        "Invalid Stage - no pseudo root prim could be found.");
  }

  return pseudo_root_prim;
}

eigenmath::Vector3d ConvertVector(const pxr::GfVec3d& vec) {
  return {vec[0], vec[1], vec[2]};
}

eigenmath::Vector3f ConvertVector(const pxr::GfVec3f& vec) {
  return {vec[0], vec[1], vec[2]};
}

eigenmath::Quaterniond ConvertQuat(const pxr::GfQuatd& usd_quat) {
  return {
      usd_quat.GetReal(),
      usd_quat.GetImaginary()[0],
      usd_quat.GetImaginary()[1],
      usd_quat.GetImaginary()[2],
  };
}

eigenmath::Quaternionf ConvertQuat(const pxr::GfQuatf& usd_quat) {
  return {
      usd_quat.GetReal(),
      usd_quat.GetImaginary()[0],
      usd_quat.GetImaginary()[1],
      usd_quat.GetImaginary()[2],
  };
}

eigenmath::Matrix4d ConvertMatrix4d(const pxr::GfMatrix4d& usd_matrix) {
  // GfMatrix4d is row-major and our eigenmath::Matrix4d is column-major here.
  // Passing the row-major data to this column-major constructor works fine
  // because we want to do the transpose anyways.
  return eigenmath::Matrix4d(usd_matrix.GetArray());
}

absl::StatusOr<intrinsic::Pose3d> ExtractPose(
    const pxr::GfMatrix4d& transform) {
  // Factor transform to check for scale and sheer.
  pxr::GfMatrix4d scale_orient;
  pxr::GfVec3d scale;
  pxr::GfMatrix4d factored_rotation;
  pxr::GfVec3d translation;
  pxr::GfMatrix4d projection;
  INTR_RET_CHECK(transform.Factor(&scale_orient, &scale, &factored_rotation,
                                  &translation, &projection))
      << "Found a transform in an unsupported format. Please fix the transform "
         "so that it represents a 3D rotation and translation: "
      << transform;
  INTR_RET_CHECK(factored_rotation.HasOrthogonalRows3())
      << "Found a transform with shear applied, which is not supported. Please "
         "remove the shear from the transform: "
      << transform;
  INTR_RET_CHECK_LE((scale - pxr::GfVec3d(1.0)).GetLengthSq(), 1e-12)
      << "Found a transform with a non-unit scaling of " << scale
      << ", which is not supported. Please remove the scaling from the "
         "transform: "
      << transform;

  return Pose3d(ConvertQuat(transform.ExtractRotationQuat()).normalized(),
                ConvertVector(translation));
}

intrinsic::Pose3d CreatePose(const pxr::GfQuatf& rotation,
                             const pxr::GfVec3f& position) {
  return Pose3d(ConvertQuat(rotation).cast<double>().normalized(),
                ConvertVector(position).cast<double>());
}

absl::StatusOr<intrinsic::Pose3d> GetWorldPose(
    const pxr::UsdPrim& prim, pxr::UsdGeomXformCache& xform_cache) {
  pxr::GfMatrix4d world_transform = xform_cache.GetLocalToWorldTransform(prim);
  INTR_ASSIGN_OR_RETURN(Pose3d world_pose, ExtractPose(world_transform),
                        _ << absl::Substitute("while getting the pose of '$0'",
                                              prim.GetPath().GetString()));
  return world_pose;
}

absl::StatusOr<intrinsic::Pose3d> GetRelativePose(
    const pxr::UsdPrim& prim1, const pxr::UsdPrim& prim2,
    pxr::UsdGeomXformCache& xform_cache) {
  // NOTE - avoid using `XformCache::ComputeRelativeTransformation` here because
  // it requires that prim1 is a child of prim2.
  pxr::GfMatrix4d prim1_transform = xform_cache.GetLocalToWorldTransform(prim1);
  pxr::GfMatrix4d prim2_transform = xform_cache.GetLocalToWorldTransform(prim2);
  INTR_ASSIGN_OR_RETURN(
      pxr::GfMatrix4d relative_transform,
      GetRelativeTransform(prim1_transform, prim2_transform),
      _ << absl::Substitute("while getting the pose of '$0' relative to '$1'",
                            prim1.GetPath().GetString(),
                            prim2.GetPath().GetString()));
  INTR_ASSIGN_OR_RETURN(
      Pose3d relative_pose, ExtractPose(relative_transform),
      _ << absl::Substitute("while getting the pose of '$0' relative to '$1'",
                            prim1.GetPath().GetString(),
                            prim2.GetPath().GetString()));
  return relative_pose;
}

absl::StatusOr<eigenmath::Matrix4d> GetRelativeTransform(
    const pxr::UsdPrim& prim1, const pxr::UsdPrim& prim2,
    pxr::UsdGeomXformCache& xform_cache) {
  // NOTE - avoid using `XformCache::ComputeRelativeTransformation` here because
  // it requires that prim1 is a child of prim2.
  pxr::GfMatrix4d prim1_transform = xform_cache.GetLocalToWorldTransform(prim1);
  pxr::GfMatrix4d prim2_transform = xform_cache.GetLocalToWorldTransform(prim2);
  INTR_ASSIGN_OR_RETURN(
      pxr::GfMatrix4d relative_transform,
      GetRelativeTransform(prim1_transform, prim2_transform),
      _ << absl::Substitute(
          "while getting the transform of '$0' relative to '$1'",
          prim1.GetPath().GetString(), prim2.GetPath().GetString()));
  return ConvertMatrix4d(relative_transform);
}

absl::StatusOr<pxr::GfMatrix4d> GetRelativeTransform(
    const pxr::GfMatrix4d& mat_a, const pxr::GfMatrix4d& mat_b) {
  // The transform we want is: MatA * MatB^-1
  // v*(MatA * MatB^-1) transforms v from A's space to B's space.
  // It is this way instead of MatB^-1*MatA*v because matrices in
  // OpenUSD are row-major order and are meant to post-multiply vectors.
  //
  // `GetInverse` does not fail for singular matrices. It returns a matrix with
  // all values set to FLT_MAX instead, so the determinant is checked here.
  double determinant = 0.0;
  const pxr::GfMatrix4d inverse_b = mat_b.GetInverse(&determinant);
  if (std::abs(determinant) < kSingularMatrixTolerance) {
    std::stringstream matrix_string;
    matrix_string << mat_b;
    return absl::InvalidArgumentError(absl::Substitute(
        "Cannot invert the singular transform matrix $0", matrix_string.str()));
  }
  return mat_a * inverse_b;
}

Pose3d ConvertedToMeters(const Pose3d& pose, double meters_per_unit) {
  return Pose3d(pose.quaternion(), pose.translation() * meters_per_unit);
}

absl::StatusOr<pxr::UsdPrim> GetSingleTarget(
    const pxr::UsdRelationship& relationship) {
  pxr::SdfPathVector target_paths;
  INTR_RET_CHECK(relationship.GetTargets(&target_paths)) << absl::Substitute(
      "Failed to resolve targets for $0", relationship.GetPath().GetString());
  INTR_RET_CHECK(target_paths.size() == 1) << absl::Substitute(
      "Expected exactly one target for $0", relationship.GetPath().GetString());
  pxr::UsdPrim target_prim =
      relationship.GetStage()->GetPrimAtPath(target_paths[0]);
  INTR_RET_CHECK(target_prim.IsValid()) << absl::Substitute(
      "The rel $0 refers to an invalid prim: $1",
      relationship.GetPath().GetString(), target_paths[0].GetString());
  return target_prim;
}

absl::StatusOr<std::vector<std::string>> GetTargetPaths(
    const pxr::UsdRelationship& relationship) {
  pxr::SdfPathVector target_paths;
  INTR_RET_CHECK(relationship.GetTargets(&target_paths)) << absl::Substitute(
      "Failed to resolve targets for $0", relationship.GetPath().GetString());
  std::vector<std::string> target_path_strings;
  for (const auto& target_path : target_paths) {
    pxr::UsdPrim target_prim =
        relationship.GetStage()->GetPrimAtPath(target_path);
    if (target_prim.IsValid()) {
      target_path_strings.push_back(target_path.GetString());
    }
  }
  return target_path_strings;
}

std::optional<pxr::UsdRelationship> FindRelationshipWithName(
    const pxr::UsdPrim& root_prim, const std::string& relationship_name) {
  const pxr::UsdPrimRange prim_range{root_prim.GetPrim(),
                                     pxr::UsdTraverseInstanceProxies()};
  pxr::TfToken relToken(relationship_name);
  for (auto it = prim_range.begin(); it != prim_range.end(); ++it) {
    if (it->HasRelationship(relToken)) {
      return it->GetRelationship(relToken);
    }
  }
  return std::nullopt;
}

std::optional<pxr::UsdPhysicsRigidBodyAPI> GetParentRigidBody(
    const pxr::UsdPrim& prim) {
  // Traverse up the hierarchy looking for a rigid body
  for (pxr::UsdPrim curr = prim; curr; curr = curr.GetParent()) {
    if (curr.HasAPI<pxr::UsdPhysicsRigidBodyAPI>()) {
      return pxr::UsdPhysicsRigidBodyAPI{curr};
    }
  }
  return std::nullopt;
}

absl::StatusOr<double> GetDoubleOrFloat(const pxr::UsdAttribute& attr) {
  if (!attr.IsValid()) {
    return absl::InvalidArgumentError("Cannot read an invalid USD attribute.");
  }
  pxr::VtValue value;
  if (!attr.Get(&value)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to read attribute $0", attr.GetPath().GetString()));
  }
  if (value.IsHolding<double>()) {
    return value.UncheckedGet<double>();
  }
  if (value.IsHolding<float>()) {
    return value.UncheckedGet<float>();
  }
  return absl::InvalidArgumentError(
      absl::Substitute("Attribute $0 holds type '$1', but a double or float "
                       "value was expected",
                       attr.GetPath().GetString(), value.GetTypeName()));
}

bool IsMarkedInvisible(const pxr::UsdPrim& prim) {
  pxr::UsdGeomImageable imageable{prim};
  if (!imageable) {
    return false;
  }
  pxr::TfToken visibility;
  imageable.GetVisibilityAttr().Get(&visibility);
  return visibility == pxr::UsdGeomTokens->invisible;
}

std::string GetErrorString(const pxr::TfErrorMark& error_mark) {
  if (error_mark.IsClean()) {
    return "No errors";
  }
  std::vector<std::string> errors;
  for (auto const& err : error_mark) {
    errors.push_back(err.GetCommentary());
  }
  std::string errors_string;
  if (errors.size() == 0) {
    errors_string = "Unknown error";
  } else if (errors.size() == 1) {
    errors_string = errors[0];
  } else {
    // > 1 errors
    std::vector<std::string> numbered_errors;
    for (int i = 0; i < errors.size(); i++) {
      numbered_errors.push_back(std::to_string(i + 1) + ") " + errors[i]);
    }
    errors_string = absl::StrJoin(numbered_errors, ", ");
  }
  return errors_string;
}

std::string DumpStage(const pxr::UsdStageRefPtr& stage) {
  std::stringstream ss;
  // Print the identifier of the layer the stage was loaded from
  ss << "Root Layer Identifier: " << stage->GetRootLayer()->GetIdentifier()
     << std::endl;

  pxr::UsdPrim root_prim = stage->GetPseudoRoot();
  if (root_prim.IsValid()) {
    ss << DumpPrimitiveRecursive(root_prim);
  } else {
    ss << "Failed to find root prim" << std::endl;
  }
  return ss.str();
}

std::string DumpPrimitiveRecursive(const pxr::UsdPrim& prim) {
  std::stringstream ss;
  DumpPrimitiveRecursiveInternal(prim, 0, ss);
  return ss.str();
}

};  // namespace intrinsic::usd
