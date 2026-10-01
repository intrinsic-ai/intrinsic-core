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

#include "intrinsic/scene/usd/entity_from_usd.h"

#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/rotation.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdPhysics/collisionAPI.h>
#include <pxr/usd/usdPhysics/driveAPI.h>
#include <pxr/usd/usdPhysics/fixedJoint.h>
#include <pxr/usd/usdPhysics/massAPI.h>
#include <pxr/usd/usdPhysics/metrics.h>
#include <pxr/usd/usdPhysics/prismaticJoint.h>
#include <pxr/usd/usdPhysics/revoluteJoint.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>
#include <pxr/usd/usdPhysics/tokens.h>
#include <pxr/usd/usdShade/input.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

#include <algorithm>
#include <limits>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/substitute.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/scene/usd/utils.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/proto/geometry_component.pb.h"

namespace intrinsic {
namespace usd {

namespace {

absl::StatusOr<eigenmath::Vector3d> ParseAxisAttribute(
    const pxr::UsdAttribute& axis_attr) {
  pxr::TfToken axis_token;
  INTR_RET_CHECK(axis_attr.Get(&axis_token));
  if (axis_token == pxr::UsdPhysicsTokens->x) {
    return eigenmath::Vector3d{1, 0, 0};
  } else if (axis_token == pxr::UsdPhysicsTokens->y) {
    return eigenmath::Vector3d{0, 1, 0};
  } else if (axis_token == pxr::UsdPhysicsTokens->z) {
    return eigenmath::Vector3d{0, 0, 1};
  }
  return absl::InvalidArgumentError(
      absl::Substitute("unexpected axis $0", axis_token.GetString()));
}

Pose3d GetRotatedPose(const eigenmath::Vector3d& original_axis,
                      const eigenmath::Vector3d& rotated_axis) {
  // Returns a pose where the original axis of the primitive is rotated
  // to the given `rotated_axis`. Used to set the pose of cylinders and
  // capsules, which we define as z-aligned but may be aligned to another
  // axis in OpenUSD.
  auto rotation =
      eigenmath::Quaterniond::FromTwoVectors(original_axis, rotated_axis);
  return Pose3d(rotation.normalized());
}

// Returns true if the given prim is a collider, i.e. the prim or one of its
// ancestors has the collision API applied, and collisions are not disabled by
// the prim or any of its ancestors.
bool IsCollisionEnabled(const pxr::UsdPrim& prim) {
  bool has_collision_api = false;
  for (pxr::UsdPrim curr = prim; curr; curr = curr.GetParent()) {
    if (!curr.HasAPI<pxr::UsdPhysicsCollisionAPI>()) {
      continue;
    }
    has_collision_api = true;
    // The USD default is `true`, which is kept if the attribute is absent.
    bool enabled = true;
    ReadAttributeOrKeepDefault(
        pxr::UsdPhysicsCollisionAPI(curr).GetCollisionEnabledAttr(), enabled);
    if (!enabled) {
      return false;
    }
  }
  return has_collision_api;
}

// Reads the points of the given mesh, which USD allows to be authored either
// as `point3f[]` (single precision) or `point3d[]` (double precision).
absl::Status ReadMeshPoints(const pxr::UsdGeomMesh& usd_mesh,
                            pxr::VtArray<pxr::GfVec3f>& points) {
  const pxr::UsdAttribute points_attr = usd_mesh.GetPointsAttr();
  if (points_attr.Get<pxr::VtArray<pxr::GfVec3f>>(&points)) {
    return absl::OkStatus();
  }
  pxr::VtArray<pxr::GfVec3d> double_points;
  if (!points_attr.Get<pxr::VtArray<pxr::GfVec3d>>(&double_points)) {
    return absl::InvalidArgumentError(
        absl::Substitute("Failed to read the points attribute from mesh $0",
                         usd_mesh.GetPath().GetString()));
  }
  points.clear();
  points.reserve(double_points.size());
  for (const pxr::GfVec3d& point : double_points) {
    points.emplace_back(point);
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<intrinsic_proto::scene_object::v1::Link> LinkProtoFromRigidBody(
    const pxr::UsdPhysicsRigidBodyAPI& rigid_body,
    pxr::UsdGeomXformCache& xform_cache,
    GeometrySerializer& geometry_serializer) {
  intrinsic_proto::scene_object::v1::Link link;
  INTR_ASSIGN_OR_RETURN(*link.mutable_geometry_component(),
                        GeometryComponentFromRigidBody(rigid_body, xform_cache,
                                                       geometry_serializer));
  INTR_ASSIGN_OR_RETURN(*link.mutable_physics_component(),
                        PhysicsComponentFromRigidBody(rigid_body));
  return link;
}

absl::StatusOr<JointProtoWithInfo> JointProtoFromUsdJoint(
    const pxr::UsdPhysicsJoint& joint, JointPolarity polarity,
    pxr::UsdGeomXformCache& xform_cache) {
  auto kinematics = KinematicsComponent::Create();

  // Parse the joint bodies and set the inboard/outbound transforms.
  //
  // Note that in KinematicsComponent the poses are:
  // - parent-to-inboard
  // - outboard-to-child
  //
  // In USD, the poses are:
  // - body0 -to- joint (aka body0-to-inboard)
  // - body1 -to- joint (aka body1-to-outboard)
  //
  // By convention, the SceneObject joint's child entity has parent_t_this set
  // to the identity, and the final transfrom from the joint's parent to child
  // will be: parent_t_child = parent_t_inboard * inboard_t_outboard *
  // outboard_t_child
  //
  // Convention stated here: world/conversion/sdf/joint_conversion.h
  // Related ticket: b/361105294
  //
  // We do not know up-front if body0 or body1 of the USD joint is considered
  // the "parent". We only find this out when we parse the full kinematic chain
  // to a tree structure in `SceneObjectFromUsdStage`. In this function, we
  // assume body0 is the parent. We swap the values later if needed.
  INTR_ASSIGN_OR_RETURN(JointBodies joint_bodies,
                        ParseJointBodies(joint, xform_cache));
  // Flip the bodies if needed so that body0 is the parent link.
  if (polarity != JointPolarity::Body0IsParent) {
    joint_bodies.FlipBodies();
  }
  kinematics->SetParentTInboard(joint_bodies.body0_t_joint);
  kinematics->SetOutboardTChild(joint_bodies.body1_t_joint.inverse());

  // Set the motion type, axis, and limits.
  // Note on axis - we don't have to flip the joint's axis to match the
  // polarity because the axis should be the same vector in the joint's inboard
  // and outboard spaces.
  if (joint.GetPrim().IsA<pxr::UsdPhysicsFixedJoint>()) {
    kinematics->SetMotionType(
        intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_FIXED);
  } else if (joint.GetPrim().IsA<pxr::UsdPhysicsRevoluteJoint>()) {
    const pxr::UsdPhysicsRevoluteJoint revolute_joint{joint.GetPrim()};
    kinematics->SetMotionType(
        intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_REVOLUTE);
    // In both USD and our SceneObject proto, the axis is relative to the
    // joint's inboard coordinate frame.
    INTR_ASSIGN_OR_RETURN(auto axis_vector,
                          ParseAxisAttribute(revolute_joint.GetAxisAttr()));
    kinematics->SetAxis(axis_vector);

    // The lower and upper limits for a revolute joint are given in degrees.
    // We convert to radians.
    // The USD default for lower_limit is -inf.
    float lower_limit{-std::numeric_limits<float>::infinity()};
    ReadAttributeOrKeepDefault(revolute_joint.GetLowerLimitAttr(), lower_limit);
    if (lower_limit != -std::numeric_limits<float>::infinity()) {
      lower_limit *= M_PI / 180.0;
    }
    // The USD default for upper_limit is +inf.
    float upper_limit{std::numeric_limits<float>::infinity()};
    ReadAttributeOrKeepDefault(revolute_joint.GetUpperLimitAttr(), upper_limit);
    if (upper_limit != std::numeric_limits<float>::infinity()) {
      upper_limit *= M_PI / 180.0f;
    }

    INTR_RETURN_IF_ERROR(kinematics->SetApplicationRawValueFixedLimits(
        lower_limit, upper_limit, false));
    INTR_RETURN_IF_ERROR(kinematics->SetSystemRawValueFixedLimits(
        lower_limit, upper_limit, true));
  } else if (joint.GetPrim().IsA<pxr::UsdPhysicsPrismaticJoint>()) {
    const pxr::UsdPhysicsPrismaticJoint prismatic_joint{joint.GetPrim()};
    kinematics->SetMotionType(
        intrinsic_proto::world::KinematicsComponent::MOTION_TYPE_PRISMATIC);
    // In both USD and our SceneObject proto, the axis is relative to the
    // joint's inboard coordinate frame.
    INTR_ASSIGN_OR_RETURN(auto axis_vector,
                          ParseAxisAttribute(prismatic_joint.GetAxisAttr()));
    kinematics->SetAxis(axis_vector);

    // The lower and upper limits for a prismatic joint are given in USD
    // distance units, which must be converted to meters.
    // The USD default for lower_limit is -inf.
    const double meters_per_unit =
        pxr::UsdGeomGetStageMetersPerUnit(joint.GetPrim().GetStage());
    float lower_limit{-std::numeric_limits<float>::infinity()};
    ReadAttributeOrKeepDefault(prismatic_joint.GetLowerLimitAttr(),
                               lower_limit);
    if (lower_limit != -std::numeric_limits<float>::infinity()) {
      lower_limit *= meters_per_unit;
    }
    // The USD default for upper_limit is +inf.
    float upper_limit{std::numeric_limits<float>::infinity()};
    ReadAttributeOrKeepDefault(prismatic_joint.GetUpperLimitAttr(),
                               upper_limit);
    if (upper_limit != std::numeric_limits<float>::infinity()) {
      upper_limit *= meters_per_unit;
    }

    INTR_RETURN_IF_ERROR(kinematics->SetApplicationRawValueFixedLimits(
        lower_limit, upper_limit, false));
    INTR_RETURN_IF_ERROR(kinematics->SetSystemRawValueFixedLimits(
        lower_limit, upper_limit, true));
  } else {
    return absl::InvalidArgumentError(absl::Substitute(
        "Joint type $0 is not supported. Supported "
        "types are: FixedJoint, RevoluteJoint, PrismaticJoint.",
        joint.GetPrim().GetTypeName().GetString()));
  }

  INTR_ASSIGN_OR_RETURN(JointDriveInfo drive_info, ParseJointDriveInfo(joint));
  kinematics->SetDamping(drive_info.damping);
  kinematics->SetFriction(drive_info.friction);

  intrinsic_proto::scene_object::v1::Joint joint_proto;
  INTR_ASSIGN_OR_RETURN(*joint_proto.mutable_kinematics_component(),
                        kinematics->ToProto());

  // Make sure the joint's raw value is cleared in the proto. The USD does not
  // specify this, and scene object validation will fail if it is set to an
  // incorrect value (out of range).
  joint_proto.mutable_kinematics_component()->clear_raw_value();

  return JointProtoWithInfo{joint_proto, joint_bodies};
}

absl::StatusOr<JointBodies> ParseJointBodies(
    const pxr::UsdPhysicsJoint& joint, pxr::UsdGeomXformCache& xform_cache) {
  // According to spec, a joint is attached to two xformables. These xformables
  // may be part of a rigid body (if the xformable is a descendent of a rigid
  // body), or, if that's not the case, it means the joint should be attached to
  // the static environment. Right now we only support the case where the joint
  // connects two rigid bodies.
  INTR_ASSIGN_OR_RETURN(pxr::UsdPrim body0_prim,
                        GetSingleTarget(joint.GetBody0Rel()));
  INTR_ASSIGN_OR_RETURN(pxr::UsdPrim body1_prim,
                        GetSingleTarget(joint.GetBody1Rel()));
  INTR_RET_CHECK(body0_prim.IsA<pxr::UsdGeomXformable>());
  INTR_RET_CHECK(body1_prim.IsA<pxr::UsdGeomXformable>());

  std::optional<pxr::UsdPhysicsRigidBodyAPI> rigid_body0 =
      GetParentRigidBody(body0_prim);
  std::optional<pxr::UsdPhysicsRigidBodyAPI> rigid_body1 =
      GetParentRigidBody(body1_prim);
  if (!(rigid_body0.has_value() && rigid_body1.has_value())) {
    auto non_rigidbody = !rigid_body0.has_value() ? body0_prim : body1_prim;
    return absl::InvalidArgumentError(absl::Substitute(
        "Only joints attached to two rigid bodies are supported. Joint $0 is "
        "connected to a non-rigid body ($1)",
        joint.GetPrim().GetPath().GetString(),
        non_rigidbody.GetPath().GetString()));
  }

  const double meters_per_unit =
      pxr::UsdGeomGetStageMetersPerUnit(joint.GetPrim().GetStage());

  // Get body0_t_frame
  pxr::GfVec3f local_pos0{0, 0, 0};
  INTR_RET_CHECK(joint.GetLocalPos0Attr().Get(&local_pos0));
  pxr::GfQuatf local_rot0{1, 0, 0, 0};
  INTR_RET_CHECK(joint.GetLocalRot0Attr().Get(&local_rot0));
  Pose3d object0_t_joint = CreatePose(local_rot0, local_pos0);

  // Get body1_t_frame
  pxr::GfVec3f local_pos1{0, 0, 0};
  INTR_RET_CHECK(joint.GetLocalPos1Attr().Get(&local_pos1));
  pxr::GfQuatf local_rot1{1, 0, 0, 0};
  INTR_RET_CHECK(joint.GetLocalRot1Attr().Get(&local_rot1));
  Pose3d object1_t_joint = CreatePose(local_rot1, local_pos1);

  // Modify the poses so that they are rigidbody_t_joint instead of
  // object_t_joint, where `object` may be some child transform (aka attachment
  // point) within the parent rigid body. For example, if a joint attaches to an
  // attachment point with an offset of (1, 0, 0) within its parent rigid body,
  // then the rigidbody_t_joint pose should account for the (-1, 0, 0) offset
  // from the attachment origin to the body origin.
  // NOTE - body0 or body1's local space may be scaled relative to its parent
  // rigid body's space, so make sure we handle this. When we convert from
  // Matrix4d to Pose3d, this scaling portion is removed, as desired.
  INTR_ASSIGN_OR_RETURN(
      auto rigidbody0_t_object0,
      GetRelativeTransform(body0_prim, rigid_body0->GetPrim(), xform_cache));
  Pose3d rigidbody0_t_joint{rigidbody0_t_object0 * object0_t_joint};

  INTR_ASSIGN_OR_RETURN(
      auto rigidbody1_t_object1,
      GetRelativeTransform(body1_prim, rigid_body1->GetPrim(), xform_cache));
  Pose3d rigidbody1_t_joint{rigidbody1_t_object1 * object1_t_joint};

  return JointBodies{
      .body0 = *rigid_body0,
      .body1 = *rigid_body1,
      .body0_t_joint = ConvertedToMeters(rigidbody0_t_joint, meters_per_unit),
      .body1_t_joint = ConvertedToMeters(rigidbody1_t_joint, meters_per_unit)};
}

absl::StatusOr<JointConnectionType> GetJointConnectionType(
    const pxr::UsdPhysicsJoint& joint) {
  bool body0IsRigidBody{false};
  if (joint.GetBody0Rel().HasAuthoredTargets()) {
    INTR_ASSIGN_OR_RETURN(pxr::UsdPrim body0_prim,
                          GetSingleTarget(joint.GetBody0Rel()));
    INTR_RET_CHECK(body0_prim.IsA<pxr::UsdGeomXformable>());
    body0IsRigidBody = GetParentRigidBody(body0_prim).has_value();
  }
  bool body1IsRigidBody{false};
  if (joint.GetBody1Rel().HasAuthoredTargets()) {
    INTR_ASSIGN_OR_RETURN(pxr::UsdPrim body1_prim,
                          GetSingleTarget(joint.GetBody1Rel()));
    INTR_RET_CHECK(body1_prim.IsA<pxr::UsdGeomXformable>());
    body1IsRigidBody = GetParentRigidBody(body1_prim).has_value();
  }
  if (!body0IsRigidBody && !body1IsRigidBody) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Joint $0 is invalid - neither body0 nor body1 refers to a rigid body. "
        "According to spec, at least one must be a rigid body.",
        joint.GetPrim().GetPath().GetString()));
  }
  return (body0IsRigidBody && body1IsRigidBody)
             ? JointConnectionType::RigidBodyToRigidBody
             : JointConnectionType::RigidBodyToStaticEnvironment;
}

void JointBodies::FlipBodies() {
  std::swap(body0, body1);
  std::swap(body0_t_joint, body1_t_joint);
}

absl::StatusOr<JointDriveInfo> ParseJointDriveInfo(
    const pxr::UsdPhysicsJoint& joint) {
  // USD defines the damping and stiffness through a UsdPhysicsDriveAPI
  // schema that may optionally be applied to the joint.
  //
  // The drive force on the joint is proportional to:
  // stiffness*(targetPosition - p) + damping*(targetVelocity - v)
  //
  // Docs: https://openusd.org/release/api/class_usd_physics_drive_a_p_i.html

  // TODO(b/424729946) - Not yet implemented. Just return the default values
  // for now. It is not entirely clear how the USD damping and stiffness map
  // to our proto damping and friction values.

  return JointDriveInfo{.damping = 0.0, .friction = 0.0};
}

absl::StatusOr<intrinsic_proto::world::GeometryComponent>
GeometryComponentFromRigidBody(const pxr::UsdPhysicsRigidBodyAPI& rigid_body,
                               pxr::UsdGeomXformCache& xform_cache,
                               GeometrySerializer& geometry_serializer) {
  auto geometry_component = GeometryComponent::Create();
  NamedGeometryProtoSet visual_geometry_set;
  NamedGeometryProtoSet collision_geometry_set;
  UsdUniqueNamer visual_geometry_namer;
  UsdUniqueNamer collision_geometry_namer;

  // Iterate children in depth-first, pre-order (parent then children).
  // Make sure we iterate into instance proxies.
  const pxr::UsdPrimRange primRange{rigid_body.GetPrim(),
                                    pxr::UsdTraverseInstanceProxies()};
  for (auto it = primRange.begin(); it != primRange.end(); ++it) {
    const pxr::UsdPrim child_prim = *it;
    if (IsMarkedInvisible(child_prim)) {
      // Skip invisible subtrees
      it.PruneChildren();
      continue;
    }

    if (child_prim != rigid_body.GetPrim() &&
        child_prim.HasAPI<pxr::UsdPhysicsRigidBodyAPI>()) {
      // According to the UsdPhysics docs, a nested rigid body should be treated
      // as entirely independent from its parent. So do not traverse into its
      // subtree here. It will be handled by the outer iteration loop in
      // `SceneObjectFromUsdStage`
      it.PruneChildren();
    } else if (child_prim.IsA<pxr::UsdGeomGprim>()) {
      // UsdGeomGprim is the base-class for all USD geometry classes. This
      // includes shapes like spheres, cubes, cylinders and also meshes.

      // Get the relative transform to the rigid body. Note - unlike rigid
      // bodies and joints, geometry may have scaling applied, so use
      // `GetRelativeTransform` instead of `GetRelativePose` here.
      INTR_ASSIGN_OR_RETURN(
          eigenmath::Matrix4d geometry_transform,
          GetRelativeTransform(child_prim, rigid_body.GetPrim(), xform_cache),
          _ << absl::Substitute("while parsing '$0'",
                                child_prim.GetPath().GetString()));
      // Convert the UsdGeomGprim to a Geometry object
      INTR_ASSIGN_OR_RETURN(
          auto geometry, GeometryFromGprim(pxr::UsdGeomGprim{child_prim}),
          _ << absl::Substitute("while parsing '$0'",
                                child_prim.GetPath().GetString()));
      TransformedGeometry transformed_geometry{geometry, geometry_transform};
      INTR_ASSIGN_OR_RETURN(
          auto geometry_proto,
          ToProto(transformed_geometry, &geometry_serializer),
          _ << absl::Substitute("while serializing '$0'",
                                child_prim.GetPath().GetString()));

      // The geometry may be a visual geometry, collision geometry, or both.
      // - It is a visual geometry unless the geometry's "purpose" is set to
      //   "guide", which indicates that it is not part of the real world.
      // - It is a collision geometry if the geometry (or any of its parents)
      //   has the PhysicsCollisionAPI schema applied.
      // - If you take a regular geometry like a sphere and add
      //   PhysicsCollisionAPI (with default purpose), it should be both visual
      //   and collision geometry.
      pxr::UsdGeomImageable imageable{child_prim};
      INTR_RET_CHECK(imageable);
      // Note - this traverses up the scene tree and isn't the most efficient.
      // Should be fine because we aren't working with massive scene trees for
      // robot imports.
      if (imageable.ComputePurpose() != pxr::UsdGeomTokens->guide) {
        std::string unique_name =
            visual_geometry_namer.GetNameForPrim(child_prim);
        visual_geometry_set.emplace(unique_name, geometry_proto);
      }
      if (IsCollisionEnabled(child_prim)) {
        std::string unique_name =
            collision_geometry_namer.GetNameForPrim(child_prim);
        collision_geometry_set.emplace(unique_name, geometry_proto);
      }
    }
  }

  if (!visual_geometry_set.empty()) {
    geometry_component->SetGeometry(kKindVisualGeometry, visual_geometry_set);
  }
  if (!collision_geometry_set.empty()) {
    geometry_component->SetGeometry(kKindCollisionGeometry,
                                    collision_geometry_set);
  }
  return geometry_component->ToProto();
}

absl::StatusOr<intrinsic_proto::world::PhysicsComponent>
PhysicsComponentFromRigidBody(const pxr::UsdPhysicsRigidBodyAPI& rigid_body) {
  // Rigid bodies specify their mass using the UsdPhysicsMassAPI schema.
  // The schema is usually applied right to the body prim, but OpenUSD also
  // allows it to be applied to child collision geometries and accumulated
  // up (if it is not explicitly given on the body itself).
  // We only support the first case for now (given on the rigid body).
  //
  // TODO(b/470375844) - Support getting the mass when specified on child
  // colliders using the `ComputeMassProperties` helper function. This requires
  // fixing the function, which was giving some unexpected NaN values during
  // testing.
  // https://openusd.org/release/api/usd_physics_page_front.html#usdPhysics_body_mass_properties

  auto physics_component = PhysicsComponent::Create();
  if (rigid_body.GetPrim().HasAPI<pxr::UsdPhysicsMassAPI>()) {
    const pxr::UsdPhysicsMassAPI massAPI(rigid_body.GetPrim());

    // The attributes below are all optional. If one is not authored and has no
    // schema fallback, the USD default documented at each attribute is kept.
    //
    // USD default value is 0. A mass of 0 means the body's mass properties
    // should be ignored. Return the default PhysicsComponent in that case.
    float mass{0.0};
    ReadAttributeOrKeepDefault(massAPI.GetMassAttr(), mass);
    if (mass <= 0.0) {
      return physics_component->ToProto();
    }

    // USD default value: (-inf, -inf, -inf)
    // Our default value: {0, 0, 0}
    pxr::GfVec3f center_of_mass{0.0};
    ReadAttributeOrKeepDefault(massAPI.GetCenterOfMassAttr(), center_of_mass);
    if (std::isinf(center_of_mass[0]) || std::isinf(center_of_mass[1]) ||
        std::isinf(center_of_mass[2])) {
      center_of_mass = {0, 0, 0};
    }

    // USD default value: (0, 0, 0)
    // Our default value: (1, 1, 1)
    const pxr::UsdAttribute diagonal_inertia_attr =
        massAPI.GetDiagonalInertiaAttr();
    pxr::GfVec3f diagonal_inertia{0.0};
    ReadAttributeOrKeepDefault(diagonal_inertia_attr, diagonal_inertia);
    const bool has_authored_inertia = diagonal_inertia_attr.HasAuthoredValue();
    if (pxr::GfIsClose(diagonal_inertia, pxr::GfVec3f{0}, 1e-8)) {
      diagonal_inertia = {1, 1, 1};
    }

    // USD default value: (0, 0, 0, 0)
    // Our default value: (1, 0, 0, 0)
    pxr::GfQuatf principal_axes{0.0};
    ReadAttributeOrKeepDefault(massAPI.GetPrincipalAxesAttr(), principal_axes);
    if (pxr::GfIsClose(principal_axes.GetImaginary(), pxr::GfVec3f(0), 1e-8) &&
        fabsf(principal_axes.GetReal()) < 1e-8) {
      principal_axes = pxr::GfQuatf::GetIdentity();
    }

    // Convert the USD quantities to a PhysicsComponent.
    // Mass is given in arbitrary units in USD and must be converted to kg using
    // a conversion given per-stage (defaults to 1).
    Pose3d center_of_mass_pose = CreatePose(principal_axes, center_of_mass);
    double kg_per_unit = pxr::UsdPhysicsGetStageKilogramsPerUnit(
        rigid_body.GetPrim().GetStage());
    if (has_authored_inertia) {
      // The authored inertia is given in the stage's mass units. Our default
      // inertia is already in kg and must not be converted.
      diagonal_inertia *= kg_per_unit;
    }
    eigenmath::Matrix3d inertia_matrix =
        ConvertVector(diagonal_inertia).cast<double>().asDiagonal();
    physics_component->SetMassKg(mass * kg_per_unit);
    physics_component->SetThisTCenterOfMass(center_of_mass_pose);
    physics_component->SetInertiaMatrix(inertia_matrix);

    // TODO(b/470160654) - Also set the friction, stiffness, and damping
    // on the PhysicsComponent here.
  }

  return physics_component->ToProto();
}

// static
absl::StatusOr<UsdMeshData> UsdMeshData::ParseMesh(
    const pxr::UsdGeomMesh& usd_mesh) {
  UsdMeshData result{usd_mesh};
  INTR_RET_CHECK(usd_mesh.GetFaceVertexIndicesAttr().Get<pxr::VtArray<int>>(
      &result.face_vertex_indices_));
  INTR_RET_CHECK(usd_mesh.GetFaceVertexCountsAttr().Get<pxr::VtArray<int>>(
      &result.face_vertex_counts_));
  INTR_RETURN_IF_ERROR(ReadMeshPoints(usd_mesh, result.points_));

  std::string topology_error_reason;
  if (!pxr::UsdGeomMesh::ValidateTopology(
          result.face_vertex_indices_, result.face_vertex_counts_,
          result.points_.size(), &topology_error_reason)) {
    return absl::InvalidArgumentError(absl::Substitute(
        "$0 has invalid topology: $1", usd_mesh.GetPath().GetString(),
        topology_error_reason));
  }

  // Produce an array of `face_start_indices_` where element i is the start
  // index of the first vertex of face i.
  result.face_start_indices_.reserve(result.face_vertex_counts_.size());
  int next_face_start_index = 0;
  for (int vert_count : result.face_vertex_counts_) {
    result.face_start_indices_.push_back(next_face_start_index);
    next_face_start_index += vert_count;
  }

  INTR_RETURN_IF_ERROR(result.ParseMainMesh());
  INTR_RETURN_IF_ERROR(result.ParseSubsetMeshes());

  return result;
}

absl::Status UsdMeshData::ParseMainMesh() {
  // Create Intrinsic Mesh data
  const int num_vertices = points_.size();
  Mesh::VertexCollection vertices;
  vertices.reserve(num_vertices);
  for (const pxr::GfVec3f& p : points_) {
    vertices.emplace_back(p[0], p[1], p[2]);
  }
  Mesh::FaceCollection faces;
  int next_face_index = 0;
  for (int vert_count : face_vertex_counts_) {
    INTR_RETURN_IF_ERROR(TriangulateFace(face_vertex_indices_, next_face_index,
                                         vert_count, faces));
    next_face_index += vert_count;
  }

  main_mesh_ = MeshWithMaterial{
      .mesh = Mesh(vertices, faces),
      .material = TryGetMaterial(usd_mesh_.GetPrim()),
  };

  return absl::OkStatus();
}

absl::Status UsdMeshData::ParseSubsetMeshes() {
  // Add all "face" geometry subsets to the Renderable. These are subsets that
  // specify some number of faces of the main mesh and apply a different
  // material to them. We leave 'familyName' blank in these USD calls so that
  // we get the "face" subsets with any familyName.
  std::vector<pxr::UsdGeomSubset> geom_subsets =
      pxr::UsdGeomSubset::GetGeomSubsets(usd_mesh_, pxr::TfToken("face"));
  if (geom_subsets.size() > 0) {
    std::string validation_error;
    if (!pxr::UsdGeomSubset::ValidateFamily(usd_mesh_, pxr::TfToken("face"),
                                            pxr::TfToken(""),
                                            &validation_error)) {
      return absl::InvalidArgumentError(
          absl::Substitute("The mesh $0 has invalid subsets: $1",
                           usd_mesh_.GetPath().GetString(), validation_error));
    }

    for (const pxr::UsdGeomSubset& geom_subset : geom_subsets) {
      // The indices attribute contains the indices of the faces of this
      // subset mesh.
      pxr::VtArray<int> indices;
      INTR_RET_CHECK(geom_subset.GetIndicesAttr().Get(&indices));
      INTR_ASSIGN_OR_RETURN(auto subset_mesh,
                            CreateMeshWithFaceIndices(indices));
      subset_meshes_.push_back(MeshWithMaterial{
          .mesh = std::move(subset_mesh),
          .material = TryGetMaterial(geom_subset.GetPrim()),
      });
    }

    // Add a subset for any faces that were not assigned to any subset,
    // completing the partition.
    auto unassigned_indices = pxr::UsdGeomSubset::GetUnassignedIndices(
        usd_mesh_, pxr::TfToken("face"), pxr::TfToken(""));
    if (!unassigned_indices.empty()) {
      INTR_ASSIGN_OR_RETURN(auto subset_mesh,
                            CreateMeshWithFaceIndices(unassigned_indices));
      subset_meshes_.push_back(MeshWithMaterial{
          .mesh = std::move(subset_mesh),
          .material = TryGetMaterial(usd_mesh_.GetPrim()),
      });
    }
  } else {
    // We have no subset meshes that partition the main mesh, so just use
    // the main mesh here.
    subset_meshes_.push_back(MeshWithMaterial{
        .mesh = main_mesh_.mesh.Clone(),
        .material = main_mesh_.material,
    });
  }

  return absl::OkStatus();
}

absl::StatusOr<Mesh> UsdMeshData::CreateMeshWithFaceIndices(
    const pxr::VtArray<int>& indices) {
  // Collect all the subset faces
  Mesh::FaceCollection subset_faces;
  subset_faces.reserve(indices.size());
  for (int face_index : indices) {
    if (face_index < 0 ||
        static_cast<size_t>(face_index) >= face_vertex_counts_.size()) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Submesh face index $0 of mesh $1 is out of bounds [0, $2)",
          face_index, usd_mesh_.GetPath().GetString(),
          face_vertex_counts_.size()));
    }
    int vert_count = face_vertex_counts_[face_index];
    int face_start_index = face_start_indices_[face_index];
    INTR_RETURN_IF_ERROR(TriangulateFace(face_vertex_indices_, face_start_index,
                                         vert_count, subset_faces));
  }

  // We copy over the vertices needed by this subset mesh and remap the
  // vertex indices of the faces.
  // `vertex_index_map` maps the original vertex index to the new vertex index.
  absl::flat_hash_map<int, int> vertex_index_map;
  Mesh::VertexCollection vertex_copies;
  vertex_copies.reserve(std::min(points_.size(), subset_faces.size() * 3));
  for (auto& face : subset_faces) {
    for (int& vertex_index : face) {
      auto it = vertex_index_map.find(vertex_index);
      if (it == vertex_index_map.end()) {
        vertex_copies.push_back({
            points_[vertex_index][0],
            points_[vertex_index][1],
            points_[vertex_index][2],
        });
        vertex_index_map[vertex_index] = vertex_copies.size() - 1;
        vertex_index = vertex_copies.size() - 1;
      } else {
        vertex_index = it->second;
      }
    }
  }
  return Mesh(std::move(vertex_copies), std::move(subset_faces));
}

