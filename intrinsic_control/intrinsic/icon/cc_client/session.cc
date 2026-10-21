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

#include "intrinsic/icon/cc_client/session.h"

#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "google/rpc/status.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "grpcpp/support/time.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/proto/concatenate_trajectory_protos.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/v1/service.grpc.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread.h"

namespace intrinsic {
namespace icon {

namespace {

constexpr char kAlreadyEndedErrorMessage[] = "The Session has already ended.";

// Reads out the stream's read buffer until failure, and ends the call. Returns
// the call status.
absl::Status CleanUpCallAfterClientWritesDone(
    grpc::ClientReaderWriterInterface<
        intrinsic_proto::icon::v1::OpenSessionRequest,
        intrinsic_proto::icon::v1::OpenSessionResponse>* stream) {
  intrinsic_proto::icon::v1::OpenSessionResponse response_message;
  // Clear out any response messages from the read queue. Our API suggests
  // that there shouldn't be any, since we haven't sent a request, but to
  // protect against server-side bugs that would deadlock or crash the client
  // by erroneously sending responses, we should read until explicit failure
  // before Finish()ing the stream.
  while (stream->Read(&response_message)) {
    LOG(ERROR) << "Received unexpected response from the server:"
               << response_message;
  }
  return ToAbslStatus(stream->Finish());
}

// Writes a message to the server and reads the response. Returns an error if
// either reading or writing fails, in which case the call is dead.
absl::StatusOr<intrinsic_proto::icon::v1::OpenSessionResponse>
WriteMessageAndReadResponse(
    const intrinsic_proto::icon::v1::OpenSessionRequest& request,
    grpc::ClientReaderWriterInterface<
        intrinsic_proto::icon::v1::OpenSessionRequest,
        intrinsic_proto::icon::v1::OpenSessionResponse>* stream) {
  constexpr char kAbortedErrorMsg[] = "Communication with server failed.";
  if (!stream->Write(request)) {
    return absl::AbortedError(kAbortedErrorMsg);
  }

  intrinsic_proto::icon::v1::OpenSessionResponse response;
  if (!stream->Read(&response)) {
    return absl::AbortedError(kAbortedErrorMsg);
  }

  return response;
}

// Initializes the action session and returns the session id if successful. Ends
// the call on failure. On success, the `stream` is done sending and receiving
// initial_session_data.
absl::StatusOr<SessionId> InitializeSessionOrEndCall(
    grpc::ClientReaderWriterInterface<
        intrinsic_proto::icon::v1::OpenSessionRequest,
        intrinsic_proto::icon::v1::OpenSessionResponse>* stream,
    absl::Span<const std::string> parts,
    const intrinsic_proto::data_logger::Context& context,
    std::optional<absl::Time> deadline) {
  intrinsic_proto::icon::v1::OpenSessionRequest request_message;
  *request_message.mutable_initial_session_data()
       ->mutable_allocate_parts()
       ->mutable_part() = {parts.begin(), parts.end()};
  if (deadline.has_value()) {
    *request_message.mutable_initial_session_data()->mutable_deadline() =
        ::intrinsic::FromAbslTimeClampToValidRange(*deadline);
  }
  *request_message.mutable_log_context() = context;

  absl::StatusOr<intrinsic_proto::icon::v1::OpenSessionResponse>
      status_or_response = WriteMessageAndReadResponse(request_message, stream);

  if (!status_or_response.ok()) {
    INTR_RETURN_IF_ERROR(CleanUpCallAfterClientWritesDone(stream));
    return status_or_response.status();
  }

  const intrinsic_proto::icon::v1::OpenSessionResponse& response_message =
      *status_or_response;

  if (absl::Status status =
          intrinsic::MakeStatusFromRpcStatus(response_message.status());
      !status.ok()) {
    // We don't expect to receive an error at this point in time, so if we do
    // end the call, since this is an unexpected state.
    stream->WritesDone();
    // Return the first error to the caller, and log the call failure error if
    // it occurs during cleanup.
    if (absl::Status cleanup_status = CleanUpCallAfterClientWritesDone(stream);
        !cleanup_status.ok()) {
      LOG(ERROR) << "Call finished with status: " << status;
    }
    return status;
  }

  if (!response_message.has_initial_session_data()) {
    // The server should've sent back initial session data, but didn't. Kill the
    // session.
    stream->WritesDone();
    // Return the first error to the caller, and log the call failure error if
    // it occurs during cleanup.
    if (absl::Status status = CleanUpCallAfterClientWritesDone(stream);
        !status.ok()) {
      LOG(ERROR) << "Call finished with status: " << status;
    }

    return absl::InternalError(
        "Did not receive initial session data from the server");
  }

  return SessionId(response_message.initial_session_data().session_id());
}

}  // namespace

ReactionDescriptor::ReactionDescriptor(const Condition& condition)
    : condition_(condition) {}

ReactionDescriptor& ReactionDescriptor::WithHandle(
    ReactionHandle reaction_handle, intrinsic::SourceLocation loc) {
  reaction_handle_ = {reaction_handle, loc};
  return *this;
}

ReactionDescriptor& ReactionDescriptor::WithRealtimeActionOnCondition(
    ActionInstanceId action_id) {
  action_id_ = action_id;
  stop_associated_action_ = true;
  return *this;
}

ReactionDescriptor& ReactionDescriptor::WithParallelRealtimeActionOnCondition(
    ActionInstanceId action_id) {
  action_id_ = action_id;
  stop_associated_action_ = false;
  return *this;
}

ReactionDescriptor& ReactionDescriptor::WithWatcherOnCondition(
    std::function<void()> on_condition) {
  on_condition_ = std::move(on_condition);
  return *this;
}

ReactionDescriptor& ReactionDescriptor::FireOnce(bool enable) {
  fire_once_ = enable;
  return *this;
}

ReactionDescriptor& ReactionDescriptor::WithRealtimeSignalOnCondition(
    absl::string_view realtime_signal_name) {
  realtime_signal_name_ = realtime_signal_name;
  return *this;
}

// static
intrinsic_proto::icon::v1::Reaction ReactionDescriptor::ToProto(
    const ReactionDescriptor& reaction_descriptor, ReactionId reaction_id,
    const std::optional<ActionInstanceId>& action_id) {
  intrinsic_proto::icon::v1::Reaction reaction;
  *reaction.mutable_condition() =
      ::intrinsic::icon::ToProto(reaction_descriptor.condition_);
  reaction.set_fire_once(reaction_descriptor.fire_once_);
  if (action_id.has_value()) {
    ::intrinsic_proto::icon::v1::Reaction_ActionAssociation action_association;
    action_association.set_action_instance_id(action_id->value());
    action_association.set_stop_associated_action(
        reaction_descriptor.stop_associated_action_);
    *reaction.mutable_action_association() = action_association;
    if (reaction_descriptor.realtime_signal_name_.has_value()) {
      reaction.mutable_action_association()->set_triggered_signal_name(
          reaction_descriptor.realtime_signal_name_.value());
    }
  }
  reaction.set_reaction_instance_id(reaction_id.value());
  if (reaction_descriptor.action_id_.has_value()) {
    reaction.mutable_response()->set_start_action_instance_id(
        reaction_descriptor.action_id_.value().value());
  }
  return reaction;
}

ActionDescriptor::ActionDescriptor(absl::string_view action_type_name,
                                   ActionInstanceId action_id,
                                   const SlotPartMap& slot_part_map)
    : action_type_name_(action_type_name),
      action_id_(action_id),
      slot_data_(slot_part_map) {}
ActionDescriptor::ActionDescriptor(absl::string_view action_type_name,
                                   ActionInstanceId action_id,
                                   absl::string_view part_name)
    : action_type_name_(action_type_name),
      action_id_(action_id),
      slot_data_(std::string(part_name)) {}

ActionDescriptor& ActionDescriptor::WithFixedParams(
    const ::google::protobuf::Message& fixed_params) {
  google::protobuf::Any any_proto;
  static_cast<void>(
      any_proto.PackFrom(fixed_params));  // TODO: Handle return value
  fixed_params_ = any_proto;
  return *this;
}

ActionDescriptor& ActionDescriptor::WithReaction(
    const ReactionDescriptor& reaction_descriptor) {
  reaction_descriptors_.push_back(reaction_descriptor);
  return *this;
}

Action::Action(ActionInstanceId id) : id_(id) {}

absl::StatusOr<std::unique_ptr<Session>> Session::Start(
    std::shared_ptr<ChannelInterface> icon_channel,
    absl::Span<const std::string> parts,
    const intrinsic_proto::data_logger::Context& context,
    std::optional<absl::Time> deadline) {
  return StartImpl(
      context, icon_channel,
      intrinsic_proto::icon::v1::IconApi::NewStub(icon_channel->GetChannel()),
      parts, icon_channel->GetClientContextFactory(), deadline);
}

absl::StatusOr<std::unique_ptr<Session>> Session::Start(
    std::unique_ptr<intrinsic_proto::icon::v1::IconApi::StubInterface> stub,
    absl::Span<const std::string> parts,
    const ClientContextFactory& client_context_factory,
    const intrinsic_proto::data_logger::Context& context,
    std::optional<absl::Time> deadline) {
  return StartImpl(context, nullptr, std::move(stub), parts,
                   client_context_factory, deadline);
}

absl::StatusOr<std::unique_ptr<Session>> Session::StartImpl(
    const intrinsic_proto::data_logger::Context& context,
    std::shared_ptr<ChannelInterface> icon_channel,
    std::unique_ptr<intrinsic_proto::icon::v1::IconApi::StubInterface> stub,
    absl::Span<const std::string> parts,
    const ClientContextFactory& client_context_factory,
    std::optional<absl::Time> deadline) {
  std::unique_ptr<grpc::ClientContext> start_session_context =
      client_context_factory();
  // Streaming call that requires indefinite deadline
  start_session_context->set_deadline(absl::InfiniteFuture());
  std::unique_ptr<grpc::ClientReaderWriterInterface<
      intrinsic_proto::icon::v1::OpenSessionRequest,
      intrinsic_proto::icon::v1::OpenSessionResponse>>
      session_stream = stub->OpenSession(start_session_context.get());
  INTR_ASSIGN_OR_RETURN(SessionId session_id,
                        InitializeSessionOrEndCall(session_stream.get(), parts,
                                                   context, deadline));

  // Initialize the watcher stream at session start, so that no reactions can
  // be missed by the client. This allows reactions associated with this
  // session to be buffered in the `watcher_stream` until we read them later,
  // i.e. when the watcher loop is run.
  std::unique_ptr<grpc::ClientContext> watcher_context =
      client_context_factory();
  // Streaming call that requires indefinite deadline
  watcher_context->set_deadline(absl::InfiniteFuture());
  intrinsic_proto::icon::v1::WatchReactionsRequest watch_reactions_request;
  watch_reactions_request.set_session_id(session_id.value());
  std::unique_ptr<grpc::ClientReaderInterface<
      intrinsic_proto::icon::v1::WatchReactionsResponse>>
      watcher_stream =
          stub->WatchReactions(watcher_context.get(), watch_reactions_request);

  intrinsic_proto::icon::v1::WatchReactionsResponse response;
  if (!watcher_stream->Read(&response)) {
    return ToAbslStatus(watcher_stream->Finish());
  }

  if (response.has_reaction_event()) {
    return absl::InternalError(
        "Should receive an empty reaction first to indicate that the stream is "
        "ready.");
  }

  LOG(INFO) << "Session started with context: " << context;

  return absl::WrapUnique(
      new Session(std::move(icon_channel), std::move(start_session_context),
                  std::move(session_stream), std::move(watcher_context),
                  std::move(watcher_stream), std::move(stub), session_id,
                  context, client_context_factory));
}

Session::~Session() {
  // Don't end the session again if the session has already ended.
  if (session_ended_) {
    return;
  }

  if (absl::Status status = End(); !status.ok()) {
    LOG(ERROR) << "Session ending with status: " << status;
  }
}

absl::StatusOr<Action> Session::AddAction(
    const ActionDescriptor& action_descriptor) {
  INTR_ASSIGN_OR_RETURN(std::vector<Action> actions,
                        AddActions({action_descriptor}));
  return actions[0];
}

struct FillSlotData {
  intrinsic_proto::icon::v1::ActionInstance& action_instance_proto;
  void operator()(const SlotPartMap& slot_part_map) {
    *action_instance_proto.mutable_slot_part_map() = ToProto(slot_part_map);
  }
  void operator()(const std::string& part_name) {
    action_instance_proto.set_part_name(part_name);
  }
};

absl::StatusOr<std::vector<Action>> Session::AddActions(
    absl::Span<const ActionDescriptor> action_descriptors) {
  if (session_ended_) {
    return absl::FailedPreconditionError(kAlreadyEndedErrorMessage);
  }
  // First, check the union of all new reaction handles with the existing
  // handles for uniqueness.
  std::vector<ReactionDescriptor> new_reaction_descriptors;
  for (const ActionDescriptor& action_descriptor : action_descriptors) {
    absl::c_copy(action_descriptor.reaction_descriptors_,
                 std::back_inserter(new_reaction_descriptors));
  }
  INTR_RETURN_IF_ERROR(CheckReactionHandlesUnique(new_reaction_descriptors));

  absl::flat_hash_map<ReactionId, ReactionDescriptor>
      reaction_descriptors_by_id;
  intrinsic_proto::icon::v1::OpenSessionRequest request;

  for (const ActionDescriptor& action_descriptor : action_descriptors) {
    intrinsic_proto::icon::v1::ActionInstance* action_instance =
        request.mutable_add_actions_and_reactions()->add_action_instances();
    action_instance->set_action_type_name(action_descriptor.action_type_name_);
    action_instance->set_action_instance_id(
        action_descriptor.action_id_.value());
    // Fill the correct part of the ActionInstance proto depending on which
    // variant value is set in slot_data_.
    std::visit(FillSlotData{*action_instance}, action_descriptor.slot_data_);
    if (action_descriptor.fixed_params_.has_value()) {
      *action_instance->mutable_fixed_parameters() =
          action_descriptor.fixed_params_.value();
    }

    absl::MutexLock l(&session_mutex_);
    for (const ReactionDescriptor& reaction_descriptor :
         action_descriptor.reaction_descriptors_) {
      ReactionId reaction_id = reaction_id_sequence_.GetNext();
      CHECK(
          reaction_descriptors_by_id.insert({reaction_id, reaction_descriptor})
              .second)
          << "SequenceNumber generated duplicate ReactionId: "
          << reaction_id.value();
      *request.mutable_add_actions_and_reactions()->add_reactions() =
          ReactionDescriptor::ToProto(reaction_descriptor, reaction_id,
                                      action_descriptor.action_id_);
    }
  }
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::icon::v1::OpenSessionResponse response,
                        GetResponseOrEnd(request));
  INTR_RETURN_IF_ERROR(EndAndLogOnAbort(response.status()));

