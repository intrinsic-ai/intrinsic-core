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

#include "intrinsic/world/objects/transform_node_internal.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/util/aggregate_type.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/objects/frame_internal.h"
#include "intrinsic/world/objects/object_entity_filter.h"
#include "intrinsic/world/objects/object_world_data.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/world.h"
#include "tf2/transform_datatypes.hpp"

namespace intrinsic {
namespace object_world {

namespace {

class PrintNameVisitor : public TransformNodeConstVisitor {
 public:
  PrintNameVisitor() = default;

  absl::Status Visit(const Frame& frame) override {
    std::string parent_name =
        absl::StrCat("Object: ", frame.GetParent()->GetName());
    if (const Frame* parent_frame = frame.GetParentFrame();
        parent_frame != nullptr) {
      absl::StrAppend(&parent_name,
                      " -- Parent Frame: ", parent_frame->GetName());
    }
    printed_name_ =
        absl::StrCat("Frame: ", frame.GetName(), " (", parent_name, ")");
    return absl::OkStatus();
  }
  absl::Status Visit(const WorldObject& object) override {
    printed_name_ = absl::StrCat("Object: ", object.GetName());
    return absl::OkStatus();
  }

  std::string GetPrintedName() const { return printed_name_; }

 private:
  std::string printed_name_;
};

std::string GetTransformNodeName(const TransformNode& node) {
  PrintNameVisitor visitor;
  absl::Status visit_status = node.Accept(visitor);
  if (!visit_status.ok()) {
    LOG(ERROR) << visit_status;
    return "<ERROR (check logs)>";
  }

  return visitor.GetPrintedName();
}

}  // namespace

TransformNode::TransformNode(ObjectWorldResourceId id, WorldObject* parent,
                             ObjectWorldData& data)
    : data_(data), id_(std::move(id)), parent_(parent) {}

void TransformNode::SetParentAsymmetric(WorldObject* parent) {
  parent_ = parent;
}

absl::StatusOr<Pose3d> TransformNode::GetTransform(const TransformNode* other,
                                                   absl::Time timestamp) const {
  return GetTransform(std::nullopt, other, std::nullopt, timestamp);
}

absl::StatusOr<Pose3d> TransformNode::GetTransform(
    std::optional<world::ObjectEntityFilter> this_filter,
    const TransformNode* other,
    std::optional<world::ObjectEntityFilter> other_filter,
    absl::Time timestamp) const {
  AttachmentEntityId this_id{kInvalidEntityId};
  if (this_filter.has_value()) {
    INTR_ASSIGN_OR_RETURN(this_id, GetTransformEntityId(*this_filter));
  } else {
    INTR_ASSIGN_OR_RETURN(this_id, GetTransformOriginEntityId());
  }
  AttachmentEntityId other_id{kInvalidEntityId};
  if (other_filter.has_value()) {
    INTR_ASSIGN_OR_RETURN(other_id, other->GetTransformEntityId(*other_filter));
  } else {
    INTR_ASSIGN_OR_RETURN(other_id, other->GetTransformOriginEntityId());
  }

  tf2::Stamped<Pose3d> transform_out;
  INTR_RETURN_IF_ERROR(GetEntityWorld().GetTransform(this_id, other_id,
                                                     timestamp, transform_out));
  return transform_out;
}

absl::Status TransformNode::SetTransform(const TransformNode* a,
                                         const TransformNode* b,
                                         const Pose3d& a_t_b,
                                         std::optional<absl::Time> timestamp) {
  const world::ObjectEntityFilter kBaseFilter =
      world::ObjectEntityFilter::BaseEntity();
  return SetTransform(kBaseFilter, a, kBaseFilter, b, kBaseFilter, a_t_b,
                      timestamp);
}

absl::Status TransformNode::SetTransform(
    std::optional<world::ObjectEntityFilter> this_filter,
    const TransformNode* node_a,
    std::optional<world::ObjectEntityFilter> node_a_filter,
    const TransformNode* node_b,
    std::optional<world::ObjectEntityFilter> node_b_filter, const Pose3d& a_t_b,
    std::optional<absl::Time> timestamp, bool bypass_movable_check) {
  const world::ObjectEntityFilter kBaseFilter =
      world::ObjectEntityFilter::BaseEntity();

  if (!bypass_movable_check) {
    INTR_RETURN_IF_ERROR(CheckIsMovable(this_filter));
  }
  INTR_ASSIGN_OR_RETURN(
      const AttachmentEntityId this_id,
      GetTransformEntityId(this_filter.value_or(kBaseFilter)));

  INTR_ASSIGN_OR_RETURN(
      const AttachmentComponent* attachment,
      GetEntityWorld().GetComponentByEntityId<AttachmentComponent>(this_id));
  INTR_ASSIGN_OR_RETURN(
      const AttachmentEntityId a_id,
      node_a->GetTransformEntityId(node_a_filter.value_or(kBaseFilter)));
  INTR_ASSIGN_OR_RETURN(
      const AttachmentEntityId b_id,
      node_b->GetTransformEntityId(node_b_filter.value_or(kBaseFilter)));

  if (a_id == b_id) {
    if (this_id != a_id) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Nodes a and b are identical, so updating the transform of '",
          GetTransformNodeName(*this),
          "' which is different, is meaningless."));
    }

    if (!a_t_b.isApprox(Pose3d())) {
      return absl::InvalidArgumentError(
          "Nodes a and b are identical, so cannot update with a non-identity "
          "pose.");
    }

    // Nothing to do.
    return absl::OkStatus();
  }

  INTR_ASSIGN_OR_RETURN(World * world, GetMutableEntityWorld());
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId common_ancestor_id,
                        world->FindCommonAncestor(a_id, b_id));
  if (common_ancestor_id == this_id) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Cannot update the transform of '", GetTransformNodeName(*this),
        "' since both nodes a '", GetTransformNodeName(*node_a), "' and b '",
        GetTransformNodeName(*node_b), "' are descendants."));
  }

  // Since we know that the node to update is not the common ancestor of A and
  // B, we can check to make sure that this node is an ancestor of either A or
  // B, since otherwise updating the transform of this node will not affect A
  // nor B.
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId this_along_a,
                        world->FindCommonAncestor(this_id, a_id));
  INTR_ASSIGN_OR_RETURN(AttachmentEntityId this_along_b,
                        world->FindCommonAncestor(this_id, b_id));
  if (this_along_a != this_id && this_along_b != this_id) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Cannot update the transform of '", GetTransformNodeName(*this),
        "' since it is neither a parent of a '", GetTransformNodeName(*node_a),
        "' nor b '", GetTransformNodeName(*node_b), "'."));
  }

  INTR_RETURN_IF_ERROR(
      world->MarkTransformInaccuracy(this_id, attachment->GetParentId(), true));
  INTR_ASSIGN_OR_RETURN(
      AttachmentEntityId updated_id,
      world->UpdateIndirectTransform(a_id, b_id, a_t_b, timestamp));
  INTR_RET_CHECK_EQ(this_id, updated_id);
  return absl::OkStatus();
}

}  // namespace object_world
}  // namespace intrinsic