// static
absl::Status UsdMeshData::TriangulateFace(
    const pxr::VtArray<int>& face_vertex_indices, size_t face_start_index,
    size_t num_vertices, Mesh::FaceCollection& out_faces) {
  if (num_vertices < 3) {
    return absl::InvalidArgumentError(
        "The given face has less than 3 vertices.");
  }

  // Use simple fan-triangulation of the polygon. Use the first vertex as the
  // pivot and iterate around the polygon (which is in CCW winding order).
  for (size_t i = 1; i < num_vertices - 1; ++i) {
    out_faces.push_back({face_vertex_indices[face_start_index],
                         face_vertex_indices[face_start_index + i],
                         face_vertex_indices[face_start_index + i + 1]});
  }
  return absl::OkStatus();
}

absl::StatusOr<Geometry> GeometryFromGprim(
    const pxr::UsdGeomGprim& usd_geometry) {
  INTR_RET_CHECK(usd_geometry.GetPrim().IsValid())
      << " cannot convert a USD geometry with invalid prim";

  if (usd_geometry.GetPrim().IsA<pxr::UsdGeomMesh>()) {
    return GeometryFromUsdMesh(pxr::UsdGeomMesh{usd_geometry});
  } else {
    return GeometryFromUsdPrimitive(usd_geometry);
  }
}