  // Save any callbacks and ReactionHandles for the successfully added
  // reactions.
  SaveReactionData(reaction_descriptors_by_id);
  return MakeActionVector(action_descriptors);
}

absl::Status Session::AddFreestandingReaction(
    const ReactionDescriptor& reaction_descriptor) {
  return AddFreestandingReactions({reaction_descriptor});
}

absl::Status Session::AddFreestandingReactions(
    absl::Span<const ReactionDescriptor> reaction_descriptors) {
  if (session_ended_) {
    return absl::FailedPreconditionError(kAlreadyEndedErrorMessage);
  }
  INTR_RETURN_IF_ERROR(CheckReactionHandlesUnique(reaction_descriptors));

  absl::flat_hash_map<ReactionId, ReactionDescriptor>
      reaction_descriptors_by_id;
  intrinsic_proto::icon::v1::OpenSessionRequest request;
  for (const ReactionDescriptor& reaction_descriptor : reaction_descriptors) {
    absl::MutexLock l(&session_mutex_);
    const ReactionId reaction_id = reaction_id_sequence_.GetNext();
    CHECK(reaction_descriptors_by_id.insert({reaction_id, reaction_descriptor})
              .second)
        << "SequenceNumber generated duplicate ReactionId: "
        << reaction_id.value();
    *request.mutable_add_actions_and_reactions()->add_reactions() =
        ReactionDescriptor::ToProto(reaction_descriptor, reaction_id,
                                    std::nullopt);
  }
  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::icon::v1::OpenSessionResponse response,
      GetResponseOrEnd(request));
  INTR_RETURN_IF_ERROR(EndAndLogOnAbort(response.status()));

