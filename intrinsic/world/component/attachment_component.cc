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

#include "intrinsic/world/component/attachment_component.h"

#include <memory>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/proto/attachment_component.pb.h"

namespace intrinsic {

namespace {

class AttachmentComponentImpl : public AttachmentComponent {
 public:
  AttachmentComponentImpl(AttachmentEntityId parent_id, Pose3d parent_t_this,
                          std::optional<absl::Time> timestamp,
                          bool inaccurate_pose, bool fixed_in_root)
      : parent_id_(parent_id),
        parent_t_this_(std::move(parent_t_this)),
        timestamp_(timestamp),
        inaccurate_pose_(inaccurate_pose),
        fixed_in_root_(fixed_in_root) {}

  absl::StatusOr<intrinsic_proto::world::AttachmentComponent> ToProto()
      const override;
  std::unique_ptr<AttachmentComponent> Clone() const override;

  AttachmentEntityId GetParentId() const override;
  void SetParentId(AttachmentEntityId handle) override;

  Pose3d GetParentTThis() const override;
  std::optional<absl::Time> GetTimestamp() const override;
  void SetParentTThis(
      const Pose3d& parent_t_this,
      std::optional<absl::Time> timestamp = std::nullopt) override;
  bool IsInaccurate() const override;
  void MarkInaccurate(bool inaccurate) override;
  bool IsFixedInRoot() const override;
  void MarkFixedInRoot(bool fixed_in_root) override;

  absl::Status UpdateFromProto(
      const intrinsic_proto::world::AttachmentComponent& proto) override;

 private:
  AttachmentEntityId parent_id_;
  Pose3d parent_t_this_;
  std::optional<absl::Time> timestamp_;
  bool inaccurate_pose_;
  bool fixed_in_root_;
};

AttachmentEntityId AttachmentComponentImpl::GetParentId() const {
  return parent_id_;
}

void AttachmentComponentImpl::SetParentId(AttachmentEntityId handle) {
  parent_id_ = handle;
}

Pose3d AttachmentComponentImpl::GetParentTThis() const {
  return parent_t_this_;
}

std::optional<absl::Time> AttachmentComponentImpl::GetTimestamp() const {
  return timestamp_;
}

void AttachmentComponentImpl::SetParentTThis(
    const Pose3d& parent_t_this, std::optional<absl::Time> timestamp) {
  parent_t_this_ = parent_t_this;
  timestamp_ = timestamp;
}

bool AttachmentComponentImpl::IsInaccurate() const { return inaccurate_pose_; }

void AttachmentComponentImpl::MarkInaccurate(bool inaccurate) {
  inaccurate_pose_ = inaccurate;
}

bool AttachmentComponentImpl::IsFixedInRoot() const { return fixed_in_root_; }

void AttachmentComponentImpl::MarkFixedInRoot(bool fixed_in_root) {
  fixed_in_root_ = fixed_in_root;
}

absl::StatusOr<intrinsic_proto::world::AttachmentComponent>
AttachmentComponentImpl::ToProto() const {
  intrinsic_proto::world::AttachmentComponent result;
  result.set_parent_uid(parent_id_.id.value());
  result.set_inaccurate_pose(inaccurate_pose_);
  result.set_fixed_in_root(fixed_in_root_);
  *result.mutable_parent_t_this() = intrinsic::ToProto(parent_t_this_);
  if (timestamp_.has_value()) {
    INTR_ASSIGN_OR_RETURN(*result.mutable_wall_clock_timestamp(),
                          intrinsic::FromAbslTime(*timestamp_));
  }

  return std::move(result);
}

std::unique_ptr<AttachmentComponent> AttachmentComponentImpl::Clone() const {
  return std::make_unique<AttachmentComponentImpl>(
      parent_id_, parent_t_this_, timestamp_, inaccurate_pose_, fixed_in_root_);
}

absl::Status AttachmentComponentImpl::UpdateFromProto(
    const intrinsic_proto::world::AttachmentComponent& proto) {
  if (proto.has_parent_t_this()) {
    INTR_ASSIGN_OR_RETURN(parent_t_this_,
                          intrinsic_proto::FromProto(proto.parent_t_this()),
                          _ << "Failed to parse parent_t_this from "
                               "AttachmentComponent::UpdateFromProto");
  }

  parent_id_ = AttachmentEntityId(proto.parent_uid());
  inaccurate_pose_ = proto.inaccurate_pose();
  fixed_in_root_ = proto.fixed_in_root();
  if (proto.has_wall_clock_timestamp()) {
    INTR_ASSIGN_OR_RETURN(timestamp_,
                          intrinsic::ToAbslTime(proto.wall_clock_timestamp()));
  } else {
    timestamp_ = std::nullopt;
  }
  return absl::OkStatus();
}

}  // namespace

std::unique_ptr<AttachmentComponent> AttachmentComponent::Create() {
  return std::make_unique<AttachmentComponentImpl>(
      AttachmentEntityId(kInvalidEntityId), Pose3d::Identity(),
      /*timestamp=*/std::nullopt,
      /*inaccurate_pose=*/false, /*fixed_in_root=*/false);
}

absl::StatusOr<std::unique_ptr<AttachmentComponent>>
AttachmentComponent::FromProto(
    const intrinsic_proto::world::AttachmentComponent& proto) {
  Pose3d parent_t_this;
  if (proto.has_parent_t_this()) {
    INTR_ASSIGN_OR_RETURN(parent_t_this,
                          intrinsic_proto::FromProto(proto.parent_t_this()));
  }

  std::optional<absl::Time> timestamp = std::nullopt;
  if (proto.has_wall_clock_timestamp()) {
    INTR_ASSIGN_OR_RETURN(timestamp,
                          intrinsic::ToAbslTime(proto.wall_clock_timestamp()));
  }

  // TODO(stoyang): Validate parent_id has the attachment component.
  std::unique_ptr<AttachmentComponent> result =
      std::make_unique<AttachmentComponentImpl>(
          AttachmentEntityId(proto.parent_uid()), parent_t_this, timestamp,
          /*inaccurate_pose=*/proto.inaccurate_pose(),
          /*fixed_in_root=*/proto.fixed_in_root());

  return std::move(result);
}

}  // namespace intrinsic
