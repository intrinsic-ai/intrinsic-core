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

#ifndef INTRINSIC_ICON_SERVER_TESTING_SESSION_MOCK_H_
#define INTRINSIC_ICON_SERVER_TESTING_SESSION_MOCK_H_

#include <gmock/gmock.h>

#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_group.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/server/session_interface.h"

namespace intrinsic {
namespace icon {

class SessionMock : public SessionInterface {
 public:
  MOCK_METHOD(absl::Status, AddActionsAndReactions,
              (const ActionsAndReactions&), (override));

  MOCK_METHOD(absl::Status, RemoveActionsAndReactions,
              (const ActionAndReactionIds&), (override));

  MOCK_METHOD(absl::Status, RemoveAllActionsAndReactions, (), (override));

  MOCK_METHOD(absl::Status, StartActions,
              (absl::Span<const ActionInstanceId> action_instance_ids,
               bool stop_active_actions),
              (override));

  MOCK_METHOD(absl::StatusOr<std::optional<ReactionEvent>>, PollReactions, (),
              (override));

  MOCK_METHOD(absl::Status, WriteToStreamingInput,
              (ActionInstanceId action_instance_id,
               absl::string_view input_name,
               const google::protobuf::Any& value),
              (override));

  MOCK_METHOD(absl::StatusOr<::intrinsic_proto::icon::StreamingOutput>,
              GetLatestStreamingOutput,
              (ActionInstanceId action_instance_id, absl::Time deadline),
              (override));

  MOCK_METHOD(absl::StatusOr<::intrinsic_proto::icon::JointTrajectoryPVA>,
              GetPlannedTrajectory, (ActionInstanceId action_instance_id),
              (override));

  MOCK_METHOD(PartGroup, GetPartGroup, (), (const, override));

  MOCK_METHOD(absl::flat_hash_set<ActionInstanceId>, GetActionInstanceIds, (),
              (const, override));
};

}  // namespace icon
}  // namespace intrinsic

#endif  // INTRINSIC_ICON_SERVER_TESTING_SESSION_MOCK_H_