absl::StatusOr<Geometry> GeometryFromUsdMesh(const pxr::UsdGeomMesh& usd_mesh) {
  // Parse the mesh to a main mesh and any subset meshes.
  // - We create the ExactGeometry from the main mesh vertex data. This is used
  //   by motion planning, and anything else that needs just the vertex data.
  // - We generate the Renderable from the subset meshes, which partition the
  //   main mesh into meshes of different materials.
  INTR_ASSIGN_OR_RETURN(auto mesh_data, UsdMeshData::ParseMesh(usd_mesh));
  UsdMeshData::MeshWithMaterial& main_mesh = mesh_data.GetMainMesh();
  ExactGeometry exact_geometry = ExactGeometry{std::move(main_mesh.mesh)};

  RenderableGenerator renderable_generator;
  for (UsdMeshData::MeshWithMaterial& subset_mesh :
       mesh_data.GetSubsetMeshes()) {
    auto subset_material =
        subset_mesh.material.has_value()
            ? std::optional<MaterialProperties>(
                  MaterialProperties::FromProto(*subset_mesh.material))
            : std::nullopt;
    INTR_RETURN_IF_ERROR(renderable_generator.AddGeometry(
        ExactGeometry{std::move(subset_mesh.mesh)}, subset_material));
  }

  INTR_ASSIGN_OR_RETURN(auto renderable, renderable_generator.Finish());
  const bool keep_renderable = renderable != nullptr;
  return Geometry{std::move(exact_geometry), renderable, keep_renderable,
                  std::nullopt};
}

