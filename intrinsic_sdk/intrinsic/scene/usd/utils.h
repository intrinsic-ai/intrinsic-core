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

#ifndef INTRINSIC_SCENE_USD_UTILS_H_
#define INTRINSIC_SCENE_USD_UTILS_H_

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/tf/errorMark.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/relationship.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>

#include <optional>
#include <string>

#include "absl/status/statusor.h"
#include "intrinsic/math/pose3.h"

namespace intrinsic::usd {

// Initializes the OpenUSD library by registering the runfiles path to the
// required plugins. Must be called in any executable that uses OpenUSD, or else
// OpenUSD will throw an exception upon attempting to call any functions.
// The cc_binary must include `@openusd//:plugin_files` as a `data`
// dependency so that the plugin files are available in the exec's runfiles.
//
// For tests,
// use `usd/testing/test_utils/UsdTestEnvironment` instead of this.
absl::Status InitializeOpenUsdLibrary();

// Opens a UsdStage from the contents of USD file, stored
// in a string. First, an SdfLayer is created from `contents`,
// then a UsdStage with that root layer.
//
// Pointer Management:
// UsdStageRefPtr is a typedef for TRefPtr<UsdStage>. TRefPtr
// is OpenUSD's implementation of std::shared_ptr (a reference-counted
// smart ptr). See the docs here:
// https://openusd.org/dev/api/class_tf_ref_ptr.html#details
//
// Usage:
// - Usage is basically the same as std::shared_ptr.
// - Call methods like `stagePtr->SomeMethod()`
// - Use `*stagePtr` to get a UsdStage&
// - The ref-count automatically increments on ptr copy and decrements on
//   destruction.
// - Call `stagePtr->Reset()` to reset.
// - Unlike std::shared_ptr, there is no .get() method to get the raw
//   pointer. There are ways to get it if needed, but this usage is
//   discouraged by the OpenUSD authors.
//
absl::StatusOr<pxr::UsdStageRefPtr> LoadStageFromString(
    const std::string& contents);

// Returns the root prim that should be used to start parsing the scene tree.
// If the stage specifies a `defaultPrim`, then that prim will be used.
// Otherwise, we fallback to using the stage's pseudo-root.
absl::StatusOr<pxr::UsdPrim> GetRootPrim(const pxr::UsdStageRefPtr& stage);

// Converts a USD vector to an eigenmath vector
eigenmath::Vector3d ConvertVector(const pxr::GfVec3d& vec);
eigenmath::Vector3f ConvertVector(const pxr::GfVec3f& vec);

// Converts a USD quaternion to an eigenmath quaternion
eigenmath::Quaterniond ConvertQuat(const pxr::GfQuatd& usd_quat);
eigenmath::Quaternionf ConvertQuat(const pxr::GfQuatf& usd_quat);

// Converts a USD matrix4d to an eigenmath matrix4d
eigenmath::Matrix4d ConvertMatrix4d(const pxr::GfMatrix4d& usd_matrix);

// Extracts the translation and rotation from the given matrix
// into a Pose3d.
absl::StatusOr<intrinsic::Pose3d> ExtractPose(const pxr::GfMatrix4d& matrix);

// Creates an intrinsic pose from a USD rotation and position, for convenience
intrinsic::Pose3d CreatePose(const pxr::GfQuatf& rotation,
                             const pxr::GfVec3f& position);

// Returns the world pose of the given prim
absl::StatusOr<intrinsic::Pose3d> GetWorldPose(
    const pxr::UsdPrim& prim, pxr::UsdGeomXformCache& xform_cache);

// Returns the prim2_t_prim1 pose of the given prims. That is,
// get prim1's pose with-respect-to prim2. Note `prim2` may be a parent of
// `prim1` or vice versa.
// This errors if the transform between prim1 and prim2 contains any scaling.
absl::StatusOr<intrinsic::Pose3d> GetRelativePose(
    const pxr::UsdPrim& prim1, const pxr::UsdPrim& prim2,
    pxr::UsdGeomXformCache& xform_cache);

// Get the relative transformation of prim A in prim B's frame.
// Aka the matrix that transforms a vector in A's space to a vector in B's
// space. Unlike `GetRelativePose`, the returned matrix may have scaling.
//
// See docs: https://openusd.org/dev/api/class_gf_matrix4d.html
absl::StatusOr<eigenmath::Matrix4d> GetRelativeTransform(
    const pxr::UsdPrim& prim1, const pxr::UsdPrim& prim2,
    pxr::UsdGeomXformCache& xform_cache);

// Get the relative transformation of matrix A in matrix B's frame.
// Aka the matrix that transforms a vector in A's space to a vector in B's
// space. Returns an error if `mat_b` is singular and therefore cannot be
// inverted.
// See docs: https://openusd.org/dev/api/class_gf_matrix4d.html
absl::StatusOr<pxr::GfMatrix4d> GetRelativeTransform(
    const pxr::GfMatrix4d& mat_a, const pxr::GfMatrix4d& mat_b);

// Returns `pose` but with the translation converted to meters, using the
// given conversion.
Pose3d ConvertedToMeters(const Pose3d& pose, double meters_per_unit);

// Resolves the given relationship to a single target prim. Returns an error
// if a target cannot be resolved.
absl::StatusOr<pxr::UsdPrim> GetSingleTarget(
    const pxr::UsdRelationship& relationship);

// Returns the Prim paths of all targets of the given relationship.
// Only includes paths to valid prims / paths that exist.
absl::StatusOr<std::vector<std::string>> GetTargetPaths(
    const pxr::UsdRelationship& relationship);

// Trys to find a relationship with the given name by recursively traversing
// the given `root_prim`. Returns the first relationship found with this name,
// or none if not found.
std::optional<pxr::UsdRelationship> FindRelationshipWithName(
    const pxr::UsdPrim& root_prim, const std::string& relationship_name);

// Gets the nearest ancestor UsdPhysicsRigidBody of the given primitive (which
// may be the primitive itself), or none if this prim is not a descendent of a
// rigid body.
std::optional<pxr::UsdPhysicsRigidBodyAPI> GetParentRigidBody(
    const pxr::UsdPrim& prim);

// Returns true if the prim itself or any of its ancestor prims has the given
// API, otherwise false.
template <typename APIType>
bool InheritsAPI(const pxr::UsdPrim& prim) {
  for (pxr::UsdPrim curr = prim; curr; curr = curr.GetParent()) {
    if (curr.HasAPI<APIType>()) {
      return true;
    }
  }
  return false;
}

// Reads a scalar attribute that may be authored with single or double
// precision. USD schemas declare dimensions such as `radius` as double, but
// exporters frequently author them as float.
absl::StatusOr<double> GetDoubleOrFloat(const pxr::UsdAttribute& attr);

// Reads `attr` into `value`, leaving `value` untouched if the attribute is
// invalid or holds no value of type `T`. USD properties are optional, so
// callers pre-populate `value` with the default to fall back to.
template <typename T>
void ReadAttributeOrKeepDefault(const pxr::UsdAttribute& attr, T& value) {
  T attr_value;
  if (attr.IsValid() && attr.Get<T>(&attr_value)) {
    value = attr_value;
  }
}

// Returns true if the given UsdPrim is marked as "invisible", meaning that
// the prim and its entire subtree should be pruned from all processing.
// This only checks the visibility attribute on the prim itself. It does not
// traverse the ancestors to check for visibility.
bool IsMarkedInvisible(const pxr::UsdPrim& prim);

// Helper function to extract all errors from the error_mark and return
// them as a formatted string.
std::string GetErrorString(const pxr::TfErrorMark& error_mark);

// Dumps the contents of the stage to a string, for debugging.
// Includes a description of all primitives and their properties,
// recursively.
std::string DumpStage(const pxr::UsdStageRefPtr& stage);

// Dumps the contents of the primitive to a string, for debugging.
// Includes a description of this primitive's properties and
// child primitives, recursively.
std::string DumpPrimitiveRecursive(const pxr::UsdPrim& prim);

};  // namespace intrinsic::usd

#endif  // INTRINSIC_SCENE_USD_UTILS_H_
