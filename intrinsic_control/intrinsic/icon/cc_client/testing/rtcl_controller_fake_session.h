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

#ifndef INTRINSIC_ICON_CC_CLIENT_TESTING_RTCL_CONTROLLER_FAKE_SESSION_H_
#define INTRINSIC_ICON_CC_CLIENT_TESTING_RTCL_CONTROLLER_FAKE_SESSION_H_

#include <any>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_group.h"
#include "intrinsic/icon/control/realtime_bridge_types.h"
#include "intrinsic/icon/control/realtime_signal_storage.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/icon/server/testing/fake_operational_state.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"

namespace intrinsic::icon {

// RtclControllerFakeSession fakes the behavior of an RtclController-based RTCL
// system.
//
// It runs the requested Actions and Reactions at a high level by
// evaluating intrinsic::icon::SessionWill and intrinsic::icon::ActionWill
// objects. "At a high level" here means that the SessionWill and ActionWill
// objects define the behavior of each Action and the robot status completely.
// However, the fake still invokes the original Actions' factory method (which
// checks Fixed parameters for correctness). It also follows the same logic as
// RtclController when evaluating Part/Action compatibility and triggering
// Reactions.
//
// RtclControllerFakeSession supports all action types that are registered with
// the RTCL. There are two ways to register action types:
// 1. Link the action's `register` target into the test binary.
// 2. Load a plugin action `.so` before running your test code (using
//    intrinsic/icon/server/custom_action_plugin_loader.h)
//
// Depending on whether an RtclControllerFakeSession object is strict or not, it
// may tolerate deviations from the exact sequence of events expected by its
// SessionWill object, or else return errors from its interface methods.
//
// Updates to active Actions/Expectations, as well as the simulated time stamp
// are driven by PollReactions(). That is, for each call of PollReactions(), the
// current action advances through its list of behaviors (updating state
// variables and/or robot status) by one, and the fake executes any Reactions
// that are triggered by the updated state variables.
//
// Note that the time stamp always increases in 1ns steps.
class RtclControllerFakeSession : public SessionInterface {
 public:
  enum class ValidationMode { kStrict, kNice };

  // If this instance is strict, checks for any unmet expectations. Unmet
  // expectations cause a test failure.
  ~RtclControllerFakeSession() override;

  // This fake session will check the ActionWill expectations from
  // `session_expectation`. `part_configs_by_name` contains PartConfig protos
  // that are passed to action factories during AddActionsAndReactions() for all
  // parts which are controlled by this session. `validation_mode` determines
  // whether or not unexpected actions and unmet ActionWill expectations cause
  // test errors. The fake session updates `operational_state` and update the
  // robot status using `new_status` according to the ActionWill expectations.
  // The fake session calls `get_robot_status` to retrieve the current robot
  // status. `part_name_to_index_map` is used to convert the robot status
  // returned by `get_robot_status` into a `PublishOutput`. If
  // `part_name_to_index_map` is not set, the order of part names in
  // `part_configs_by_name` will be used to create the indices.
  RtclControllerFakeSession(
      const SessionWill& session_expectation,
      const absl::flat_hash_map<std::string,
                                intrinsic_proto::icon::v1::PartConfig>&
          part_configs_by_name,
      ValidationMode validation_mode, FakeOperationalState& operational_state,
      std::function<
          void(const intrinsic_proto::icon::v1::GetStatusResponse& new_status)>
          update_robot_status,
      std::function<intrinsic_proto::icon::v1::GetStatusResponse()>
          get_robot_status,
      const absl::flat_hash_map<std::string, size_t>* const
          part_name_to_index_map = nullptr,
      double frequency_hz = 1000.0)
      : frequency_hz_(frequency_hz),
        operational_state_(operational_state),
        update_robot_status_(std::move(update_robot_status)),
        get_robot_status_(std::move(get_robot_status)),
        validation_mode_(validation_mode),
        part_configs_by_name_(part_configs_by_name) {
    if (part_name_to_index_map != nullptr) {
      part_name_to_index_map_ = *part_name_to_index_map;
    } else {
      size_t part_index = 0;
      for (auto& [part_name, config] : part_configs_by_name) {
        part_name_to_index_map_[part_name] = part_index;
        part_index++;
      }
    }

    absl::c_copy(session_expectation.InOrderActionBehaviors(),
                 std::back_inserter(ordered_action_behaviors_));
    // Activate the first behavior if it is action independent.
    if (!ordered_action_behaviors_.empty() &&
        !ordered_action_behaviors_.front().action_id.has_value()) {
      active_action_behavior_ = ordered_action_behaviors_.front();
      ordered_action_behaviors_.pop_front();
    }
  }