  // Save any callbacks and ReactionHandles for the successfully added
  // reactions.
  SaveReactionData(reaction_descriptors_by_id);
  return absl::OkStatus();
}

absl::Status Session::RemoveAction(ActionInstanceId action_id) {
  return RemoveActions({action_id});
}

absl::Status Session::RemoveActions(
    const std::vector<ActionInstanceId>& action_ids) {
  if (session_ended_) {
    return absl::FailedPreconditionError(kAlreadyEndedErrorMessage);
  }

  intrinsic_proto::icon::v1::OpenSessionRequest request;
  for (ActionInstanceId action_id : action_ids) {
    request.mutable_remove_action_and_reaction_ids()->add_action_instance_ids(
        action_id.value());
  }

  INTR_ASSIGN_OR_RETURN(intrinsic_proto::icon::v1::OpenSessionResponse response,
                        GetResponseOrEnd(request));

  return EndAndLogOnAbort(response.status());
}

absl::Status Session::ClearAllActionsAndReactions() {
  if (session_ended_) {
    return absl::FailedPreconditionError(kAlreadyEndedErrorMessage);
  }

  intrinsic_proto::icon::v1::OpenSessionRequest request;
  request.mutable_clear_all_actions_reactions();
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::icon::v1::OpenSessionResponse response,
                        GetResponseOrEnd(request));

  return EndAndLogOnAbort(response.status());
}

