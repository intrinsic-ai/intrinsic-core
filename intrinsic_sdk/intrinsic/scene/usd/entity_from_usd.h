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

#ifndef INTRINSIC_SCENE_USD_ENTITY_FROM_USD_H_
#define INTRINSIC_SCENE_USD_ENTITY_FROM_USD_H_

#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdPhysics/joint.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>
#include <pxr/usd/usdShade/shader.h>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/internal/mesh/mesh.h"
#include "intrinsic/geometry/proto/v1/material.pb.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"

namespace intrinsic {
namespace usd {

// Converts a UsdPhysicsRigidBodyAPI prim to a Link proto.
// UsdPhysicsRigidBodyAPI prims contain their collision and visual meshes
// in their subtree. They may also contain nested UsdPhysicsRigidBodyAPI prims,
// which should be treated an independent rigid bodies and must be converted
// separately.
// - The XformCache is used to speed up transform lookups
// - Any geometry will be written to storage using the given
// `geometry_serializer`.
// See docs:
// https://openusd.org/release/api/usd_physics_page_front.html#usdPhysics_rigid_bodies
absl::StatusOr<intrinsic_proto::scene_object::v1::Link> LinkProtoFromRigidBody(
    const pxr::UsdPhysicsRigidBodyAPI& rigid_body,
    pxr::UsdGeomXformCache& xform_cache,
    GeometrySerializer& geometry_serializer);

// Helper struct that stores the two rigid bodies and poses of a joint
struct JointBodies {
  pxr::UsdPhysicsRigidBodyAPI body0;
  pxr::UsdPhysicsRigidBodyAPI body1;
  Pose3d body0_t_joint;
  Pose3d body1_t_joint;

