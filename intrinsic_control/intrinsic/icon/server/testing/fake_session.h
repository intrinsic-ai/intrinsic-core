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

#ifndef INTRINSIC_ICON_SERVER_TESTING_FAKE_SESSION_H_
#define INTRINSIC_ICON_SERVER_TESTING_FAKE_SESSION_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_group.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/streaming_output.pb.h"
#include "intrinsic/icon/server/session_interface.h"

namespace intrinsic {
namespace icon {

class FakeSession : public SessionInterface {
 public:
  // Creates a FakeSession that manages a session with the `part_names`
  // assigned to it.
  static std::unique_ptr<SessionInterface> Create(
      const absl::flat_hash_set<std::string>& part_names, SessionId session_id);

  explicit FakeSession(PartGroup part_group)
      : part_group_(std::move(part_group)) {}

  ~FakeSession() override = default;

  // Disallow move.
  FakeSession(FakeSession&&) = delete;
  FakeSession& operator=(FakeSession&&) = delete;

  // Disable copy.
  FakeSession(const FakeSession&) = delete;
  FakeSession& operator=(const FakeSession&) = delete;

  absl::Status AddActionsAndReactions(
      const ActionsAndReactions& actions_and_reactions) override;

  absl::Status RemoveActionsAndReactions(
      const ActionAndReactionIds& action_and_reaction_ids) override;

  absl::Status RemoveAllActionsAndReactions() override;

  absl::Status StartActions(
      absl::Span<const ActionInstanceId> action_instance_ids,
      bool stop_active_actions) override;

  absl::StatusOr<std::optional<ReactionEvent>> PollReactions() override;

  absl::Status WriteToStreamingInput(
      ActionInstanceId action_instance_id, absl::string_view input_name,
      const google::protobuf::Any& value) override;

  absl::StatusOr<::intrinsic_proto::icon::StreamingOutput>
  GetLatestStreamingOutput(ActionInstanceId action_instance_id,
                           absl::Time deadline) override;

  absl::StatusOr<::intrinsic_proto::icon::JointTrajectoryPVA>
  GetPlannedTrajectory(ActionInstanceId action_instance_id) override;

  PartGroup GetPartGroup() const override;

  absl::flat_hash_set<ActionInstanceId> GetActionInstanceIds() const override;

 private:
  PartGroup part_group_;
};

}  // namespace icon
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_SERVER_TESTING_FAKE_SESSION_H_