absl::Status Session::StartAction(const Action& action,
                                  bool stop_active_actions) {
  return StartActions({action}, stop_active_actions);
}

absl::Status Session::StartActions(absl::Span<const Action> actions,
                                   bool stop_active_actions) {
  if (session_ended_) {
    return absl::FailedPreconditionError(kAlreadyEndedErrorMessage);
  }
  intrinsic_proto::icon::v1::OpenSessionRequest::StartActionsRequestData
      start_actions_request;
  start_actions_request.set_stop_active_actions(stop_active_actions);
  for (const Action& action : actions) {
    start_actions_request.add_action_instance_ids(action.id().value());
  }
  intrinsic_proto::icon::v1::OpenSessionRequest request;
  *request.mutable_start_actions_request() = start_actions_request;
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::icon::v1::OpenSessionResponse response,
                        GetResponseOrEnd(request));
  return EndAndLogOnAbort(response.status());
}

absl::Status Session::StopAllActions() {
  return StartActions({}, /*stop_active_actions=*/true);
}

absl::Status Session::RunWatcherLoop(absl::Time deadline) {
  current_watcher_loop_user_count_++;
  auto cleanup =
      absl::MakeCleanup([this]() { current_watcher_loop_user_count_--; });
  {
    absl::MutexLock l(&reactions_queue_mutex_);
    if (watcher_loop_state_ == kRunning) {
      return absl::FailedPreconditionError("Watcher loop is already running.");
    }
    if (watcher_loop_state_ == kFinished) {
      return absl::FailedPreconditionError(
          "Watcher loop is already finished. Please reset the session state "
          "using ResetWatcherLoopState() to run the watcher loop again.");
    }
    quit_watcher_loop_ = false;
    watcher_loop_state_ = kRunning;
  }
  auto cleanup2 = absl::MakeCleanup([this]() {
    absl::MutexLock l(&reactions_queue_mutex_);
    watcher_loop_state_ = kFinished;
  });
  while (true) {
    std::optional<
        absl::StatusOr<intrinsic_proto::icon::v1::WatchReactionsResponse>>
        response;
    {
      absl::MutexLock l(&reactions_queue_mutex_);
      if (!reactions_queue_mutex_.AwaitWithDeadline(
              absl::Condition(this, &Session::ShouldWakeWatcherLoop),
              deadline)) {
        return absl::DeadlineExceededError(
            "Deadline exceeded in RunWatcherLoop()");
      }
      if (reactions_queue_.empty() && quit_watcher_loop_) {
        // If the event queue has closed, we can still quit from a quit event
        // triggered in a reaction callback by checking `quit_watcher_loop_`.
        return absl::OkStatus();
      }
      if (!reactions_queue_.empty()) {
        response = reactions_queue_.front();
        reactions_queue_.pop_front();
      }
    }
    if (!response.has_value()) {
      absl::Status status = End();
      LOG(INFO) << "Session ended unexpectedly while running the watcher loop "
                   "with status: "
                << status;
      return absl::AbortedError(
          "The call died while reading reactions, the session has been ended.");
    }

    // Grpc error from ICON server, end session and error.
    if (!response->ok()) {
      absl::Status status = End();
      LOG(INFO) << "Session ended unexpectedly while running the watcher loop "
                   "with status: "
                << response->status() << "\n EndSession() status: " << status;
      return response->status();
    }

    // Normal case, service reaction callbacks.
    TriggerReactionCallbacks(response->value());
  }
}