  // Swaps body0 and body1 fields so that body0 becomes body1
  // and vice versa.
  void FlipBodies();
};

// Parses the bodies part of the UsdPhysicsJoint. This determines which rigid
// bodies the joint connects and the joint_t_body transforms. We currently
// only support joints that connect two rigid bodies. We also support connecting
// to a child of a rigid body (which will use the parent rigid body).
// We do not support joints that attach to to the static environment (that is,
// ones where object0 or object1 is not a rigid body).
absl::StatusOr<JointBodies> ParseJointBodies(
    const pxr::UsdPhysicsJoint& joint, pxr::UsdGeomXformCache& xform_cache);

// In USD a joint may connect two rigid bodies or it may connect a rigid body
// to the static environment, depending on how body0 and body1 are set.
enum class JointConnectionType {
  RigidBodyToRigidBody,
  RigidBodyToStaticEnvironment,
};

// Returns the joint's connection type, or an error if the joint is malformed.
absl::StatusOr<JointConnectionType> GetJointConnectionType(
    const pxr::UsdPhysicsJoint& joint);

// Stores the friction and damping of a joint, which are parsed from
// the UsdPhysicsDriveAPI and related APIs
struct JointDriveInfo {
  double damping{0.0};
  double friction{0.0};
};

// Parses the drive info (damping and friction) of a UsdPhysicsJoint. This is
// specified on the joint using the UsdPhysicsDriveAPI. If not specified, we
// return the default values of damping=0 and friction=0.
absl::StatusOr<JointDriveInfo> ParseJointDriveInfo(
    const pxr::UsdPhysicsJoint& joint);

// In USD joints, there is no notion of parent and child links. A joint just
// has body0 and body1 refs, in no particular order.
// JointPolarity specifies which of the two bodies is the parent link in terms
// of our SceneObject proto.
enum class JointPolarity {
  Body0IsParent,
  Body1IsParent,
};

// Helper struct that stores the parsed joint proto, along with some extra
// information about the joint bodies, which is required to construct the
// entity hierarchy based on the joints.
struct JointProtoWithInfo {
  intrinsic_proto::scene_object::v1::Joint joint_proto;
  JointBodies joint_bodies;
};

// Converts a UsdPhysicsJoint to a Joint proto.
// You must pass the `polarity` field to specify whether body0 or body1 of this
// joint is the parent in the link/joint chain. We require this info to set the
// parent_t_inboard and outboard_t_child poses correctly in the proto. In the
// returned `JointBodies` struct, body0 refers to the parent link and body1 the
// child link, regardless of the original order in the UsdPhysicsJoint.
//
// See docs:
// https://openusd.org/release/api/usd_physics_page_front.html#usdPhysics_joints
absl::StatusOr<JointProtoWithInfo> JointProtoFromUsdJoint(
    const pxr::UsdPhysicsJoint& joint, JointPolarity polarity,
    pxr::UsdGeomXformCache& xform_cache);

// Creates a GeometryComponent from the Usd rigid body by traversing its
// subtree and collecting all visual and collision geometry prims.
// - The XformCache is used to speed up transform lookups
// - Any geometry will be written to storage using the given
// `geometry_serializer`, and the geometry protos will use storage_refs.
absl::StatusOr<intrinsic_proto::world::GeometryComponent>
GeometryComponentFromRigidBody(const pxr::UsdPhysicsRigidBodyAPI& rigid_body,
                               pxr::UsdGeomXformCache& xform_cache,
                               GeometrySerializer& geometry_serializer);

// Creates a PhysicsComponent from a Usd rigid body by traversing its
// subtree and collecting all physics info.
absl::StatusOr<intrinsic_proto::world::PhysicsComponent>
PhysicsComponentFromRigidBody(const pxr::UsdPhysicsRigidBodyAPI& rigid_body);

// Converts a UsdGeomGPrim (which is the Usd base-class of all geometry types,
// like sphere, cube, cylinder, mesh, etc) to a Geometry type.
// Calls GeometryFromUsdMesh for meshes and GeometryFromUsdPrimitive for
// primitive shapes.
absl::StatusOr<Geometry> GeometryFromGprim(const pxr::UsdGeomGprim& gprim);

// Converts a UsdGeomMesh to a Geometry type.
absl::StatusOr<Geometry> GeometryFromUsdMesh(const pxr::UsdGeomMesh& usd_mesh);

// Converts a UsdGeomGPrim primitive type to a Geometry type.
// Currently, only the following primitive types are supported:
// - Sphere
// - Cube
// - Cylinder
// - Capsule
absl::StatusOr<Geometry> GeometryFromUsdPrimitive(
    const pxr::UsdGeomGprim& usd_geometry);

// Helper class for parsing a USD geom mesh, which may optionally contain
// some subset meshes for describing different materials.
class UsdMeshData {
 public:
  struct MeshWithMaterial {
    Mesh mesh;
    std::optional<intrinsic_proto::geometry::v1::MaterialProperties> material;
  };
  UsdMeshData() = delete;

  // Parses the given mesh and validates it.
  // We validate topology and also verify that the mesh only contains
  // triangle and quad faces.
  static absl::StatusOr<UsdMeshData> ParseMesh(
      const pxr::UsdGeomMesh& usd_mesh);

  // Get the the main mesh, as an Intrinsic mesh data type.
  MeshWithMaterial& GetMainMesh() { return main_mesh_; }

  // Get a list of meshes that partition the main mesh into
  // sub-meshes with different materials. They should be used to generate the
  // renderable. In USD, subset meshes are created using UsdGeomSubset. Most
  // commonly, UsdGeomSubset is used to create a mesh from a subset of the faces
  // of a parent mesh, for giving a different material on the subset.
  std::vector<MeshWithMaterial>& GetSubsetMeshes() { return subset_meshes_; }

 private:
  UsdMeshData(const pxr::UsdGeomMesh& usd_mesh) : usd_mesh_(usd_mesh) {}

  absl::Status ParseMainMesh();
  absl::Status ParseSubsetMeshes();

  // Creates a mesh that contains the faces with the given indices. The mesh
  // contains the vertex and face data copied over from the main mesh.
  absl::StatusOr<Mesh> CreateMeshWithFaceIndices(
      const pxr::VtArray<int>& indices);