absl::StatusOr<Geometry> GeometryFromUsdPrimitive(
    const pxr::UsdGeomGprim& usd_geometry) {
  ExactGeometry exact_geometry = ExactGeometry::CreateEmpty();
  if (usd_geometry.GetPrim().IsA<pxr::UsdGeomSphere>()) {
    const pxr::UsdGeomSphere usd_sphere{usd_geometry};
    INTR_ASSIGN_OR_RETURN(const double radius,
                          GetDoubleOrFloat(usd_sphere.GetRadiusAttr()));
    shapes::Sphere intrinsic_sphere{radius};
    exact_geometry = ExactGeometry{std::move(intrinsic_sphere)};
  } else if (usd_geometry.GetPrim().IsA<pxr::UsdGeomCube>()) {
    const pxr::UsdGeomCube usd_cube{usd_geometry};
    INTR_ASSIGN_OR_RETURN(const double size,
                          GetDoubleOrFloat(usd_cube.GetSizeAttr()));
    shapes::Box intrinsic_box(eigenmath::Vector3d(size, size, size));
    exact_geometry = ExactGeometry{std::move(intrinsic_box)};
  } else if (usd_geometry.GetPrim().IsA<pxr::UsdGeomCylinder>()) {
    const pxr::UsdGeomCylinder usd_cylinder{usd_geometry};
    INTR_ASSIGN_OR_RETURN(const double height,
                          GetDoubleOrFloat(usd_cylinder.GetHeightAttr()));
    INTR_ASSIGN_OR_RETURN(const double radius,
                          GetDoubleOrFloat(usd_cylinder.GetRadiusAttr()));
    INTR_ASSIGN_OR_RETURN(auto axis_vector,
                          ParseAxisAttribute(usd_cylinder.GetAxisAttr()));
    // Our shapes::Cylinder is Z-axis aligned, so we must use a rotated
    // pose here.
    shapes::Cylinder intrinsic_cylinder(height, radius);
    geo::TransformedPrimitiveShapePtr transformed_shape{
        std::make_shared<shapes::Cylinder>(intrinsic_cylinder),
        GetRotatedPose(eigenmath::Vector3d{0, 0, 1}, axis_vector)};
    INTR_ASSIGN_OR_RETURN(exact_geometry,
                          ExactGeometry::Create(transformed_shape));
  } else if (usd_geometry.GetPrim().IsA<pxr::UsdGeomCapsule>()) {
    const pxr::UsdGeomCapsule usd_capsule{usd_geometry};
    INTR_ASSIGN_OR_RETURN(const double height,
                          GetDoubleOrFloat(usd_capsule.GetHeightAttr()));
    INTR_ASSIGN_OR_RETURN(const double radius,
                          GetDoubleOrFloat(usd_capsule.GetRadiusAttr()));
    INTR_ASSIGN_OR_RETURN(auto axis_vector,
                          ParseAxisAttribute(usd_capsule.GetAxisAttr()));
    // Our shapes::Capsule is Z-axis aligned, so we must use a rotated
    // pose here.
    shapes::Capsule intrinsic_capsule(height, radius);
    geo::TransformedPrimitiveShapePtr transformed_shape{
        std::make_shared<shapes::Capsule>(intrinsic_capsule),
        GetRotatedPose(eigenmath::Vector3d{0, 0, 1}, axis_vector)};
    INTR_ASSIGN_OR_RETURN(exact_geometry,
                          ExactGeometry::Create(transformed_shape));
  } else {
    return absl::InvalidArgumentError(absl::Substitute(
        "This USD primitive geometry type is not yet supported: $0",
        usd_geometry.GetPrim().GetDescription()));
  }

  // We generate a Renderable for the Geometry only if it has a material. The
  // renderable will contain the shape converted to a mesh + the material.
  // If the primitive shape has no material, there is no need to generate a
  // renderable.
  auto material_properties = TryGetMaterial(usd_geometry.GetPrim());
  std::shared_ptr<const Renderable> renderable;
  if (material_properties.has_value()) {
    INTR_ASSIGN_OR_RETURN(renderable, GenerateRenderableForExactGeometry(
                                          exact_geometry, material_properties));
  }

  const bool keep_renderable = renderable != nullptr;
  return Geometry{std::move(exact_geometry), renderable, keep_renderable,
                  std::nullopt};
}