absl::Status Session::ResetWatcherLoopState() {
  if (current_watcher_loop_user_count_ > 0) {
    return absl::FailedPreconditionError(
        "Session::ResetWatcherLoopState() called while RunWatcherLoop(), "
        "QuitWatcherLoop() or WaitForWatcherLoopStart() is running. This is "
        "not allowed.");
  }
  // We need an explicit reset function since the situation what to wait for is
  // ambigous.
  // Imagine the situation:
  //   Thread 1 calls RunWatcherLoop().
  //   Thread 2 calls QuitWatcherLoop().
  //   T1 finishes loop.
  //   Thread 1 calls RunWatcherLoop() again.
  //   Thread 2 calls QuitWatcherLoop(timeout=10).
  //   Depending on thread scheduling, QuitWatcherLoop() is called before or
  //   after RunWatcherLoop().
  //
  //   Problem: Should T2 now wait for the previous loop
  //   (i.e. be done immediately) or the next loop? Therefore, the user needs to
  //   consciously reset the internal state.

  // Without an explicit reset, we cannot distinguish between a loop that
  // already finished and a loop that has not been started.
  // This is crucial for `QuitWatcherLoop(timeout)` since it needs to know
  // whether it can return immediately since the loop has already finished or
  // needs to wait for the loop to start.
  absl::MutexLock l(&reactions_queue_mutex_);
  watcher_loop_state_ = kNotStarted;
  quit_watcher_loop_ = false;
  return absl::OkStatus();
}