  // Adds a collection of reactions and actions to the state machine that
  // is being cyclically evaluated.
  //
  // Returns AlreadyExists if any Actions or Reactions use IDs that already
  // exist in this Session.
  // Returns FailedPrecondition if the RtclControllerFakeSession is strict, and
  // there are no more unfulfilled ActionWill expectations.
  // Returns FailedPrecondition if the RtclControllerFakeSession is strict, and
  // the there is no expectation with the ActionInstanceId of an added action.
  // Returns NotFound if there is no factory for any of the added actions.
  // Forwards any errors from action factories.
  // Forwards any errors from user-supplied action parameter matchers.
  // Returns NotFound if any of the reactions in `actions_and_reactions` refers
  // to an ActionInstanceId that does not belong to any action.
  // Returns AlreadyExists if there are duplicate reaction IDs.
  absl::Status AddActionsAndReactions(
      const ActionsAndReactions& actions_and_reactions)
      ABSL_LOCKS_EXCLUDED(mutex_) override;

  // Removes the collection of reactions and action instances from the fake
  // session.
  absl::Status RemoveActionsAndReactions(
      const ActionAndReactionIds& action_and_reaction_ids)
      ABSL_LOCKS_EXCLUDED(mutex_) override;

  // Removes all actions and reactions that have been previously added.
  absl::Status RemoveAllActionsAndReactions() override;

  // Starts the requested action if possible. Any ActionWill behaviors for that
  // action will run the next time PollReactions() is called. Starting multiple
  // actions or starting actions in parallel in not implemented yet.
  absl::Status StartActions(
      absl::Span<const ActionInstanceId> action_instance_ids,
      bool stop_active_actions) ABSL_LOCKS_EXCLUDED(mutex_) override;

  // Advances the ActionWill expectations of the current active action, if any,
  // and returns any Reactions that this may trigger.
  // Such reactions may change the active action.
  absl::StatusOr<std::optional<ReactionEvent>> PollReactions()
      ABSL_LOCKS_EXCLUDED(mutex_) override;

  // Streaming inputs and outputs are not implemented for ChannelFake yet.
  absl::Status WriteToStreamingInput(ActionInstanceId action_instance_id,
                                     absl::string_view input_name,
                                     const google::protobuf::Any& value)
      ABSL_LOCKS_EXCLUDED(mutex_) override;

  // Streaming inputs and outputs are not implemented for ChannelFake yet.
  absl::StatusOr<::intrinsic_proto::icon::StreamingOutput>
  GetLatestStreamingOutput(ActionInstanceId action_instance_id,
                           absl::Time deadline)
      ABSL_LOCKS_EXCLUDED(mutex_) override;

  // If the factory of the action with `action_instance_id` saved a trajectory,
  // returns that trajectory.
  // Returns `kFailedPrecondition` if  there is no trajectory for the given
  // `action_instance_id`.
  absl::StatusOr<::intrinsic_proto::icon::JointTrajectoryPVA>
  GetPlannedTrajectory(ActionInstanceId action_instance_id)
      ABSL_LOCKS_EXCLUDED(mutex_) override;

  // Returns the Parts that this Session is using.
  PartGroup GetPartGroup() const ABSL_LOCKS_EXCLUDED(mutex_) override;