absl::StatusOr<std::optional<intrinsic_proto::geometry::v1::MaterialProperties>>
ParseMaterialForPrim(const pxr::UsdPrim& primitive) {
  // We wrap the primitive in a UsdShadeMaterialBindingAPI and compute the bound
  // material. Note that the primitive actually does -not- need to have the
  // 'MaterialBindingAPI' applied. Many assets have 'rel material:binding'
  // without the API applied. The call to ComputeBoundMaterial will work
  // regardless. It looks for the all-purpose material of the geometry.
  pxr::UsdShadeMaterialBindingAPI prim_with_material{primitive};
  pxr::UsdShadeMaterial material = prim_with_material.ComputeBoundMaterial();
  if (!material) {
    // No material found
    return std::nullopt;
  }

  auto makeMaterialError = [&](absl::string_view message) {
    return absl::InvalidArgumentError(
        absl::Substitute("Error parsing Material $0: $1",
                         material.GetPath().GetString(), message));
  };

  // Resolve the Material's surface shader.
  // We only support materials connected to a single surface shader.
  //
  // First, check for a surface output in the universal/default render context.
  // UsdPreviewSurface shaders will be placed here.
  // If one isn't found, check for a surface output in the mdl render context.
  // Nvidia uses the mdl context to specify OmniPBR shaders on the material,
  // like, `token outputs:mdl:surface.connect = ...`.
  pxr::UsdShadeConnectableAPI connected_source;
  pxr::UsdShadeOutput surface_output = material.GetSurfaceOutput();
  pxr::UsdShadeOutput mdl_surface_output =
      material.GetSurfaceOutput(pxr::TfToken("mdl"));
  if (surface_output && surface_output.GetConnectedSources().size() == 1) {
    connected_source = surface_output.GetConnectedSources()[0].source;
  } else if (mdl_surface_output &&
             mdl_surface_output.GetConnectedSources().size() == 1) {
    connected_source = mdl_surface_output.GetConnectedSources()[0].source;
  }
  if (!connected_source ||
      !connected_source.GetPrim().IsA<pxr::UsdShadeShader>()) {
    return makeMaterialError(
        "Could not find a supported shader. We only support materials where "
        "the surface output is connected to a UsdPreviewSurface or OmniPBR "
        "shader.");
  }

  // First, check if the shader is a UsdPreviewSurface.
  // If not, check if it is a OmniPBR shader.
  pxr::UsdShadeShader shader{connected_source.GetPrim()};
  pxr::TfToken shader_id;
  if (shader.GetShaderId(&shader_id) &&
      shader_id == pxr::TfToken("UsdPreviewSurface")) {
    INTR_ASSIGN_OR_RETURN(auto props,
                          ParseMaterialFromUsdPreviewSurfaceShader(shader));
    return props;
  }

  // If info:id is not present, check if it's an MDL shader.
  pxr::TfToken mdl_subidentifier;
  pxr::UsdAttribute sub_id_attr = shader.GetPrim().GetAttribute(
      pxr::TfToken("info:mdl:sourceAsset:subIdentifier"));
  if (sub_id_attr && sub_id_attr.Get(&mdl_subidentifier) &&
      mdl_subidentifier == pxr::TfToken("OmniPBR")) {
    INTR_ASSIGN_OR_RETURN(auto props, ParseMaterialFromOmniPbrShader(shader));
    return props;
  }

  return absl::InvalidArgumentError(absl::Substitute(
      "Shader type '$0' is not yet supported.", shader_id.GetText()));
}