absl::Status Session::WaitForWatcherLoopStart(absl::Time deadline) {
  current_watcher_loop_user_count_++;
  auto cleanup =
      absl::MakeCleanup([this]() { current_watcher_loop_user_count_--; });
  absl::MutexLock l(&reactions_queue_mutex_);
  if (watcher_loop_state_ == kFinished) {
    return absl::FailedPreconditionError(
        "Watcher loop is already finished, please reset the session state "
        "using ResetWatcherLoopState() to run the watcher loop again.");
  }
  auto cond = [this]() ABSL_EXCLUSIVE_LOCKS_REQUIRED(reactions_queue_mutex_) {
    return watcher_loop_state_ == kRunning || watcher_loop_state_ == kFinished;
  };
  if (!reactions_queue_mutex_.AwaitWithDeadline(absl::Condition(&cond),
                                                deadline)) {
    return absl::DeadlineExceededError(
        "Deadline exceeded while waiting for watcher loop to start.");
  }
  return absl::OkStatus();
}

absl::Status Session::QuitWatcherLoop(absl::Time watcher_loop_start_deadline) {
  current_watcher_loop_user_count_++;
  auto cleanup =
      absl::MakeCleanup([this]() { current_watcher_loop_user_count_--; });
  INTR_RETURN_IF_ERROR(WaitForWatcherLoopStart(watcher_loop_start_deadline));
  QuitWatcherLoop();
  return absl::OkStatus();
}

void Session::QuitWatcherLoop() {
  current_watcher_loop_user_count_++;
  auto cleanup =
      absl::MakeCleanup([this]() { current_watcher_loop_user_count_--; });
  absl::MutexLock l(&reactions_queue_mutex_);
  quit_watcher_loop_ = true;
}

absl::StatusOr<::intrinsic_proto::icon::StreamingOutput>
Session::GetLatestOutput(ActionInstanceId id, absl::Time deadline) {
  std::unique_ptr<grpc::ClientContext> context = client_context_factory_();
  context->set_deadline(absl::ToChronoTime(deadline));
  ::intrinsic_proto::icon::v1::GetLatestStreamingOutputRequest request;
  request.set_session_id(session_id_.value());
  request.set_action_id(id.value());
  ::intrinsic_proto::icon::v1::GetLatestStreamingOutputResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->GetLatestStreamingOutput(context.get(), request, &response)));
  return response.output();
}

absl::StatusOr<::intrinsic_proto::icon::JointTrajectoryPVA>
Session::GetPlannedTrajectory(ActionInstanceId id) {
  std::unique_ptr<grpc::ClientContext> context = client_context_factory_();
  // Streaming call that requires indefinite deadline
  context->set_deadline(absl::InfiniteFuture());
  ::intrinsic_proto::icon::v1::GetPlannedTrajectoryRequest request;
  request.set_session_id(session_id_.value());
  request.set_action_id(id.value());

  std::unique_ptr<::grpc::ClientReaderInterface<
      ::intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse>>
      stream = stub_->GetPlannedTrajectory(context.get(), request);

  ::intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse response;
  std::vector<::intrinsic_proto::icon::JointTrajectoryPVA>
      planned_trajectory_segments;
  while (stream->Read(&response)) {
    planned_trajectory_segments.push_back(
        response.planned_trajectory_segment());
  }
  INTR_RETURN_IF_ERROR(ToAbslStatus(stream->Finish()));

  return ConcatenateTrajectoryProtos(planned_trajectory_segments);
}

absl::Status Session::RunWatcherLoopUntilReaction(
    ReactionHandle reaction_handle, absl::Time deadline) {
  {
    absl::MutexLock l(&session_mutex_);
    auto maybe_id = reaction_handle_to_id_and_loc_.find(reaction_handle);
    if (maybe_id == reaction_handle_to_id_and_loc_.end()) {
      return absl::NotFoundError(
          absl::StrCat("There is no reaction with ReactionHandle(",
                       reaction_handle.value(), ")"));
    }
    const ReactionId reaction_id = maybe_id->second.first;
    auto [it, inserted] = reaction_callback_map_.insert(
        {reaction_id,
         [this, reaction_id]() ABSL_LOCKS_EXCLUDED(session_mutex_) {
           {
             absl::MutexLock l(&session_mutex_);
             reaction_callback_map_.erase(reaction_id);
           }
           this->QuitWatcherLoop();
         }});
    if (!inserted) {
      std::function<void()> previous_callback = it->second;
      it->second = [this, previous_callback, reaction_id]()
                       ABSL_LOCKS_EXCLUDED(session_mutex_) {
                         previous_callback();
                         {
                           absl::MutexLock l(&session_mutex_);
                           reaction_callback_map_.insert_or_assign(
                               reaction_id, previous_callback);
                         }
                         this->QuitWatcherLoop();
                       };
    }
  }
  return RunWatcherLoop(deadline);
}

