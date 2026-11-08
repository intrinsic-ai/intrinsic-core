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

#include "intrinsic/icon/server/testing/fake_session.h"

#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_group.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/streaming_output.pb.h"
#include "intrinsic/icon/server/session_interface.h"

namespace intrinsic {
namespace icon {

// static
std::unique_ptr<SessionInterface> FakeSession::Create(
    const absl::flat_hash_set<std::string>& part_names, SessionId session_id) {
  return std::make_unique<FakeSession>(
      PartGroup(part_names.begin(), part_names.end()));
}

absl::Status FakeSession::AddActionsAndReactions(
    const ActionsAndReactions& actions_and_reactions) {
  return absl::UnimplementedError("Not implemented.");
}

absl::Status FakeSession::RemoveActionsAndReactions(
    const ActionAndReactionIds& action_and_reaction_ids) {
  return absl::UnimplementedError("Not implemented.");
}

absl::Status FakeSession::RemoveAllActionsAndReactions() {
  return absl::UnimplementedError("Not implemented.");
}

absl::Status FakeSession::StartActions(
    absl::Span<const ActionInstanceId> action_instance_ids,
    bool stop_active_actions) {
  return absl::UnimplementedError("Not implemented.");
}

absl::StatusOr<std::optional<ReactionEvent>> FakeSession::PollReactions() {
  LOG(ERROR) << "Unimplemented";
  return std::optional<ReactionEvent>();
}

absl::Status FakeSession::WriteToStreamingInput(
    ActionInstanceId action_instance_id, absl::string_view input_name,
    const google::protobuf::Any& value) {
  return absl::UnimplementedError("Not implemented.");
}

absl::StatusOr<::intrinsic_proto::icon::StreamingOutput>
FakeSession::GetLatestStreamingOutput(ActionInstanceId action_instance_id,
                                      absl::Time deadline) {
  return absl::UnimplementedError("Not implemented.");
}

absl::StatusOr<::intrinsic_proto::icon::JointTrajectoryPVA>
FakeSession::GetPlannedTrajectory(ActionInstanceId action_instance_id) {
  return absl::UnimplementedError("Not implemented.");
}

PartGroup FakeSession::GetPartGroup() const { return part_group_; }

absl::flat_hash_set<ActionInstanceId> FakeSession::GetActionInstanceIds()
    const {
  // Since the Add* and Remove* methods are no-ops (and return
  // kUnimplementedError), we can always return the empty set here.
  return {};
}

}  // namespace icon
}  // namespace intrinsic
