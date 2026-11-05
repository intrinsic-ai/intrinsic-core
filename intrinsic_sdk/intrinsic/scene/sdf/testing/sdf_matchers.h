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

#ifndef INTRINSIC_SCENE_SDF_TESTING_SDF_MATCHERS_H_
#define INTRINSIC_SCENE_SDF_TESTING_SDF_MATCHERS_H_

#include <string>

#include "gmock/gmock.h"
#include "gz/math/eigen3/Conversions.hh"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/simulation/gazebo/type_conversion.h"
#include "sdf/Box.hh"
#include "sdf/Capsule.hh"
#include "sdf/Collision.hh"
#include "sdf/Cylinder.hh"
#include "sdf/Element.hh"
#include "sdf/Ellipsoid.hh"
#include "sdf/Geometry.hh"
#include "sdf/Joint.hh"
#include "sdf/Material.hh"
#include "sdf/Mesh.hh"
#include "sdf/Sphere.hh"
#include "sdf/Visual.hh"

namespace intrinsic {
namespace sdf {

// Handy SDF matchers for testing.

MATCHER_P(HasMeshUri, uri_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  if (arg->Geom() == nullptr) {
    *result_listener << "has no geometry";
    return false;
  }
  if (arg->Geom()->MeshShape() == nullptr) {
    *result_listener << "has no mesh shape";
    return false;
  }
  std::string actual_uri = arg->Geom()->MeshShape()->Uri();
  *result_listener << "whose mesh uri is "
                   << ::testing::PrintToString(actual_uri);
  return ExplainMatchResult(uri_matcher, actual_uri, result_listener);
}

MATCHER_P(HasName, name_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  *result_listener << "whose name is " << ::testing::PrintToString(arg->Name());
  return ExplainMatchResult(name_matcher, arg->Name(), result_listener);
}

MATCHER_P(HasRawPose, pose_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  Pose3d actual_pose = GzToIntrinsic(arg->RawPose());
  *result_listener << "whose pose is " << ::testing::PrintToString(actual_pose);
  return ExplainMatchResult(pose_matcher, actual_pose, result_listener);
}

MATCHER_P(HasBoxShape, size_matcher, "") {
  if (arg == nullptr || arg->Geom() == nullptr ||
      arg->Geom()->BoxShape() == nullptr) {
    *result_listener << "does not have a box shape";
    return false;
  }
  eigenmath::Vector3d actual_size =
      ::gz::math::eigen3::convert(arg->Geom()->BoxShape()->Size());
  *result_listener << "whose size is " << ::testing::PrintToString(actual_size);
  return ExplainMatchResult(size_matcher, actual_size, result_listener);
}

MATCHER_P(HasSphereShape, radius_matcher, "") {
  if (arg == nullptr || arg->Geom() == nullptr ||
      arg->Geom()->SphereShape() == nullptr) {
    *result_listener << "does not have a sphere shape";
    return false;
  }
  double actual_radius = arg->Geom()->SphereShape()->Radius();
  *result_listener << "whose radius is "
                   << ::testing::PrintToString(actual_radius);
  return ExplainMatchResult(radius_matcher, actual_radius, result_listener);
}

MATCHER_P2(HasCylinderShape, radius_matcher, length_matcher, "") {
  if (arg == nullptr || arg->Geom() == nullptr ||
      arg->Geom()->CylinderShape() == nullptr) {
    *result_listener << "does not have a cylinder shape";
    return false;
  }
  double actual_radius = arg->Geom()->CylinderShape()->Radius();
  double actual_length = arg->Geom()->CylinderShape()->Length();
  *result_listener << "whose radius is "
                   << ::testing::PrintToString(actual_radius);
  if (!ExplainMatchResult(radius_matcher, actual_radius, result_listener)) {
    return false;
  }
  *result_listener << " and whose length is "
                   << ::testing::PrintToString(actual_length);
  return ExplainMatchResult(length_matcher, actual_length, result_listener);
}

MATCHER_P2(HasCapsuleShape, radius_matcher, length_matcher, "") {
  if (arg == nullptr || arg->Geom() == nullptr ||
      arg->Geom()->CapsuleShape() == nullptr) {
    *result_listener << "does not have a capsule shape";
    return false;
  }
  double actual_radius = arg->Geom()->CapsuleShape()->Radius();
  double actual_length = arg->Geom()->CapsuleShape()->Length();
  *result_listener << "whose radius is "
                   << ::testing::PrintToString(actual_radius);
  if (!ExplainMatchResult(radius_matcher, actual_radius, result_listener)) {
    return false;
  }
  *result_listener << " and whose length is "
                   << ::testing::PrintToString(actual_length);
  return ExplainMatchResult(length_matcher, actual_length, result_listener);
}

MATCHER_P4(IsColor, r, g, b, a, "") {
  return ExplainMatchResult(
      ::testing::AllOf(
          ::testing::Property("R", &gz::math::Color::R,
                              ::testing::FloatEq(static_cast<float>(r))),
          ::testing::Property("G", &gz::math::Color::G,
                              ::testing::FloatEq(static_cast<float>(g))),
          ::testing::Property("B", &gz::math::Color::B,
                              ::testing::FloatEq(static_cast<float>(b))),
          ::testing::Property("A", &gz::math::Color::A,
                              ::testing::FloatEq(static_cast<float>(a)))),
      arg, result_listener);
}

MATCHER_P(HasDiffuseColor, color_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  if (arg->Material() == nullptr) {
    *result_listener << "has no material";
    return false;
  }
  *result_listener << "whose diffuse color is "
                   << ::testing::PrintToString(arg->Material()->Diffuse());
  return ExplainMatchResult(color_matcher, arg->Material()->Diffuse(),
                            result_listener);
}

MATCHER_P(HasAmbientColor, color_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  if (arg->Material() == nullptr) {
    *result_listener << "has no material";
    return false;
  }
  *result_listener << "whose ambient color is "
                   << ::testing::PrintToString(arg->Material()->Ambient());
  return ExplainMatchResult(color_matcher, arg->Material()->Ambient(),
                            result_listener);
}

MATCHER_P(HasMetalness, matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  ::sdf::ElementPtr mat_elem = arg->Element()->FindElement("material");
  if (mat_elem == nullptr) {
    *result_listener << "has no material element";
    return false;
  }
  ::sdf::ElementPtr pbr_elem = mat_elem->FindElement("pbr");
  if (pbr_elem == nullptr) {
    *result_listener << "has no pbr element";
    return false;
  }
  ::sdf::ElementPtr metal_elem = pbr_elem->FindElement("metal");
  if (metal_elem == nullptr) {
    *result_listener << "has no metal element";
    return false;
  }
  double actual_metalness = metal_elem->Get<double>("metalness");
  *result_listener << "whose metalness is " << actual_metalness;
  return ExplainMatchResult(matcher, actual_metalness, result_listener);
}

MATCHER_P(HasRoughness, matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  ::sdf::ElementPtr mat_elem = arg->Element()->FindElement("material");
  if (mat_elem == nullptr) {
    *result_listener << "has no material element";
    return false;
  }
  ::sdf::ElementPtr pbr_elem = mat_elem->FindElement("pbr");
  if (pbr_elem == nullptr) {
    *result_listener << "has no pbr element";
    return false;
  }
  ::sdf::ElementPtr metal_elem = pbr_elem->FindElement("metal");
  if (metal_elem == nullptr) {
    *result_listener << "has no metal element";
    return false;
  }
  double actual_roughness = metal_elem->Get<double>("roughness");
  *result_listener << "whose roughness is " << actual_roughness;
  return ExplainMatchResult(matcher, actual_roughness, result_listener);
}

MATCHER_P(HasTransmissionFactor, matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  ::sdf::ElementPtr mat_elem = arg->Element()->FindElement("material");
  if (mat_elem == nullptr) {
    *result_listener << "has no material element";
    return false;
  }
  ::sdf::ElementPtr pbr_elem = mat_elem->FindElement("pbr");
  if (pbr_elem == nullptr) {
    *result_listener << "has no pbr element";
    return false;
  }
  ::sdf::ElementPtr metal_elem = pbr_elem->FindElement("metal");
  if (metal_elem == nullptr) {
    *result_listener << "has no metal element";
    return false;
  }
  double actual_transmission = metal_elem->Get<double>("transmission_factor");
  *result_listener << "whose transmission factor is " << actual_transmission;
  return ExplainMatchResult(matcher, actual_transmission, result_listener);
}

MATCHER_P(HasEllipsoidShape, radii_matcher, "") {
  if (arg == nullptr || arg->Geom() == nullptr ||
      arg->Geom()->EllipsoidShape() == nullptr) {
    *result_listener << "does not have an ellipsoid shape";
    return false;
  }
  eigenmath::Vector3d actual_radii =
      ::gz::math::eigen3::convert(arg->Geom()->EllipsoidShape()->Radii());
  *result_listener << "whose radii is "
                   << ::testing::PrintToString(actual_radii);
  return ExplainMatchResult(radii_matcher, actual_radii, result_listener);
}

MATCHER_P(HasScale, scale_matcher, "") {
  if (arg == nullptr || arg->Geom() == nullptr ||
      arg->Geom()->MeshShape() == nullptr) {
    *result_listener << "does not have a mesh shape with scale";
    return false;
  }
  eigenmath::Vector3d actual_scale =
      ::gz::math::eigen3::convert(arg->Geom()->MeshShape()->Scale());
  *result_listener << "whose scale is "
                   << ::testing::PrintToString(actual_scale);
  return ExplainMatchResult(scale_matcher, actual_scale, result_listener);
}

MATCHER_P(HasMass, mass_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  double actual_mass = arg->Inertial().MassMatrix().Mass();
  *result_listener << "whose mass is " << actual_mass;
  return ExplainMatchResult(mass_matcher, actual_mass, result_listener);
}

MATCHER_P(HasInertialPose, pose_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  Pose3d actual_pose = GzToIntrinsic(arg->Inertial().Pose());
  *result_listener << "whose inertial pose is "
                   << ::testing::PrintToString(actual_pose);
  return ExplainMatchResult(pose_matcher, actual_pose, result_listener);
}

MATCHER_P(HasCollisionCount, count_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  uint64_t actual_count = arg->CollisionCount();
  *result_listener << "whose collision count is " << actual_count;
  return ExplainMatchResult(count_matcher, actual_count, result_listener);
}

MATCHER_P(HasVisualCount, count_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  uint64_t actual_count = arg->VisualCount();
  *result_listener << "whose visual count is " << actual_count;
  return ExplainMatchResult(count_matcher, actual_count, result_listener);
}

MATCHER_P(HasJointType, type_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  ::sdf::JointType actual_type = arg->Type();
  *result_listener << "whose joint type is " << static_cast<int>(actual_type);
  return ExplainMatchResult(type_matcher, actual_type, result_listener);
}

MATCHER_P(HasParentName, name_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  *result_listener << "whose parent name is " << arg->ParentName();
  return ExplainMatchResult(name_matcher, arg->ParentName(), result_listener);
}

MATCHER_P(HasChildName, name_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  *result_listener << "whose child name is " << arg->ChildName();
  return ExplainMatchResult(name_matcher, arg->ChildName(), result_listener);
}

MATCHER_P(HasInertiaDiagonal, diag_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  auto actual_diag = arg->Inertial().MassMatrix().DiagonalMoments();
  *result_listener << "whose inertia diagonal is " << actual_diag;
  return ExplainMatchResult(diag_matcher, actual_diag, result_listener);
}

MATCHER_P(HasInertiaOffDiagonal, off_diag_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  auto actual_off_diag = arg->Inertial().MassMatrix().OffDiagonalMoments();
  *result_listener << "whose inertia off-diagonal is " << actual_off_diag;
  return ExplainMatchResult(off_diag_matcher, actual_off_diag, result_listener);
}

MATCHER_P(HasAutoInertia, bool_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  bool actual = arg->AutoInertia();
  *result_listener << "whose auto_inertia is " << (actual ? "true" : "false");
  return ExplainMatchResult(bool_matcher, actual, result_listener);
}

MATCHER_P(HasDensity, double_matcher, "") {
  if (arg == nullptr) {
    *result_listener << "is nullptr";
    return false;
  }
  if (!arg->Density().has_value()) {
    *result_listener << "has no density value";
    return false;
  }
  double actual = arg->Density().value();
  *result_listener << "whose density is " << actual;
  return ExplainMatchResult(double_matcher, actual, result_listener);
}

}  // namespace sdf
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SDF_TESTING_SDF_MATCHERS_H_