absl::Status Session::End() {
  if (session_ended_) {
    return absl::FailedPreconditionError(kAlreadyEndedErrorMessage);
  }
  session_ended_ = true;
  QuitWatcherLoop();  // stop triggering client callbacks.
  {
    absl::MutexLock l(&reactions_queue_mutex_);
    watcher_loop_state_ =
        kFinished;  // Cancel threads waiting in WaitForWatcherLoopStart()
  }
  absl::MutexLock lock(&session_mutex_);
  // Close the action session call
  session_stream_->WritesDone();
  // The server ends all watcher streams when the action session ends, so we
  // must clean up the action session first.
  absl::Status session_call_status =
      CleanUpCallAfterClientWritesDone(session_stream_.get());

  // Ensure that we've stopped reading reactions from the `watcher_stream_`
  // before finishing the watch reactions call to avoid calling
  // watcher_stream_.Read() concurrently from multiple threads.
  watcher_read_thread_.join();
  CleanUpWatcherCall();
  return session_call_status;
}

Session::Session(
    std::shared_ptr<ChannelInterface> icon_channel,
    std::unique_ptr<grpc::ClientContext> session_context,
    std::unique_ptr<grpc::ClientReaderWriterInterface<
        intrinsic_proto::icon::v1::OpenSessionRequest,
        intrinsic_proto::icon::v1::OpenSessionResponse>>
        session_stream,
    std::unique_ptr<grpc::ClientContext> watcher_context,
    std::unique_ptr<grpc::ClientReaderInterface<
        intrinsic_proto::icon::v1::WatchReactionsResponse>>
        watcher_stream,
    std::unique_ptr<intrinsic_proto::icon::v1::IconApi::StubInterface> stub,
    SessionId session_id, const intrinsic_proto::data_logger::Context& context,
    ClientContextFactory client_context_factory)
    : channel_(std::move(icon_channel)),
      session_context_(std::move(session_context)),
      session_stream_(std::move(session_stream)),
      watcher_context_(std::move(watcher_context)),
      watcher_stream_(std::move(watcher_stream)),
      watcher_read_thread_(&Session::WatchReactionsThreadBody, this),
      stub_(std::move(stub)),
      session_id_(session_id),
      client_context_factory_(client_context_factory) {}

absl::Status Session::CheckReactionHandlesUnique(
    absl::Span<const ReactionDescriptor> reaction_descriptors) const {
  absl::MutexLock l(&session_mutex_);
  absl::flat_hash_map<ReactionHandle, intrinsic::SourceLocation>
      new_reaction_handles;
  for (const auto& [reaction_handle, id_and_loc] :
       reaction_handle_to_id_and_loc_) {
    new_reaction_handles.insert({reaction_handle, id_and_loc.second});
  }
  for (const ReactionDescriptor& reaction_descriptor : reaction_descriptors) {
    if (!reaction_descriptor.reaction_handle_.has_value()) {
      continue;
    }
    if (auto [it, inserted] =
            new_reaction_handles.insert(*reaction_descriptor.reaction_handle_);
        !inserted) {
      return absl::AlreadyExistsError(absl::StrCat(
          "The reaction handle ", it->first.value(),
          " already exists. First handle was applied at ",
          it->second.file_name(), ":", it->second.line(),
          ". Second handle at: ",
          reaction_descriptor.reaction_handle_->second.file_name(), ":",
          reaction_descriptor.reaction_handle_->second.line()));
    }
  }
  return absl::OkStatus();
}

void Session::SaveReactionData(
    const absl::flat_hash_map<ReactionId, ReactionDescriptor>&
        reaction_descriptors_by_id) {
  absl::MutexLock lock(&session_mutex_);
  for (const auto& [reaction_id, reaction_descriptor] :
       reaction_descriptors_by_id) {
    if (reaction_descriptor.reaction_handle_) {
      const auto [reaction_handle, loc] =
          reaction_descriptor.reaction_handle_.value();
      CHECK(reaction_handle_to_id_and_loc_
                .insert({reaction_handle, {reaction_id, loc}})
                .second)
          << "Trying to insert duplicate ReactionHandle in SaveReactionData. "
             "CheckReactionHandleUnique() should guarantee this does not "
             "happen.";
    }
    if (reaction_descriptor.on_condition_) {
      // If we fail to insert the callback, there's a serious bug in
      // SequenceNumber.
      CHECK(
          reaction_callback_map_
              .insert({reaction_id, reaction_descriptor.on_condition_.value()})
              .second)
          << "Trying to insert duplicate Reaction callback in "
             "SaveReactionData. The server should guarantee this does not "
             "happen.";
    }
  }
}