std::optional<intrinsic_proto::geometry::v1::MaterialProperties> TryGetMaterial(
    const pxr::UsdPrim& prim) {
  auto result = ParseMaterialForPrim(prim);
  if (!result.ok()) {
    LOG(WARNING) << "Failed to parse material for "
                 << prim.GetPath().GetString()
                 << ". Falling back to default material. Error: "
                 << result.status().message();
    return std::nullopt;
  }
  return *result;
}

absl::StatusOr<intrinsic_proto::geometry::v1::MaterialProperties>
ParseMaterialFromUsdPreviewSurfaceShader(
    const pxr::UsdShadeShader& preview_surface_shader) {
  intrinsic_proto::geometry::v1::MaterialProperties props;

  // Base Color (diffuseColor)
  pxr::UsdShadeInput diffuse_input =
      preview_surface_shader.GetInput(pxr::TfToken("diffuseColor"));
  if (pxr::GfVec3f diffuse_color;
      diffuse_input && diffuse_input.Get(&diffuse_color)) {
    auto* base_color = props.mutable_base_color();
    base_color->set_red(diffuse_color[0]);
    base_color->set_green(diffuse_color[1]);
    base_color->set_blue(diffuse_color[2]);
  }

  // Metalness (metallic)
  pxr::UsdShadeInput metallic_input =
      preview_surface_shader.GetInput(pxr::TfToken("metallic"));
  if (float metallic{0.0}; metallic_input && metallic_input.Get(&metallic)) {
    props.set_metalness(metallic);
  }

  // Roughness
  pxr::UsdShadeInput roughness_input =
      preview_surface_shader.GetInput(pxr::TfToken("roughness"));
  if (float roughness{0.0};
      roughness_input && roughness_input.Get(&roughness)) {
    props.set_roughness(roughness);
  }

  // Transmission (opacity)
  pxr::UsdShadeInput opacity_input =
      preview_surface_shader.GetInput(pxr::TfToken("opacity"));
  if (float opacity{0.0}; opacity_input && opacity_input.Get(&opacity)) {
    props.set_transmission(1.0 - opacity);
  }

  // Warn on unsupported/ignored fields:
  std::vector<std::string> supported_inputs = {
      "diffuseColor",
      "metallic",
      "roughness",
      "opacity",
  };
  LogWarningOnUnsupportedShaderInputs(preview_surface_shader, supported_inputs);

  return props;
}

