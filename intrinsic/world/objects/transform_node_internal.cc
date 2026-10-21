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
#include "absl/strings/substitute.h"
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
#include "tf2/include/tf2/transform_datatypes.h"

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

// Resolves the transform entity ID for `node` and `node_filter`. Reuses
// `this_id` when `node` and `node_filter` match `this_node` and `this_filter`
// to avoid a duplicate entity lookup.
absl::StatusOr<AttachmentEntityId> ResolveTransformEntityId(
    const TransformNode* this_node,
    const world::ObjectEntityFilter& this_filter, AttachmentEntityId this_id,
    const TransformNode* node, const world::ObjectEntityFilter& node_filter) {
  if (node == this_node && node_filter == this_filter) {
    return this_id;
  }
  return node->GetTransformEntityId(node_filter);
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
  const world::ObjectEntityFilter& kBaseFilter =
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
  const world::ObjectEntityFilter& kBaseFilter =
      world::ObjectEntityFilter::BaseEntity();

  if (!bypass_movable_check) {
    INTR_RETURN_IF_ERROR(CheckIsMovable(this_filter));
  }
  const world::ObjectEntityFilter& resolved_this_filter =
      this_filter.has_value() ? *this_filter : kBaseFilter;
  const world::ObjectEntityFilter& resolved_node_a_filter =
      node_a_filter.has_value() ? *node_a_filter : kBaseFilter;
  const world::ObjectEntityFilter& resolved_node_b_filter =
      node_b_filter.has_value() ? *node_b_filter : kBaseFilter;
  INTR_ASSIGN_OR_RETURN(const AttachmentEntityId this_id,
                        GetTransformEntityId(resolved_this_filter));

  INTR_ASSIGN_OR_RETURN(
      const AttachmentComponent* attachment,
      GetEntityWorld().GetComponentByEntityId<AttachmentComponent>(this_id));
  INTR_ASSIGN_OR_RETURN(
      const AttachmentEntityId a_id,
      ResolveTransformEntityId(this, resolved_this_filter, this_id, node_a,
                               resolved_node_a_filter));
  INTR_ASSIGN_OR_RETURN(
      const AttachmentEntityId b_id,
      ResolveTransformEntityId(this, resolved_this_filter, this_id, node_b,
                               resolved_node_b_filter));

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

  // Direct parent-child fast path.
  if ((b_id == this_id && attachment->GetParentId() == a_id) ||
      (a_id == this_id && attachment->GetParentId() == b_id)) {
    const std::optional<absl::Time> existing_timestamp =
        attachment->GetTimestamp();
    if (timestamp.has_value() && existing_timestamp.has_value() &&
        *timestamp < *existing_timestamp) {
      return absl::InvalidArgumentError(absl::Substitute(
          "Cannot update transform for '$0' because its timestamp $1 is newer "
          "than the given timestamp $2",
          this_id.value(), absl::FormatTime(*existing_timestamp),
          absl::FormatTime(*timestamp)));
    }
    const Pose3d parent_t_this =
        (b_id == this_id) ? a_t_b : Pose3d(a_t_b.inverse());
    INTR_ASSIGN_OR_RETURN(
        AttachmentComponent * mutable_attachment,
        world->GetComponentByEntityId<AttachmentComponent>(this_id));
    mutable_attachment->SetParentTThis(parent_t_this, timestamp);
    mutable_attachment->MarkInaccurate(false);
    return absl::OkStatus();
  }

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