  // Returns the ActionInstanceIds of all actions that are currently known to
  // the Session (i.e. those that have been added, but not yet removed).
  absl::flat_hash_set<ActionInstanceId> GetActionInstanceIds() const
      ABSL_LOCKS_EXCLUDED(mutex_) override;

  absl::flat_hash_set<ActionInstanceId> GetActiveActionIds() const
      ABSL_LOCKS_EXCLUDED(mutex_) {
    absl::MutexLock lock(mutex_);
    return active_action_ids_;
  }

 private:
  // Helper struct to hold the ADIO block names together with the PublishOutput,
  // which has pointers to those names.
  struct PublishOutputWithAdioNames {
    std::vector<std::vector<std::string>> adio_block_names;
    PublishOutput publish_output;
  };

  absl::Status AddAction(const ActionDescription& action);
  absl::Status AddReaction(const intrinsic_proto::icon::v1::Reaction& reaction);

  // Updates the current active actions, which includes a check for whether the
  // session's expectations expect `id` to become active next. The action is
  // added to `active_action_ids_`.
  absl::Status UpdateActiveAction(ActionInstanceId id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void StopActiveAction(ActionInstanceId action_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void StopAllActiveActions() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Checks if the current behavior is finished and activates the next behavior
  // of the sequence. If `target_action_id` is provided in nice validation mode,
  // advances to the next behavior for `target_action_id`.
  absl::Status ActivateNextBehavior(
      std::optional<ActionInstanceId> target_action_id = std::nullopt)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Resets the current behavior. In strict mode, also checks if the current
  // behavior is done (i.e. we've used all of its state variable maps), and
  // returns an error otherwise.
  absl::Status ResetActiveBehaviorIfDone()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Generates and returns the PublishOutput from the current value returned by
  // `get_robot_status_()`. Also returns the ADIO block names, which *must*
  // outlive the PublishOutput or any copies of the PublishOutput.
  absl::StatusOr<PublishOutputWithAdioNames> GeneratePublishOutput() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  absl::Status WriteOutput(const std::any& output,
                           StreamingOutputChannel* output_channel);

  struct ActionData {
    ActionDescription description;
    // Neither of these are moveable, so we wrap them into unique_ptrs.
    std::unique_ptr<RtclActionInterface> action;
    std::unique_ptr<StreamingIoStorage> io_storage;
    // RealtimeSignalStorage is expected to be moveable.
    RealtimeSignalStorage signal_storage;
    size_t poll_reactions_count = 0;
    size_t next_streaming_input_parameter_matcher_index = 0;
  };

  mutable absl::Mutex mutex_;
  double frequency_hz_ ABSL_GUARDED_BY(mutex_);
  FakeOperationalState& operational_state_ ABSL_GUARDED_BY(mutex_);
  std::function<
      void(const intrinsic_proto::icon::v1::GetStatusResponse& new_status)>
      update_robot_status_ ABSL_GUARDED_BY(mutex_);
  std::function<intrinsic_proto::icon::v1::GetStatusResponse()>
      get_robot_status_ ABSL_GUARDED_BY(mutex_);
  ValidationMode validation_mode_ ABSL_GUARDED_BY(mutex_);
  std::list<SessionWill::ActionAndBehaviors> ordered_action_behaviors_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, intrinsic_proto::icon::v1::PartConfig>
      part_configs_by_name_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<std::string, size_t> part_name_to_index_map_;
  absl::flat_hash_map<ActionInstanceId, ActionData> actions_by_id_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<ReactionId, intrinsic_proto::icon::v1::Reaction>
      reactions_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<ActionInstanceId> active_action_ids_
      ABSL_GUARDED_BY(mutex_) = {};
  std::optional<SessionWill::ActionAndBehaviors> active_action_behavior_
      ABSL_GUARDED_BY(mutex_) = std::nullopt;
  std::vector<ReactionEvent> outstanding_reactions_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<ReactionId> reactions_fired_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<ReactionId, bool> previous_reaction_states_
      ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA> trajectory_map_
      ABSL_GUARDED_BY(mutex_);
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CC_CLIENT_TESTING_RTCL_CONTROLLER_FAKE_SESSION_H_