absl::StatusOr<intrinsic_proto::geometry::v1::MaterialProperties>
ParseMaterialFromOmniPbrShader(const pxr::UsdShadeShader& omni_shader) {
  intrinsic_proto::geometry::v1::MaterialProperties props;

  // See here for fields reference:
  // https://docs.omniverse.nvidia.com/materials-and-rendering/latest/templates/OmniPBR.html

  // Base Color (diffuse_color_constant)
  pxr::UsdShadeInput diffuse_input =
      omni_shader.GetInput(pxr::TfToken("diffuse_color_constant"));
  if (pxr::GfVec3f diffuse_color;
      diffuse_input && diffuse_input.Get(&diffuse_color)) {
    auto* base_color = props.mutable_base_color();
    base_color->set_red(diffuse_color[0]);
    base_color->set_green(diffuse_color[1]);
    base_color->set_blue(diffuse_color[2]);
  }

  // Metalness (metallic_constant)
  pxr::UsdShadeInput metallic_input =
      omni_shader.GetInput(pxr::TfToken("metallic_constant"));
  if (float metallic{0.0}; metallic_input && metallic_input.Get(&metallic)) {
    props.set_metalness(metallic);
  }

  // Roughness (reflection_roughness_constant)
  pxr::UsdShadeInput roughness_input =
      omni_shader.GetInput(pxr::TfToken("reflection_roughness_constant"));
  if (float roughness{0.0};
      roughness_input && roughness_input.Get(&roughness)) {
    props.set_roughness(roughness);
  }

  // Transmission (opacity_constant)
  // OmniPBR has an 'enable_opacity' flag. If true, it uses 'opacity_constant'.
  pxr::UsdShadeInput enable_opacity_input =
      omni_shader.GetInput(pxr::TfToken("enable_opacity"));
  if (bool enable_opacity{false}; enable_opacity_input &&
                                  enable_opacity_input.Get(&enable_opacity) &&
                                  enable_opacity) {
    pxr::UsdShadeInput opacity_input =
        omni_shader.GetInput(pxr::TfToken("opacity_constant"));
    if (float opacity{1.0}; opacity_input && opacity_input.Get(&opacity)) {
      props.set_transmission(1.0 - opacity);
    }
  }

  std::vector<std::string> supported_inputs = {
      "diffuse_color_constant",
      "metallic_constant",
      "reflection_roughness_constant",
      "enable_opacity",
      "opacity_constant",
  };
  LogWarningOnUnsupportedShaderInputs(omni_shader, supported_inputs);

  return props;
}

