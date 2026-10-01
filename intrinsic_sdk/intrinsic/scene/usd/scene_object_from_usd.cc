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

#include "intrinsic/scene/usd/scene_object_from_usd.h"

#include <pxr/base/tf/errorMark.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdPhysics/fixedJoint.h>
#include <pxr/usd/usdPhysics/joint.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>

#include <filesystem>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/entity.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/usd/connection_graph.h"
#include "intrinsic/scene/usd/entity_from_usd.h"
#include "intrinsic/scene/usd/utils.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "ortools/base/temp_path.h"

namespace fs = std::filesystem;

namespace intrinsic {
namespace usd {

namespace {

absl::StatusOr<Pose3d> ComputeLinkParentTThisPose(
    const ConnectionTree::LinkNode* absl_nonnull link_node,
    pxr::UsdGeomXformCache& xform_cache) {
  if (!link_node->parent_node) {
    // Is this is a toplevel link, return the identity. There should only be
    // one toplevel link in the SceneObject.
    LOG(WARNING) << "The root node " << link_node->GetPrimPath()
                 << " has a world space transform, but this will be ignored. "
                    "It will be positioned at the origin.";
    return Pose3d::Identity();
  }

  if (auto parent_link =
          dynamic_cast<const ConnectionTree::LinkNode* absl_nonnull>(
              link_node->parent_node);
      parent_link) {
    // The parent is another link, which we have a FixedJoint connection to.
    // Our parent_t_this is the relative pose from parent prim to this prim.
    // The actual attachment point of the fixed joint is not significant here.
    return GetRelativePose(link_node->rigid_body.GetPrim(),
                           parent_link->rigid_body.GetPrim(), xform_cache);
  } else if (auto parent_joint =
                 dynamic_cast<const ConnectionTree::JointNode* absl_nonnull>(
                     link_node->parent_node);
             parent_joint) {
    // By convention, the parent_t_this of a joint's child is the identity
    // transform. The joint entity's parent_t_this will describe the relative
    // transform from the joint's parent link to child link.
    // Convention stated here: world/conversion/sdf/joint_conversion.h
    // Related ticket: b/361105294
    return Pose3d::Identity();
  } else {
    return absl::InternalError(
        "Unexpected parent node type. Expected LinkNode or JointNode.");
  }
}

}  // namespace

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromUsdStage(const pxr::UsdStageRefPtr& stage,
                        GeometrySerializer& geometry_serializer) {
  INTR_RETURN_IF_ERROR(internal::PreprocessUsdStage(stage))
      << "Failed to preprocess the USD stage";
  INTR_ASSIGN_OR_RETURN(const pxr::UsdPrim root_prim, GetRootPrim(stage),
                        _ << "Could not find the root prim of the stage.");

  // We currently only support metersPerUnit = 1 (the default)
  // TODO(b/471230322) - Add support for other values
  const double meters_per_unit = pxr::UsdGeomGetStageMetersPerUnit(stage);
  if (meters_per_unit != 1.0) {
    return absl::InvalidArgumentError(absl::Substitute(
        "We require that metersPerUnit is equal to 1.0 Found: $0",
        meters_per_unit));
  }

  // We convert the USD stage to our SceneObject Entity hierarchy in
  // a couple steps:
  //
  // 1. Create a graph of the rigid-body and joint hierarchy
  // 2. Compute a tree from this graph (erroring if we find cycles)
  // 3. Use this tree to create our SceneObject Entity hierarchy
  //
  // Refer to the USD docs here:
  // https://openusd.org/release/api/usd_physics_page_front.html
  // https://openusd.org/release/api/usd_geom_page_front.html

  PhysicsPrims physics_prims = GetPhysicsPrims(root_prim);
  if (physics_prims.rigid_bodies.empty()) {
    return absl::InvalidArgumentError(
        "No rigid bodies were found in the USD stage");
  }
  std::optional<std::string> root_link_path = TryGetRootLinkPath(root_prim);

  pxr::UsdGeomXformCache xform_cache;
  INTR_ASSIGN_OR_RETURN(auto connection_graph,
                        ComputeConnectionGraph(physics_prims, xform_cache),
                        _ << "Failed to compute connection graph");
  INTR_ASSIGN_OR_RETURN(
      auto connection_tree,
      ComputeConnectionTreeFromGraph(connection_graph, root_link_path),
      _ << "Failed to compute connection tree");

  // Validate that we only have 1 root link. This is required by SceneObject
  // proto. We should always have >0 toplevel links if the stage
  // contains rigid bodies (checked earlier). If we find >1
  // toplevel links, the given UsdStage is not supported by us (argument error).
  std::vector<const ConnectionTree::LinkNode* absl_nonnull> toplevel_links =
      connection_tree.GetToplevelLinks();
  INTR_RET_CHECK(toplevel_links.size() > 0) << "No root link found";
  if (toplevel_links.size() > 1) {
    std::vector<std::string> toplevel_link_names;
    for (const auto* link : toplevel_links) {
      toplevel_link_names.push_back(link->GetPrimPath());
    }
    return absl::InvalidArgumentError(absl::Substitute(
        "There can only be one root rigid-body in the USD stage. "
        " Found $0: $1",
        toplevel_links.size(), absl::StrJoin(toplevel_link_names, ", ")));
  }

  // Do a DFS traversal of the connection-tree, starting from the root link.
  // Create SceneObject Entities as we go.
  UsdUniqueNamer entity_namer;
  intrinsic_proto::scene_object::v1::SceneObject scene_object;
  scene_object.set_name(entity_namer.GetNameForPrim(root_prim));
  const auto* root_link = toplevel_links[0];
  std::vector<const ConnectionTree::BaseNode* absl_nonnull> stack = {
      root_link,
  };
  while (!stack.empty()) {
    const auto* current_node = stack.back();
    stack.pop_back();

    auto* entity = scene_object.mutable_entities()->Add();

    if (auto link_node =
            dynamic_cast<const ConnectionTree::LinkNode* absl_nonnull>(
                current_node);
        link_node) {
      entity->set_name(entity_namer.GetNameForPrim(link_node->GetPrim()));
      if (link_node->parent_node) {
        entity->set_parent_name(
            entity_namer.GetNameForPrim(link_node->parent_node->GetPrim()));
      }

      INTR_ASSIGN_OR_RETURN(
          *entity->mutable_link(),
          LinkProtoFromRigidBody(link_node->rigid_body, xform_cache,
                                 geometry_serializer),
          _ << absl::Substitute("failed to parse rigid body $0",
                                link_node->GetPrimPath()));

      INTR_ASSIGN_OR_RETURN(auto parent_t_this,
                            ComputeLinkParentTThisPose(link_node, xform_cache));
      *entity->mutable_parent_t_this() = ToProto(parent_t_this);

      // Push children to stack for processing
      for (auto it = link_node->child_joints.rbegin();
           it != link_node->child_joints.rend(); ++it) {
        stack.push_back(*it);
      }
      for (auto it = link_node->child_links.rbegin();
           it != link_node->child_links.rend(); ++it) {
        stack.push_back(*it);
      }
    } else if (auto joint_node =
                   dynamic_cast<const ConnectionTree::JointNode* absl_nonnull>(
                       current_node);
               joint_node) {
      entity->set_name(entity_namer.GetNameForPrim(joint_node->GetPrim()));
      INTR_RET_CHECK(joint_node->parent_link) << absl::Substitute(
          "Joint $0 has no parent link", joint_node->GetPrimPath());
      entity->set_parent_name(
          entity_namer.GetNameForPrim(joint_node->parent_link->GetPrim()));

      // Find the required polarity.
      // If body1 is the parent, must flip the polarity.
      JointPolarity joint_polarity{JointPolarity::Body0IsParent};
      INTR_ASSIGN_OR_RETURN(auto original_joint_bodies,
                            ParseJointBodies(joint_node->joint, xform_cache));
      if (original_joint_bodies.body1.GetPrim() ==
          joint_node->parent_link->rigid_body.GetPrim()) {
        joint_polarity = JointPolarity::Body1IsParent;
      }

      INTR_ASSIGN_OR_RETURN(auto joint_proto_with_info,
                            JointProtoFromUsdJoint(joint_node->joint,
                                                   joint_polarity, xform_cache),
                            _ << absl::Substitute("failed to parse joint $0",
                                                  joint_node->GetPrimPath()));
      *entity->mutable_joint() = joint_proto_with_info.joint_proto;

      // By convention, we set the joint's parent_t_this transform to
      // body0_t_joint * joint_t_body1, and we set body1's parent_t_this
      // to the identity. During simulation, the joint's parent_t_this is
      // updated according to the formula:
      // parent_t_this = body0_t_joint * inbound_t_outbound * joint_t_body1
      //
      // Convention stated here: world/conversion/sdf/joint_conversion.h
      // Related ticket: b/361105294
      auto joint_bodies = joint_proto_with_info.joint_bodies;
      Pose3d parent_t_this =
          joint_bodies.body0_t_joint * joint_bodies.body1_t_joint.inverse();
      *entity->mutable_parent_t_this() = ToProto(parent_t_this);

      // Push child to stack for processing
      stack.push_back(joint_node->child_link);
    } else {
      return absl::InternalError(
          "Unexpected BaseNode type. Expected LinkNode or JointNode.");
    }
  }

  return scene_object;
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromUsdFileData(absl::string_view file_name,
                           absl::string_view file_contents,
                           GeometrySerializer& geometry_serializer) {
  // To properly import the file with the correct file-type, USD requires that
  // the file be read from disk (or read from memory using a custom resolver,
  // which we avoid for now). We write the file_contents to disk then load to a
  // stage. When writing to a temp-file, preserve the filename + suffix. USD
  // uses the suffix to detect what type of USD file it is.
  TempPath temp_dir("/tmp/");
  // `TempPath` creates the directory but does not remove it on destruction, so
  // the cleanup is done here.
  absl::Cleanup temp_dir_cleanup = [&temp_dir]() {
    std::error_code ec;
    fs::remove_all(temp_dir.path(), ec);
    if (ec) {
      LOG(WARNING) << "Failed to delete the temporary directory "
                   << temp_dir.path() << ": " << ec.message();
    }
  };
  const std::string temp_file =
      fs::path(temp_dir.path()) / fs::path(file_name).filename();
  INTR_RETURN_IF_ERROR(
      file::SetContents(temp_file, file_contents, file::Defaults()))
      << "Failed to write the contents of " << file_name << " to " << temp_file;

  return SceneObjectFromUsdFile(temp_file, geometry_serializer);
}

absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObject>
SceneObjectFromUsdFile(absl::string_view file_path,
                       GeometrySerializer& geometry_serializer) {
  pxr::TfErrorMark error_mark;
  auto stage = pxr::UsdStage::Open(std::string(file_path));
  if (!stage) {
    return absl::InvalidArgumentError(absl::Substitute(
        "Failed to load file $0. Details: $1",
        fs::path(file_path).filename().string(), GetErrorString(error_mark)));
  }
  return SceneObjectFromUsdStage(stage, geometry_serializer);
}

absl::flat_hash_set<std::string> SupportedUsdExtensions() {
  return absl::flat_hash_set<std::string>({"usdz", "usda", "usdc", "usd"});
}

namespace internal {

absl::Status PreprocessUsdStage(const pxr::UsdStageRefPtr& stage) {
  // If the Stage contains no links (UsdPhysicsRigidBody), then we add one at
  // the top level so that we can still parse all the geometry in the file as if
  // it is part of link.
  INTR_ASSIGN_OR_RETURN(pxr::UsdPrim root_prim, GetRootPrim(stage));
  if (root_prim.IsPseudoRoot()) {
    // We cannot set a UsdPhysicsRigidBodyAPI on the pseudo-root, so use its
    // first child.
    if (!root_prim.GetChildren().empty()) {
      root_prim = *root_prim.GetChildren().begin();
    } else {
      return absl::InvalidArgumentError("The USD stage is empty.");
    }
  }
  PhysicsPrims physics_prims = GetPhysicsPrims(root_prim);
  if (physics_prims.rigid_bodies.empty()) {
    pxr::UsdPhysicsRigidBodyAPI::Apply(root_prim);
  }
  return absl::OkStatus();
}

}  // namespace internal

}  // namespace usd
}  // namespace intrinsic