  // Given a face that starts at `face_start_index` within `face_vertex_indices`
  // and contains `num_vertices`, converts the face to triangle faces (using fan
  // triangulation). Pushes the resulting faces to `out_faces`.
  static absl::Status TriangulateFace(
      const pxr::VtArray<int>& face_vertex_indices, size_t face_start_index,
      size_t num_vertices, Mesh::FaceCollection& out_faces);

  const pxr::UsdGeomMesh& usd_mesh_;
  pxr::VtArray<int> face_vertex_indices_;
  pxr::VtArray<int> face_vertex_counts_;
  pxr::VtArray<int> face_start_indices_;
  pxr::VtArray<pxr::GfVec3f> points_;
  MeshWithMaterial main_mesh_;
  std::vector<MeshWithMaterial> subset_meshes_;
};

// If the prim has a Material given through the MaterialBindingAPI, then
// attempt to parse it to our MaterialProperties proto. Returns std::nullopt
// if no material is specified. Returns an error if a material is given but we
// do not support the format.
//
// We support:
// 1. Materials with a UsdPreviewSurface shader (the standard USD way to
// specify simple PBR materials)
// 2. Materials with a OmniPBR shader (the OmniVerse way to specify simple PBR
// shaders, commonly found in USD assets)
absl::StatusOr<std::optional<intrinsic_proto::geometry::v1::MaterialProperties>>
ParseMaterialForPrim(const pxr::UsdPrim& primitive);

// Helper wrapper around `ParseMaterialForPrim`. Parses the prim's assigned
// material. If there is no material assigned return nullopt.
// If there is an error while reading the material, we consider this non-fatal
// and log an error, returning nullopt.
std::optional<intrinsic_proto::geometry::v1::MaterialProperties> TryGetMaterial(
    const pxr::UsdPrim& prim);

// Parse a UsdPreviewSurface to our MaterialProperties proto.
// UsdPreviewSurface is the standard way in USD to specify a surface with
// basic PBR properties like diffuseColor, metalness, etc. Typically this
// shader is used to quickly render the object during simulation or editing,
// and a more complicated material may be used for final renderings.
//
// Docs here:
// https://openusd.org/release/spec_usdpreviewsurface.html
absl::StatusOr<intrinsic_proto::geometry::v1::MaterialProperties>
ParseMaterialFromUsdPreviewSurfaceShader(
    const pxr::UsdShadeShader& preview_surface_shader);

// Parse an OmniPBR shader to our MaterialProperties proto.
// OmniPBR is Omniverse's standard PBR shader. Many USD assets use this
// shader format, so we support it in addition to the the standard
// UsdPreviewSurface shader format.
//
// Docs here:
// https://docs.omniverse.nvidia.com/materials-and-rendering/latest/templates/OmniPBR.html
absl::StatusOr<intrinsic_proto::geometry::v1::MaterialProperties>
ParseMaterialFromOmniPbrShader(const pxr::UsdShadeShader& omni_shader);

// Logs a warning if the given shader contains are inputs that are not
// included in the set of supported inputs.
void LogWarningOnUnsupportedShaderInputs(
    const pxr::UsdShadeShader& shader,
    const std::vector<std::string>& supported_inputs);

// Returns a list of all inputs used in the shader that are not included in
// the set of supported inputs.
std::vector<std::string> GetUnsupportedShaderInputs(
    const pxr::UsdShadeShader& shader,
    const std::vector<std::string>& supported_inputs);

// A helper class to assign unique names to UsdPrims. This is used for naming
// Entities in the SceneObject proto, and also for assigning Geometry prims
// unique names for use in the NamedGeometries map of the Geometry proto.
class UsdUniqueNamer {
 public:
  UsdUniqueNamer() = default;

  // Get a unique name for the given prim. If a name has already been generated
  // for this prim, returns that.
  std::string GetNameForPrim(const pxr::UsdPrim& prim);

 private:
  std::string FindUnusedName(const std::string& prim_name);

  absl::flat_hash_map<std::string, std::string> prim_path_to_name_;
  absl::flat_hash_set<std::string> used_names_;
};

}  // namespace usd
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_USD_ENTITY_FROM_USD_H_