void LogWarningOnUnsupportedShaderInputs(
    const pxr::UsdShadeShader& shader,
    const std::vector<std::string>& supported_inputs) {
  auto unsupported_inputs =
      GetUnsupportedShaderInputs(shader, supported_inputs);
  if (!unsupported_inputs.empty()) {
    LOG(WARNING) << "Shader " << shader.GetPath().GetString()
                 << " uses unsupported inputs, which will be ignored: "
                 << absl::StrJoin(unsupported_inputs, ", ")
                 << ". Supported inputs are: "
                 << absl::StrJoin(supported_inputs, ", ");
  }
}

std::vector<std::string> GetUnsupportedShaderInputs(
    const pxr::UsdShadeShader& shader,
    const std::vector<std::string>& supported_inputs) {
  std::vector<std::string> unsupported_inputs;
  for (const auto& input : shader.GetInputs()) {
    std::string input_name = input.GetBaseName().GetString();
    if (input && std::find(supported_inputs.begin(), supported_inputs.end(),
                           input_name) == supported_inputs.end()) {
      unsupported_inputs.push_back(input_name);
    }
  }
  return unsupported_inputs;
}

std::string UsdUniqueNamer::GetNameForPrim(const pxr::UsdPrim& prim) {
  const std::string& prim_path = prim.GetPath().GetString();
  if (prim_path_to_name_.contains(prim_path)) {
    // Already has an assigned name
    return prim_path_to_name_[prim_path];
  }
  std::string entity_name = FindUnusedName(prim.GetName().GetString());
  prim_path_to_name_[prim_path] = entity_name;
  used_names_.insert(entity_name);
  return entity_name;
}

std::string UsdUniqueNamer::FindUnusedName(const std::string& prim_name) {
  std::string entity_name{prim_name};
  // prim_name should never be the empty string or the root path ('/'), but
  // handle these cases just to be safe. Prim names themselves, other than
  // the root, cannot contain slashes.
  if (entity_name.empty() || entity_name == "/") {
    entity_name = "Entity";
  }

  if (used_names_.contains(entity_name)) {
    // Fixup the entity name if needed. Check if ends in a `_<num>`, and
    // increment that number if so.
    std::string base_name = entity_name;
    int suffix_to_use = 2;

    const size_t last_underscore = entity_name.find_last_of('_');
    if (last_underscore != std::string::npos &&
        last_underscore < entity_name.size() - 1) {
      const absl::string_view suffix_str =
          absl::string_view(entity_name).substr(last_underscore + 1);
      int current_suffix{0};
      if (absl::SimpleAtoi(suffix_str, &current_suffix)) {
        base_name = entity_name.substr(0, last_underscore);
        suffix_to_use = current_suffix + 1;
      }
    }

    do {
      entity_name = absl::StrCat(base_name, "_", suffix_to_use++);
    } while (used_names_.contains(entity_name));
  }

  return entity_name;
}

}  // namespace usd
}  // namespace intrinsic