absl::StatusOr<intrinsic_proto::icon::v1::OpenSessionResponse>
Session::GetResponseOrEnd(
    const intrinsic_proto::icon::v1::OpenSessionRequest& request) {
  absl::StatusOr<intrinsic_proto::icon::v1::OpenSessionResponse>
      status_or_response;
  {
    absl::MutexLock l(&session_mutex_);
    status_or_response =
        WriteMessageAndReadResponse(request, session_stream_.get());
  }
  if (!status_or_response.ok()) {
    LOG(ERROR) << "Call died while completing message exchange: "
               << status_or_response.status();
    absl::Status session_status = End();
    LOG(ERROR) << "Ended session with status: " << session_status;
    return absl::AbortedError(
        "The session ended while performing a remote operation.");
  }

  return *status_or_response;
}

void Session::TriggerReactionCallbacks(
    const intrinsic_proto::icon::v1::WatchReactionsResponse& reaction) {
  if (!reaction.has_reaction_event()) {
    return;
  }
  ReactionId reaction_id(reaction.reaction_event().reaction_id());
  std::function<void()> reaction_callback;
  {
    absl::MutexLock l(&session_mutex_);
    auto it = reaction_callback_map_.find(reaction_id);
    if (it == reaction_callback_map_.end()) {
      return;
    }
    reaction_callback = it->second;
  }
  reaction_callback();
}

void Session::CleanUpWatcherCall() {
  absl::MutexLock l(&reactions_queue_mutex_);
  absl::StatusOr<
      std::optional<intrinsic_proto::icon::v1::WatchReactionsResponse>>
      response = std::nullopt;
  while (!reactions_queue_.empty()) {
    const auto& response = reactions_queue_.front();
    if (response.ok()) {
      DLOG(INFO) << "Had reaction event in queue after quitting watcher loop: "
                 << *response;
    }
    reactions_queue_.pop_front();
  }
  LOG(INFO) << "Ended watcher call";
}

absl::Status Session::EndAndLogOnAbort(const ::google::rpc::Status& status) {
  absl::Status absl_status = ToAbslStatus(status);
  if (absl_status.ok() || absl_status.code() != absl::StatusCode::kAborted) {
    return absl_status;
  }

  LOG(ERROR) << "Session ending due to status: " << absl_status;
  if (absl::Status call_status = End(); !call_status.ok()) {
    LOG(ERROR) << "Session ended with call status: " << call_status;
  }

  // Prepend to make clear that the Session has ended.
  StatusBuilder aborted_status_builder(absl_status);
  aborted_status_builder.SetPrepend() << "Session ended. ";
  return aborted_status_builder;
}

void Session::WatchReactionsThreadBody() {
  absl::MutexLock watch_lock(&watch_reactions_mutex_);
  intrinsic_proto::icon::v1::WatchReactionsResponse watcher_reactions_response;
  // Read will return false when the call ends. The call normally ends when the
  // session is over. If the call ends earlier, it's due to a connection failure
  // or a bug on the server.
  while (watcher_stream_->Read(&watcher_reactions_response)) {
    absl::MutexLock l(&reactions_queue_mutex_);
    reactions_queue_.push_back(watcher_reactions_response);
    if (reactions_queue_.size() > 10) {
      LOG_EVERY_N_SEC(WARNING, 10)
          << "RunWatcherLoop has a large backlog of reactions. Please make "
             "sure 1) you call RunWatcherLoop just after StartAction and 2) "
             "any callback functions are quick.";
    }
  }
  grpc::Status grpc_status = watcher_stream_->Finish();
  absl::MutexLock l(&reactions_queue_mutex_);
  if (!grpc_status.ok()) {
    absl::Status error = ToAbslStatus(grpc_status);  // Only allowed for errors.
    reactions_queue_.push_back(error);
  }
  reactions_stream_closed_ = true;
}

// static
std::vector<Action> Session::MakeActionVector(
    absl::Span<const ActionDescriptor> action_descriptors) {
  std::vector<Action> actions;
  actions.reserve(action_descriptors.size());
  for (const ActionDescriptor& action_descriptor : action_descriptors) {
    actions.push_back(Action(action_descriptor.action_id_));
  }
  return actions;
}

}  // namespace icon
}  // namespace intrinsic
