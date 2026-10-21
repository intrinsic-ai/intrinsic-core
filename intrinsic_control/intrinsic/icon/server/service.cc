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

#include "intrinsic/icon/server/service.h"

#include <functional>
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
#include "absl/container/flat_hash_set.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gloop/util/gtl/iterator_adaptors.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/text_format.h"
#include "google/rpc/status.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/sync_stream.h"
#include "intrinsic/icon/cc_client/operational_status.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/common/slot_part_map.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/control/parts/payload_property.h"
#include "intrinsic/icon/control/parts/realtime_log_context.h"
#include "intrinsic/icon/control/realtime_operational_status.h"
#include "intrinsic/icon/proto/concatenate_trajectory_protos.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/types_proto_conversions.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/extended_status_constants.h"
#include "intrinsic/icon/server/icon_api_service.h"
#include "intrinsic/icon/server/operational_state_interface.h"
#include "intrinsic/icon/server/part_collection.h"
#include "intrinsic/icon/server/parts_manager.h"
#include "intrinsic/icon/server/robot_connection_interface.h"
#include "intrinsic/icon/server/session_instance.h"
#include "intrinsic/icon/server/session_interface.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/util/atomic_sequence_num.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/util/testcallback.h"
#include "intrinsic/world/robot_payload/robot_payload.h"

namespace intrinsic {
namespace icon {

namespace {

// This has to be smaller than the max metadata size set in the gRPC envelope
// since client metadata limits may use the default value of 8KB (see
// GRPC_ARG_MAX_METADATA_SIZE in
// google3/third_party/grpc/include/grpc/impl/channel_arg_names.h ). We set this
// to less than half of the 8KB limit to mitigate cases where the debug message
// is used as context for errors downstream effectively duplicating the
// metadata.
constexpr int kMaxMetadataMessageSize = 3 * 1024;
// If Session::Start() has no deadline, we use this.
// It waits for the parts of a session to be enabled.
constexpr absl::Duration kDefaultStartSessionTimeout = absl::Seconds(10);

std::string ActionTypeNotFoundHelpText(absl::string_view action_type_name) {
  return absl::StrCat(
      "There is no Action type '", action_type_name,
      "'. Are you sure it has been registered? (The most common way to "
      "register an Action type is by linking its _register target into the "
      "ICON server)");
}

bool PartIsCompatibleWithAllActionTypes(
    absl::string_view part_name,
    absl::Span<const std::string> action_type_names,
    RobotConnectionInterface& robot_connection) {
  for (const auto& action_type_name : action_type_names) {
    if (auto status = robot_connection.ActionCompatibleWithPart(
            /*action_type_name=*/action_type_name,
            /*part_name=*/part_name);
        !status.ok()) {
      LOG(ERROR) << status;
      return false;
    }
  }
  return true;
}

// Returns a copy of `actions_and_reactions` modified (if necessary) so that
// each Action instance has a SlotPartMap.
//
// If an Action instance already has a SlotPartMap, it is copied unchanged.
//
// If it does not, this function attempts to infer a SlotPartMap. This only
// works for Action instances that use a single Slot!
//
// Reads the name of the Action's Slot from its signature from
// `action_metadata_map`, and creates a SlotPartMap that maps the (single)
// Part in the Action instance proto to that Slot.
//
// Returns an error if an Action instance has neither a SlotPartMap nor a Part
// name.
// Returns an error if an Action instance has a Part name, but its signature
// has multiple (required) Slots.
// Returns an error if an Action instance's type is not found in
// `action_metadata_map`.
absl::StatusOr<intrinsic_proto::icon::v1::ActionsAndReactions>
InferSlotPartMaps(
    const intrinsic_proto::icon::v1::ActionsAndReactions& actions_and_reactions,
    const absl::flat_hash_map<std::string,
                              intrinsic_proto::icon::v1::ActionSignature>&
        action_type_to_signature) {
  // Make a copy to fill in SlotPartMaps for all ActionInstances.
  intrinsic_proto::icon::v1::ActionsAndReactions actions_and_reactions_copy =
      actions_and_reactions;
  for (auto& action_instance :
       *actions_and_reactions_copy.mutable_action_instances()) {
    switch (action_instance.slot_data_case()) {
      case intrinsic_proto::icon::v1::ActionInstance::SLOT_DATA_NOT_SET:
        return absl::InvalidArgumentError(absl::StrCat(
            "ActionInstance(", action_instance.action_instance_id(),
            ") has no slot data at all! You must supply either a "
            "SlotPartMap or a Part name."));
      case intrinsic_proto::icon::v1::ActionInstance::SlotDataCase::
          kSlotPartMap: {
        if (action_instance.slot_part_map().slot_name_to_part_name().empty()) {
          return absl::InvalidArgumentError(absl::StrCat(
              "ActionInstance(", action_instance.action_instance_id(),
              ") has an empty SlotPartMap!"));
        }
        break;
      }
      case intrinsic_proto::icon::v1::ActionInstance::SlotDataCase::kPartName: {
        std::string part_name = action_instance.part_name();
        // Get the Action Type's signature to map the single Part to its
        // (hopefully) single Slot.
        auto action_signature =
            action_type_to_signature.find(action_instance.action_type_name());
        if (action_signature == action_type_to_signature.end()) {
          return absl::NotFoundError(absl::StrCat(
              ActionTypeNotFoundHelpText(action_instance.action_type_name()),
              " While inferring SlotPartMap. Available Action type names: ",
              absl::StrJoin(
                  action_type_to_signature, ", ",
                  [](std::string* os, const auto& name_and_signature) {
                    absl::StrAppend(os, name_and_signature.first);
                  })));
        }
        if (action_signature->second.part_slot_infos().size() != 1) {
          return absl::FailedPreconditionError(absl::StrCat(
              "Action type '", action_instance.action_type_name(),
              "' does not have exactly one Part Slot. Cannot infer Part "
              "<-> Slot mapping for part ",
              part_name));
        }
        action_instance.mutable_slot_part_map()
            ->mutable_slot_name_to_part_name()
            ->insert({action_signature->second.part_slot_infos().begin()->first,
                      part_name});
        break;
      }
    }
  }
  return actions_and_reactions_copy;
}

class SessionHandler {
 public:
  // `session_interface` must outlive this.
  // `action_type_compatible_with_slot_map` is a function that determines
  // whether a given action type is compatible with a SlotPartMap. It should
  // return meaningful errors in case the action type is not compatible.
  //
  // We pass `action_type_compatible_with_slot_map` as a std::function so that
  // it can do any mutex locking/unlocking it needs to. The resources that
  // determine Action/Part compatibility are guarded by a mutex, so this is
  // cleaner than passing the resource to SessionHandler directly.
  explicit SessionHandler(
      SessionInterface* session_interface,
      std::function<
          absl::Status(absl::string_view action_type_name,
                       const intrinsic::icon::SlotPartMap& slot_part_map)>
          action_type_compatible_with_slot_map)
      : session_interface_(session_interface),
        action_type_compatible_with_slot_map_(
            std::move(action_type_compatible_with_slot_map)) {}

  // Handles the `request` and returns the response message to send.
  intrinsic_proto::icon::v1::OpenSessionResponse HandleOpenSessionRequest(
      const intrinsic_proto::icon::v1::OpenSessionRequest& request,
      const absl::flat_hash_map<std::string,
                                intrinsic_proto::icon::v1::ActionSignature>&
          action_type_to_signature) {
    intrinsic_proto::icon::v1::OpenSessionResponse response;
    absl::StatusOr<std::optional<
        intrinsic_proto::icon::v1::OpenSessionResponse::ActionResponse>>
        status_or_optional_action_response =
            HandleActionRequest(request, action_type_to_signature);
    if (!status_or_optional_action_response.ok()) {
      *response.mutable_status() =
          SaveStatusAsRpcStatus(status_or_optional_action_response.status());
      // Create an empty action_response to mark the type of
      // OpenSessionResponse.
      response.mutable_action_response();
      LOG(ERROR) << "HandleActionRequest failed with: "
                 << status_or_optional_action_response.status();
      return response;
    }
    // Only set the response type as an action_response if there was one
    // present
    if (status_or_optional_action_response->has_value()) {
      *response.mutable_action_response() =
          status_or_optional_action_response->value();
    }

    if (absl::Status status = HandleStartActionInstanceRequest(request);
        !status.ok()) {
      *response.mutable_status() = SaveStatusAsRpcStatus(status);
      return response;
    }

    response.mutable_status()->set_code(absl::OkStatus().raw_code());
    return response;
  }

 private:
  // Handles the `action_request` present in the `request`. Returns an Ok
  // status with an ActionResponse if there was an action request, and the
  // request succeeded. If action request is not set in `request`, returns an
  // unset optional.
  absl::StatusOr<std::optional<
      intrinsic_proto::icon::v1::OpenSessionResponse::ActionResponse>>
  HandleActionRequest(
      const intrinsic_proto::icon::v1::OpenSessionRequest& request,
      const absl::flat_hash_map<std::string,
                                intrinsic_proto::icon::v1::ActionSignature>&
          action_type_to_signature) {
    switch (request.action_request_case()) {
      case intrinsic_proto::icon::v1::OpenSessionRequest::
          ACTION_REQUEST_NOT_SET:
        return std::nullopt;
      case intrinsic_proto::icon::v1::OpenSessionRequest::
          kAddActionsAndReactions: {
        {
          INTR_ASSIGN_OR_RETURN(
              intrinsic_proto::icon::v1::ActionsAndReactions
                  actions_and_reactions_proto,
              InferSlotPartMaps(request.add_actions_and_reactions(),
                                action_type_to_signature));
          for (const ::intrinsic_proto::icon::v1::ActionInstance&
                   action_instance :
               actions_and_reactions_proto.action_instances()) {
            SlotPartMap slot_part_map =
                SlotPartMapFromProto(action_instance.slot_part_map());
            INTR_RETURN_IF_ERROR(action_type_compatible_with_slot_map_(
                action_instance.action_type_name(), slot_part_map));
          }
          INTR_ASSIGN_OR_RETURN(
              auto actions_and_reactions,
              ActionsAndReactionsFromProto(actions_and_reactions_proto));
          INTR_RETURN_IF_ERROR(session_interface_->AddActionsAndReactions(
              std::move(actions_and_reactions)));
        }
        break;
      }
      case intrinsic_proto::icon::v1::OpenSessionRequest::
          kRemoveActionAndReactionIds:
        INTR_RETURN_IF_ERROR(session_interface_->RemoveActionsAndReactions(
            ActionAndReactionIdsFromProto(
                request.remove_action_and_reaction_ids())));
        break;
      case intrinsic_proto::icon::v1::OpenSessionRequest::
          kClearAllActionsReactions:
        INTR_RETURN_IF_ERROR(
            session_interface_->RemoveAllActionsAndReactions());
        break;
    }
    return intrinsic_proto::icon::v1::OpenSessionResponse::ActionResponse();
  }

  // Handles the `start_actions_request` present in the `request`.
  absl::Status HandleStartActionInstanceRequest(
      const intrinsic_proto::icon::v1::OpenSessionRequest& request) {
    if (!request.has_start_actions_request()) {
      return absl::OkStatus();
    }

    std::vector<ActionInstanceId> action_ids;
    for (auto id : request.start_actions_request().action_instance_ids()) {
      action_ids.push_back(ActionInstanceId(id));
    }
    return session_interface_->StartActions(
        action_ids, request.start_actions_request().stop_active_actions());
  }

  SessionInterface* const session_interface_;
  std::function<absl::Status(absl::string_view action_type_name,
                             const intrinsic::icon::SlotPartMap& slot_part_map)>
      action_type_compatible_with_slot_map_;
};

void LogOpenSessionRequest(
    const intrinsic_proto::icon::v1::OpenSessionRequest& req) {
  intrinsic_proto::data_logger::LogItem li;
  li.mutable_metadata()->set_event_source("icon.server.session_request");
  static_cast<void>(li.mutable_payload()->mutable_any()->PackFrom(
      req));  // TODO: Handle return value
  data_logger::LogAsync(std::move(li));
}

void LogOpenSessionResponse(
    const intrinsic_proto::icon::v1::OpenSessionResponse& resp) {
  intrinsic_proto::data_logger::LogItem li;
  li.mutable_metadata()->set_event_source("icon.server.session_response");
  static_cast<void>(li.mutable_payload()->mutable_any()->PackFrom(
      resp));  // TODO: Handle return value
  data_logger::LogAsync(std::move(li));
}

// Any methods whose names end in Const are there to enforce stricter const
// requirements when we can. The virtual methods defined by the gRPC service
// are non-const, but many of them do not modify actual ICON state (beyond
// locking/unlocking a mutex).
class ApplicationLayerService : public IconApiService {
 public:
  // Tracks cancellable grpc stream `context` during its lifetime.
  class ScopedCancellableGrpcContext {
   public:
    ScopedCancellableGrpcContext(ApplicationLayerService& service,
                                 ::grpc::ServerContext* context)
        : service_(service),
          service_shutdown_(service.GetShutdownNotification()),
          context_(context) {
      if (!service_shutdown_->HasBeenNotified()) {
        absl::MutexLock l(service_.open_streams_mutex_);
        service_.open_grpc_streams_.insert(context_);
      }
    }
    ~ScopedCancellableGrpcContext() {
      if (!service_shutdown_->HasBeenNotified()) {
        absl::MutexLock l(service_.open_streams_mutex_);
        service_.open_grpc_streams_.erase(context_);
      }
    }
    ScopedCancellableGrpcContext(const ScopedCancellableGrpcContext&) = delete;
    ScopedCancellableGrpcContext& operator=(
        const ScopedCancellableGrpcContext&) = delete;

   private:
    ApplicationLayerService& service_;
    std::shared_ptr<absl::Notification> service_shutdown_;
    ::grpc::ServerContext* context_;
  };

  explicit ApplicationLayerService(RobotConnectionInterface& robot_connection)
      : robot_connection_(robot_connection),
        shutdown_notification_(std::make_shared<absl::Notification>()) {}

  ~ApplicationLayerService() override {
    {
      absl::MutexLock l(open_streams_mutex_);
      LOG(INFO) << "Trying to cancel " << open_grpc_streams_.size()
                << " grpc streams and close sessions.";
      for (const auto& context : open_grpc_streams_) {
        context->TryCancel();
      }

      LOG(INFO) << "Cancelled all grpc streams.";
      if (open_streams_mutex_.AwaitWithTimeout(
              absl::Condition(this, &ApplicationLayerService::HasNoOpenStreams),
              absl::Seconds(1))) {
        LOG(INFO) << "All grpc streams are closed.";
      } else {
        LOG(WARNING) << "Failed to close grpc streams within timeout.";
        // Signalling this Notification effectively "disarms" any remaining
        // ScopedCancellableGrpcContext objects. If they are destroyed after we
        // exit this destructor, they will access invalid memory (i.e. the old
        // `this` pointer for this ApplicationLayerService).
        shutdown_notification_->Notify();
      }
    }
    // Check that all Sessions are also closed (this is implied by the above,
    // but let's be safe)
    {
      absl::MutexLock l(session_map_mutex_);
      if (session_map_mutex_.AwaitWithTimeout(
              absl::Condition(this, &ApplicationLayerService::HasNoSessions),
              absl::Seconds(1))) {
        LOG(INFO) << "All sessions are closed.";
      } else {
        LOG(WARNING) << "Failed to close sessions within timeout.";
      }
    }
  }

  void TryCancel() override ABSL_LOCKS_EXCLUDED(open_streams_mutex_) {
    absl::MutexLock l(open_streams_mutex_);
    LOG(INFO) << "Trying to cancel " << open_grpc_streams_.size()
              << " grpc streams.";
    for (const auto& context : open_grpc_streams_) {
      context->TryCancel();
    }
  }

  // Returns a `shared_ptr` to a `Notification` that fires when the
  // `ApplicationLayerService` gets destroyed.
  //
  // `ScopedCancellableGrpcContext` may outlive the `ApplicationLayerService`,
  // so this must be a shared_ptr so that any surviving
  // `ScopedCancellableGrpcContext`s can still read it after the
  // `ApplicationLayerService`'s death.
  std::shared_ptr<absl::Notification> GetShutdownNotification() {
    return shutdown_notification_;
  }

  bool HasNoOpenStreams() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(open_streams_mutex_) {
    return open_grpc_streams_.empty();
  }

  bool HasNoSessions() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(session_map_mutex_) {
    return session_id_to_session_map_.empty();
  }

  ScopedCancellableGrpcContext RegisterCancellableGrpcStream(
      ::grpc::ServerContext* context) {
    return ScopedCancellableGrpcContext(*this, context);
  }

  // Gets details of an action type by name.
  ::grpc::Status GetActionSignatureByName(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetActionSignatureByNameRequest* request,
      intrinsic_proto::icon::v1::GetActionSignatureByNameResponse* response)
      override {
    return GetActionSignatureByNameConst(context, request, response);
  }

  ::grpc::Status GetActionSignatureByNameConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetActionSignatureByNameRequest* request,
      intrinsic_proto::icon::v1::GetActionSignatureByNameResponse* response)
      const {
    const auto action_type_to_signature =
        robot_connection_.ActionTypeToSignature();
    auto signature = action_type_to_signature.find(request->name());
    if (signature == action_type_to_signature.end()) {
      LOG(INFO) << ActionTypeNotFoundHelpText(request->name());

      // Report "not found" with an unset `action_signature`.
      response->clear_action_signature();
      return ToGrpcStatus(absl::OkStatus());
    }
    *response->mutable_action_signature() = signature->second;
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status GetConfig(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetConfigRequest* request,
      intrinsic_proto::icon::v1::GetConfigResponse* response) override {
    return GetConfigConst(context, request, response);
  }

  ::grpc::Status RestartServer(::grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               google::protobuf::Empty* response) override {
    return ToGrpcStatus(
        absl::UnimplementedError("ICON server restart is unimplemented."));
  }

  ::grpc::Status GetConfigConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetConfigRequest* request,
      intrinsic_proto::icon::v1::GetConfigResponse* response) const {
    const auto part_configs =
        robot_connection_.GetPartCollection().GetPartConfigs();
    *response->mutable_part_configs() = {part_configs.begin(),
                                         part_configs.end()};
    response->set_control_frequency_hz(
        robot_connection_.config().frequency_hz());
    *response->mutable_server_config() = robot_connection_.config();
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status GetStatus(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetStatusRequest* request,
      intrinsic_proto::icon::v1::GetStatusResponse* response) override {
    return GetStatusConst(context, request, response);
  }

  ::grpc::Status GetStatusConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetStatusRequest* request,
      intrinsic_proto::icon::v1::GetStatusResponse* response) const {
    absl::Time deadline = absl::FromChrono(context->deadline());
    {
      INTR_ASSIGN_OR_RETURN_GRPC(
          (absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>
               part_status_map),
          robot_connection_.GetPartCollection().GetPartStatuses(deadline));
      *response->mutable_part_status() = {part_status_map.begin(),
                                          part_status_map.end()};
    }
    INTR_ASSIGN_OR_RETURN_GRPC(
        *response->mutable_safety_status(),
        robot_connection_.GetPartCollection().GetSafetyStatus(deadline));
    {
      absl::MutexLock l(session_map_mutex_);
      for (const auto& [key, session_data] : session_id_to_session_map_) {
        intrinsic_proto::icon::v1::GetStatusResponse::SessionStatus
            session_status;
        for (const auto& part_name :
             session_data->GetInterface().GetPartGroup()) {
          session_status.mutable_part_group()->add_parts(part_name);
        }
        for (const ActionInstanceId& action_id :
             session_data->GetInterface().GetActionInstanceIds()) {
          session_status.add_action_ids(action_id.value());
        }
        if (bool inserted = response->mutable_sessions()
                                ->insert({key.value(), session_status})
                                .second;
            !inserted) {
          return ToGrpcStatus(absl::InternalError(
              absl::StrCat("Duplicate Session ID ", key.value())));
        }
      }
    }
    return ToGrpcStatus(absl::OkStatus());
  }

  // Reports whether an action is compatible with a part or slot_part_map.
  ::grpc::Status IsActionCompatible(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::IsActionCompatibleRequest* request,
      intrinsic_proto::icon::v1::IsActionCompatibleResponse* response)
      override {
    response->set_is_compatible(false);
    if (request->has_part_name()) {
      if (robot_connection_
              .ActionCompatibleWithPart(request->action_type_name(),
                                        request->part_name())
              .ok()) {
        response->set_is_compatible(true);
      }
      return ToGrpcStatus(absl::OkStatus());
    } else if (request->has_slot_part_map()) {
      SlotPartMap slot_part_map =
          SlotPartMapFromProto(request->slot_part_map());
      if (robot_connection_
              .ActionCompatibleWithSlotPartMap(request->action_type_name(),
                                               slot_part_map)
              .ok()) {
        response->set_is_compatible(true);
      }
      return ToGrpcStatus(absl::OkStatus());
    }
    return ToGrpcStatus(absl::InvalidArgumentError(
        absl::StrCat("Expected IsActionCompatibleRequest to have either a "
                     "part_name or slot_part_map; request=",
                     *request)));
  }

  // Lists details of all available action types.
  ::grpc::Status ListActionSignatures(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::ListActionSignaturesRequest* request,
      intrinsic_proto::icon::v1::ListActionSignaturesResponse* response)
      override {
    return ListActionSignaturesConst(context, request, response);
  }

  ::grpc::Status ListActionSignaturesConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::ListActionSignaturesRequest* request,
      intrinsic_proto::icon::v1::ListActionSignaturesResponse* response) const {
    absl::c_transform(robot_connection_.ActionTypeToSignature(),
                      google::protobuf::RepeatedFieldBackInserter(
                          response->mutable_action_signatures()),
                      [](const auto& action_type_name_and_signature) {
                        return action_type_name_and_signature.second;
                      });
    return ToGrpcStatus(absl::OkStatus());
  }

  // Lists all parts that are compatible with a list of action types.
  ::grpc::Status ListCompatibleParts(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::ListCompatiblePartsRequest* request,
      intrinsic_proto::icon::v1::ListCompatiblePartsResponse* response)
      override {
    return ListCompatiblePartsConst(context, request, response);
  }

  ::grpc::Status ListCompatiblePartsConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::ListCompatiblePartsRequest* request,
      intrinsic_proto::icon::v1::ListCompatiblePartsResponse* response) const {
    std::vector<std::string> action_type_names = {
        request->action_type_names().begin(),
        request->action_type_names().end()};

    const auto action_type_to_signature =
        robot_connection_.ActionTypeToSignature();
    for (const auto& action_type_name : action_type_names) {
      if (action_type_to_signature.find(action_type_name) ==
          action_type_to_signature.end()) {
        return ToGrpcStatus(
            absl::NotFoundError(ActionTypeNotFoundHelpText(action_type_name)));
      }
    }
    for (const auto& part_config :
         robot_connection_.GetPartCollection().GetPartConfigs()) {
      if (!PartIsCompatibleWithAllActionTypes(
              part_config.name(), action_type_names, robot_connection_)) {
        continue;
      }
      *response->add_parts() = part_config.name();
    }

    return ToGrpcStatus(absl::OkStatus());
  }

  // Lists all available parts.
  ::grpc::Status ListParts(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::ListPartsRequest* request,
      intrinsic_proto::icon::v1::ListPartsResponse* response) override {
    return ListPartsConst(context, request, response);
  }

  ::grpc::Status ListPartsConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::ListPartsRequest* request,
      intrinsic_proto::icon::v1::ListPartsResponse* response) const {
    const auto& parts =
        robot_connection_.GetPartCollection().GetPartsManager().AllParts();
    *(response->mutable_parts()) = {parts.begin(), parts.end()};
    return ToGrpcStatus(absl::OkStatus());
  }

  // Opens an action session with the client.
  ::grpc::Status OpenSession(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<intrinsic_proto::icon::v1::OpenSessionResponse,
                                 intrinsic_proto::icon::v1::OpenSessionRequest>*
          stream) override ABSL_LOCKS_EXCLUDED(session_map_mutex_) {
    auto cancellable_stream = RegisterCancellableGrpcStream(context);
    intrinsic_proto::icon::v1::OpenSessionRequest request;
    if (!stream->Read(&request)) {
      constexpr char kMissingRequestData[] = "No initial request.";
      LOG(ERROR) << kMissingRequestData;
      return ToGrpcStatus(absl::InvalidArgumentError(kMissingRequestData));
    }

    LogOpenSessionRequest(request);

    if (!request.has_initial_session_data()) {
      constexpr char kMissingInitialSessionData[] =
          "Initial request is missing `initial_session_data`.";
      LOG(ERROR) << kMissingInitialSessionData;
      return ToGrpcStatus(
          absl::InvalidArgumentError(kMissingInitialSessionData));
    }
    if (!request.initial_session_data().has_allocate_parts()) {
      constexpr char kMissingParts[] =
          "Initial request's `initial_session_data` has no parts to "
          "allocate.";
      LOG(ERROR) << kMissingParts;
      return ToGrpcStatus(absl::InvalidArgumentError(kMissingParts));
    }

    absl::flat_hash_set<std::string> parts(
        request.initial_session_data().allocate_parts().part().begin(),
        request.initial_session_data().allocate_parts().part().end());

    absl::Time deadline = absl::Now() + kDefaultStartSessionTimeout;
    if (request.initial_session_data().has_deadline()) {
      INTR_ASSIGN_OR_RETURN_GRPC(
          deadline, ToAbslTime(request.initial_session_data().deadline()));
    }

    RealtimeOperationalStatus status =
        robot_connection_.MutableOperationalStateInterface()
            .GetInternalStatus();
    if (status.state == RealtimeOperationalState::kFatallyFaulted) {
      return ToGrpcStatus(absl::FailedPreconditionError(absl::StrCat(
          "Cannot open Session because ICON server is fatally faulted: ",
          status.fault_reason)));
    }
    if (!parts.empty()) {
      // If parts are to be controlled, wait for them to enable while they are
      // disabled.
      while (true) {
        if (absl::Now() > deadline) {
          return ToGrpcStatus(absl::DeadlineExceededError(
              "Parts did not become enabled before request deadline."));
        }
        INTR_ASSIGN_OR_RETURN_GRPC(auto part_states,
                                   robot_connection_.GetPartStates());
        bool any_faulted = false;
        bool all_enabled = true;
        for (const auto& part_name : parts) {
          auto it = part_states.find(part_name);
          if (it == part_states.end()) {
            return ToGrpcStatus(absl::NotFoundError(
                absl::StrCat("Part '", part_name, "' is not known.")));
          }
          auto part_state = it->second;
          any_faulted |= (part_state == OperationalState::kFaulted);
          all_enabled &= (part_state == OperationalState::kEnabled);
        }
        if (any_faulted) {
          return ToGrpcStatus(absl::FailedPreconditionError(absl::StrCat(
              "Cannot open Session because ICON server is FAULTED: ",
              status.fault_reason)));
        }
        if (all_enabled) {
          break;
        }
        absl::SleepFor(absl::Milliseconds(100));
      }
    }

    SessionId session_id = session_id_sequence_.GetNext();

    // Allocate parts only on session initialization.
    INTR_RETURN_IF_ERROR_GRPC(robot_connection_.MutablePartCollection()
                                  .GetPartsManager()
                                  .SetPartsAsUnavailable(parts));

    // If Session fails to create, then we need to manually release the
    // acquired parts.
    auto part_cleanup = absl::MakeCleanup([this, &parts]() {
      if (auto status = robot_connection_.MutablePartCollection()
                            .GetPartsManager()
                            .SetPartsAsAvailable(parts);
          !status.ok()) {
        LOG(ERROR) << "Failed to release parts during failed session creation.";
      }
    });

    INTR_ASSIGN_OR_RETURN_GRPC(RealtimeLogContext log_context,
                               FromProto(request.log_context()));
    INTR_ASSIGN_OR_RETURN_GRPC(
        std::unique_ptr<SessionInterface> session_interface,
        robot_connection_.CreateSession(parts, session_id, log_context),
        _ << "Failed to open action session.");
    // Save the map from action type name to signature proto.
    const auto action_type_to_signature =
        robot_connection_.ActionTypeToSignature();

    // If we've reached this point, then session can take responsibility
    // for releasing parts.
    std::move(part_cleanup).Cancel();

    SessionInstance* session = nullptr;
    {
      absl::MutexLock l(session_map_mutex_);
      session_id_to_session_map_[session_id] =
          std::make_unique<SessionInstance>(
              session_id, std::move(session_interface), context);
      session = session_id_to_session_map_[session_id].get();
    }
    // Clean up the Session no matter which of the returns below we take!
    auto session_cleanup = absl::MakeCleanup(absl::bind_front(
        &ApplicationLayerService::CleanupSession, this, parts, session));

    SessionHandler session_handler(
        &session->GetInterface(),
        [this](absl::string_view action_type_name,
               const intrinsic::icon::SlotPartMap& slot_part_map) {
          return robot_connection_.ActionCompatibleWithSlotPartMap(
              action_type_name, slot_part_map);
        });

    intrinsic_proto::icon::v1::OpenSessionResponse response =
        session_handler.HandleOpenSessionRequest(request,
                                                 action_type_to_signature);
    response.mutable_initial_session_data()->set_session_id(session_id.value());
    if (auto absl_status = ToAbslStatus(response.status()); !absl_status.ok()) {
      std::string debug_message =
          GenerateDebugMessage(session->GetId(), deadline);
      intrinsic_proto::status::ExtendedStatus extended_status =
          CreateExtendedStatus(
              ExtendedStatusCodes::kFailedToHandleOpenSessionRequest,
              absl::StrCat(
                  "Failed to handle the initial OpenSessionRequest because:\n",
                  absl_status.message()),
              {.debug_message = debug_message});
      static_cast<void>(response.mutable_status()->add_details()->PackFrom(
          extended_status));  // TODO: Handle return value
    }
    LogOpenSessionResponse(response);
    if (!stream->Write(response)) {
      return ToGrpcStatus(absl::AbortedError(
          "Failed to write session initialization response to the client. The "
          "session is dead."));
    }

    LOG(INFO) << "PUBLIC: Created Session, id:" << session_id.value();
    while (stream->Read(&request)) {
      LogOpenSessionRequest(request);
      if (request.has_initial_session_data()) {
        return ToGrpcStatus(absl::FailedPreconditionError(
            "Received initial Session data when Session is already "
            "initialized."));
      }

      auto resp = session_handler.HandleOpenSessionRequest(
          request, action_type_to_signature);
      if (auto absl_status = ToAbslStatus(resp.status()); !absl_status.ok()) {
        std::string debug_message =
            GenerateDebugMessage(session->GetId(), deadline);
        intrinsic_proto::status::ExtendedStatus extended_status =
            CreateExtendedStatus(
                ExtendedStatusCodes::kFailedToHandleOpenSessionRequest,
                absl::StrCat(
                    "Failed to handle an OpenSessionRequest because:\n",
                    absl_status.message()),
                {.debug_message = debug_message});
        static_cast<void>(resp.mutable_status()->add_details()->PackFrom(
            extended_status));  // TODO: Handle return value
      }
      LogOpenSessionResponse(resp);

      if (!stream->Write(resp)) {
        return ToGrpcStatus(absl::AbortedError(
            "Failed to write streaming response to the client. Assuming the "
            "client is dead and ending Session."));
      }
    }

    LOG(INFO) << "PUBLIC: Finishing server-side Session. There may have been "
                 "errors.";
    session->error_write_done_.Notify();
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status WatchReactions(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::WatchReactionsRequest* request,
      ::grpc::ServerWriter<intrinsic_proto::icon::v1::WatchReactionsResponse>*
          writer) override {
    auto cancellable_stream = RegisterCancellableGrpcStream(context);
    SessionInstance* session = nullptr;
    {
      absl::MutexLock l(session_map_mutex_);
      auto it =
          session_id_to_session_map_.find(SessionId(request->session_id()));
      if (it == session_id_to_session_map_.end()) {
        return ToGrpcStatus(absl::FailedPreconditionError(absl::StrCat(
            "No session with session_id ", request->session_id(), " exists")));
      }
      session = it->second.get();

      // Don't allow `session` to be deleted before the state
      // machine is done being used by WatchReactions(). To enforce this we
      // must keep `session_map_mutex_` locked until we have `watch_reactions_`.
      if (session->stop_watch_reactions_.HasBeenNotified()) {
        return ToGrpcStatus(absl::AbortedError(
            absl::StrCat("Session ended before watcher started. session_id: ",
                         request->session_id())));
      }
      if (!session->watch_reactions_.try_lock()) {
        return ToGrpcStatus(absl::UnavailableError(
            "WatchReactions already used by another session."));
      }
    }

    // Unlocking in an absl::Cleanup would be cleaner, but does not compile for
    // TryLock (b/16712284), so move logic to subfunction to ensure unlock in
    // every case.
    auto status = WatchReactionsInternal(session, writer);
    if (!status.ok()) {
      LOG(WARNING) << "Error while watching reactions: "
                   << ToAbslStatus(status);
    }
    session->watch_reactions_.unlock();
    return status;
  }

  ::grpc::Status WatchReactionsInternal(
      SessionInstance* session,
      ::grpc::ServerWriter<intrinsic_proto::icon::v1::WatchReactionsResponse>*
          writer) ABSL_EXCLUSIVE_LOCKS_REQUIRED(session->watch_reactions_) {
    // Send one empty reaction when connected.
    intrinsic_proto::icon::v1::WatchReactionsResponse response;
    if (!writer->Write(response)) {
      return ToGrpcStatus(
          absl::AbortedError("The stream closed unexpectedly."));
    }

    while (!session->stop_watch_reactions_.HasBeenNotified()) {
      absl::StatusOr<std::optional<ReactionEvent>> reaction_event =
          session->GetInterface().PollReactions();
      if (!reaction_event.ok()) {
        // Give time for OpenSession stream to finish first because that has
        // more helpful error messages.
        //
        // NOTE: if b/329816413 or similar issues reappear in the future,
        // increasing this timeout should be the first thing to try.
        session->error_write_done_.WaitForNotificationWithTimeout(
            absl::Milliseconds(100));
        // For server sided session closures, this is the only way to end the
        // OpenSessions stream.
        session->open_session_stream_->TryCancel();
        LOG(INFO) << "Cancelling session " << session->GetId()
                  << " from server side";

        StatusBuilder builder(reaction_event.status());
        std::string debug_message = GenerateDebugMessage(
            session->GetId(), absl::Now() + absl::Milliseconds(100));
        builder.AttachExtendedStatus(CreateExtendedStatus(
            ExtendedStatusCodes::kFailedToPollReactions,
            absl::StrCat("Failed while watching reactions because:\n",
                         reaction_event.status().message()),
            {.debug_message = debug_message}));
        return ToGrpcStatus(builder);
      }
      intrinsic_proto::icon::v1::WatchReactionsResponse response;
      if (reaction_event->has_value()) {
        *response.mutable_reaction_event() =
            ReactionEventToProto(reaction_event->value());
        if (!writer->Write(response)) {
          return ToGrpcStatus(
              absl::AbortedError("The stream closed unexpectedly."));
        }
      }
      absl::SleepFor(absl::Microseconds(1000));
    }
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status OpenWriteStream(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<
          intrinsic_proto::icon::v1::OpenWriteStreamResponse,
          intrinsic_proto::icon::v1::OpenWriteStreamRequest>* stream) override {
    auto cancellable_stream = RegisterCancellableGrpcStream(context);
    ::grpc::Status status = OpenWriteStreamImpl(context, stream);
    if (!status.ok()) {
      LOG(ERROR) << status.error_message();
    }
    return status;
  }

  ::grpc::Status OpenWriteStreamImpl(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<
          intrinsic_proto::icon::v1::OpenWriteStreamResponse,
          intrinsic_proto::icon::v1::OpenWriteStreamRequest>* stream) {
    intrinsic_proto::icon::v1::OpenWriteStreamRequest request;
    constexpr char kMissingRequestData[] = "No initial request.";
    if (!stream->Read(&request)) {
      return ToGrpcStatus(absl::InvalidArgumentError(kMissingRequestData));
    }

    if (!request.has_add_write_stream()) {
      return ToGrpcStatus(absl::InvalidArgumentError(kMissingRequestData));
    }

    {
      absl::MutexLock l(session_map_mutex_);
      auto it =
          session_id_to_session_map_.find(SessionId(request.session_id()));
      if (it == session_id_to_session_map_.end()) {
        return ToGrpcStatus(absl::FailedPreconditionError(absl::StrCat(
            "No session with session_id ", request.session_id(), " exists")));
      }
    }

    // Report success to the client
    intrinsic_proto::icon::v1::OpenWriteStreamResponse init_response;
    *init_response.mutable_add_stream_response()->mutable_status() =
        SaveStatusAsRpcStatus(absl::OkStatus());
    if (!stream->Write(init_response)) {
      // Session ended before we could report success to the client.
      return ToGrpcStatus(absl::OkStatus());
    }

    ActionInstanceId action_id(request.add_write_stream().action_id());
    std::string field_name = request.add_write_stream().field_name();

    while (true) {
      intrinsic_proto::icon::v1::OpenWriteStreamRequest value_req;
      if (!stream->Read(&value_req)) {
        // Client is done writing to this stream, so we can't read any more.
        return ToGrpcStatus(absl::OkStatus());
      }
      if (value_req.has_add_write_stream()) {
        return ToGrpcStatus(absl::UnimplementedError(
            "Opening additional streams is unimplemented."));
      }

      absl::MutexLock l(session_map_mutex_);
      auto session_it =
          session_id_to_session_map_.find(SessionId(request.session_id()));
      if (session_it == session_id_to_session_map_.end()) {
        return ToGrpcStatus(absl::AbortedError(
            "The parent session for this streaming input writer no longer "
            "exists. Check the return value of RunWatcherLoop() for errors."));
      }
      SessionInstance* session = session_it->second.get();

      intrinsic_proto::icon::v1::OpenWriteStreamResponse value_resp;
      if (value_req.has_write_value()) {
        absl::Status write_status =
            session->GetInterface().WriteToStreamingInput(
                action_id, field_name, value_req.write_value().value());
        *value_resp.mutable_write_value_response() =
            SaveStatusAsRpcStatus(write_status);
      }

      if (!stream->Write(value_resp)) {
        // Client is done reading from this stream, so we can't write any more.

        return ToGrpcStatus(absl::OkStatus());
      }
    }
  }

  ::grpc::Status GetLatestStreamingOutput(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetLatestStreamingOutputRequest* request,
      intrinsic_proto::icon::v1::GetLatestStreamingOutputResponse* response)
      override {
    return GetLatestStreamingOutputConst(context, request, response);
  }

  ::grpc::Status GetLatestStreamingOutputConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetLatestStreamingOutputRequest* request,
      intrinsic_proto::icon::v1::GetLatestStreamingOutputResponse* response)
      const ABSL_LOCKS_EXCLUDED(session_map_mutex_) {
    absl::MutexLock l(session_map_mutex_);
    auto session_it =
        session_id_to_session_map_.find(SessionId(request->session_id()));
    if (session_it == session_id_to_session_map_.end()) {
      return ToGrpcStatus(absl::FailedPreconditionError(absl::StrCat(
          "No session with session_id, ", request->session_id(), " exists")));
    }

    INTR_ASSIGN_OR_RETURN_GRPC(
        *response->mutable_output(),
        session_it->second->GetInterface().GetLatestStreamingOutput(
            ActionInstanceId(request->action_id()),
            absl::FromChrono(context->deadline())));
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status GetPlannedTrajectory(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetPlannedTrajectoryRequest* request,
      ::grpc::ServerWriter<
          ::intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse>* stream)
      override {
    return GetPlannedTrajectoryConst(context, request, stream);
  }

  ::grpc::Status GetPlannedTrajectoryConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetPlannedTrajectoryRequest* request,
      ::grpc::ServerWriter<
          ::intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse>* stream)
      ABSL_LOCKS_EXCLUDED(session_map_mutex_) {
    intrinsic_proto::icon::JointTrajectoryPVA planned_trajectory;
    {
      absl::MutexLock l(session_map_mutex_);
      auto session_it =
          session_id_to_session_map_.find(SessionId(request->session_id()));
      if (session_it == session_id_to_session_map_.end()) {
        return ToGrpcStatus(absl::FailedPreconditionError(absl::StrCat(
            "No session with session_id, ", request->session_id(), " exists")));
      }

      INTR_ASSIGN_OR_RETURN_GRPC(
          planned_trajectory,
          session_it->second->GetInterface().GetPlannedTrajectory(
              ActionInstanceId(request->action_id())));
    }

    // Split proto trajectory into different segments for sending via stream.
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto planned_trajectory_segments,
        SplitTrajectoryProto(planned_trajectory,
                             /*max_subtrajectory_length=*/64));

    for (const auto& segment : planned_trajectory_segments) {
      // fill response with segment and send via stream.
      ::intrinsic_proto::icon::v1::GetPlannedTrajectoryResponse response;
      *response.mutable_planned_trajectory_segment() = segment;
      if (!stream->Write(response)) {
        return ::grpc::Status::CANCELLED;
      }
    }
    return ::grpc::Status::OK;
  }

  ::grpc::Status Enable(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::EnableRequest* request,
      intrinsic_proto::icon::v1::EnableResponse* response) override {
    LOG(INFO) << "PUBLIC: Enabling robot...";

    return ToGrpcStatus(
        robot_connection_.MutableOperationalStateInterface().Enable());
  }

  ::grpc::Status Disable(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::DisableRequest* request,
      intrinsic_proto::icon::v1::DisableResponse* response) override {
    LOG(INFO) << "PUBLIC: Disabling robot...";
    bool skip_cell_control_hardware = false;
    if (request->group() ==
        intrinsic_proto::icon::v1::DisableRequest::OPERATIONAL_HARDWARE_ONLY) {
      skip_cell_control_hardware = true;
    }
    return ToGrpcStatus(
        robot_connection_.MutableOperationalStateInterface().Disable(
            skip_cell_control_hardware));
  }

  ::grpc::Status ClearFaults(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::ClearFaultsRequest* request,
      intrinsic_proto::icon::v1::ClearFaultsResponse* response) override {
    auto clear_faults_status =
        robot_connection_.MutableOperationalStateInterface().ClearFaults();
    auto enable_status =
        robot_connection_.MutableOperationalStateInterface().Enable();
    if (!clear_faults_status.ok()) {
      return ToGrpcStatus(clear_faults_status);
    }
    return ToGrpcStatus(enable_status);
  }

  ::grpc::Status GetOperationalStatus(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetOperationalStatusRequest* request,
      intrinsic_proto::icon::v1::GetOperationalStatusResponse* response)
      override {
    return GetOperationalStatusConst(context, request, response);
  }

  ::grpc::Status GetOperationalStatusConst(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetOperationalStatusRequest* request,
      intrinsic_proto::icon::v1::GetOperationalStatusResponse* response) const {
    INTR_ASSIGN_OR_RETURN_GRPC(
        OperationalStatus operational_status,
        robot_connection_.MutableOperationalStateInterface().GetStatus());
    INTR_ASSIGN_OR_RETURN_GRPC(
        OperationalStatus cell_control_status,
        robot_connection_.MutableOperationalStateInterface()
            .GetCellControlStatus());
    *response->mutable_operational_status() = ToProto(operational_status);
    *response->mutable_cell_control_hardware_status() =
        ToProto(cell_control_status);
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status SetSpeedOverride(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::SetSpeedOverrideRequest* request,
      intrinsic_proto::icon::v1::SetSpeedOverrideResponse* response) override {
    if (request->override_factor() > 1 || request->override_factor() < 0) {
      return ToGrpcStatus(absl::InvalidArgumentError(
          absl::StrCat("Speed override factor must be between 0 and 1, got",
                       request->override_factor())));
    }
    robot_connection_.SetSpeedOverride(request->override_factor());
    LOG(INFO) << "Set speed override to " << request->override_factor();
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status GetSpeedOverride(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetSpeedOverrideRequest* request,
      intrinsic_proto::icon::v1::GetSpeedOverrideResponse* response) override {
    response->set_override_factor(robot_connection_.GetSpeedOverride());
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status SetLoggingMode(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::SetLoggingModeRequest* request,
      intrinsic_proto::icon::v1::SetLoggingModeResponse* response) override {
    robot_connection_.SetLoggingMode(FromProto(request->logging_mode()));
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status GetLoggingMode(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetLoggingModeRequest* request,
      intrinsic_proto::icon::v1::GetLoggingModeResponse* response) override {
    response->set_logging_mode(ToProto(robot_connection_.GetLoggingMode()));
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status GetPartProperties(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetPartPropertiesRequest* request,
      intrinsic_proto::icon::v1::GetPartPropertiesResponse* response) override {
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto properties,
        robot_connection_.MutablePartCollection().GetPartProperties());
    INTR_RETURN_IF_ERROR_GRPC(intrinsic::FromAbslDuration(
        properties.timestamp_control, response->mutable_timestamp_control()));
    INTR_RETURN_IF_ERROR_GRPC(intrinsic::FromAbslTime(
        properties.timestamp_wall, response->mutable_timestamp_wall()));
    for (const auto& [part_name, part_properties] : properties.properties) {
      intrinsic_proto::icon::v1::PartPropertyValues part_properties_proto;
      for (const auto& [property_name, property_value] : part_properties) {
        part_properties_proto.mutable_property_values_by_name()->insert(
            {property_name, ToProto(property_value)});
      }
      response->mutable_part_properties_by_part_name()->insert(
          {part_name, std::move(part_properties_proto)});
    }
    return ToGrpcStatus(absl::OkStatus());
  }

  absl::Status SetPartPropertiesInternal(const PartPropertyMap& properties) {
    absl::MutexLock l(property_map_mutex_);
    PartPropertyMap new_property_map = property_map_;

    for (const auto& [part_name, new_part_properties] : properties.properties) {
      // Note that operator[] inserts a new map into
      // new_property_map.properties if necessary.
      auto& part_properties = new_property_map.properties[part_name];

      // Merge the new properties in the existing one. Existing properties are
      // kept or overwritten by new ones.
      for (const auto& [property_name, property_value] : new_part_properties) {
        part_properties[property_name] = property_value;
      }
    }

    INTR_RETURN_IF_ERROR(
        robot_connection_.MutablePartCollection().SetPartProperties(
            new_property_map));
    // Only overwrite the previous property map if there were no errors.
    property_map_ = std::move(new_property_map);
    return absl::OkStatus();
  }

  ::grpc::Status SetPartProperties(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::SetPartPropertiesRequest* request,
      intrinsic_proto::icon::v1::SetPartPropertiesResponse* response) override {
    PartPropertyMap new_property_map;
    for (const auto& [part_name, part_properties_proto] :
         request->part_properties_by_part_name()) {
      // Note that operator[] inserts a new map into
      // new_property_map.properties if necessary.
      auto& part_properties = new_property_map.properties[part_name];
      for (const auto& [property_name, property_value_proto] :
           part_properties_proto.property_values_by_name()) {
        INTR_ASSIGN_OR_RETURN_GRPC(PartPropertyValue property_value,
                                   FromProto(property_value_proto));
        part_properties[property_name] = property_value;
      }
    }
    return ToGrpcStatus(SetPartPropertiesInternal(new_property_map));
  }

  ::grpc::Status SetPayload(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::SetPayloadRequest* request,
      intrinsic_proto::icon::v1::SetPayloadResponse* response) override {
    absl::MutexLock l(session_map_mutex_);
    if (!session_id_to_session_map_.empty()) {
      return ToGrpcStatus(absl::FailedPreconditionError(
          absl::StrCat("There are ", session_id_to_session_map_.size(),
                       " active sessions. Cannot update payload while there "
                       "are open sessions.")));
    }
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto previous_properties,
        robot_connection_.MutablePartCollection().GetPartProperties());
    std::optional<RobotPayload> previous_payload;
    if (auto it = previous_properties.properties.find(request->part_name());
        it != previous_properties.properties.end()) {
      auto payload = FromPartPropertyMap(it->second, request->payload_name());
      // If there is no payload or an error, we don't need to save it.
      if (payload.ok()) {
        previous_payload = *payload;
      } else if (!absl::IsNotFound(payload.status())) {
        LOG(ERROR) << "Failed to get previous payload. Will not be able to "
                      "restore the previous payload in case set_payload fails. "
                      "Reason: "
                   << payload.status();
      }
    }

    INTR_ASSIGN_OR_RETURN_GRPC(RobotPayload payload,
                               intrinsic::FromProto(request->payload()));
    PartPropertyMap new_property_map;
    INTR_ASSIGN_OR_RETURN_GRPC(
        new_property_map.properties[request->part_name()],
        ToPartPropertyMap(payload, request->payload_name()));
    INTR_RETURN_IF_ERROR_GRPC(SetPartPropertiesInternal(new_property_map));
    auto reset_payload_on_any_error = absl::MakeCleanup([this, request,
                                                         &previous_payload]() {
      if (auto status = [this, request, &previous_payload]() -> absl::Status {
            PartPropertyMap new_property_map;
            INTR_ASSIGN_OR_RETURN(
                new_property_map.properties[request->part_name()],
                ToPartPropertyMap(previous_payload, request->payload_name()));
            INTR_RETURN_IF_ERROR(SetPartPropertiesInternal(new_property_map));
            return absl::OkStatus();
          }();
          !status.ok()) {
        LOG(ERROR) << "Failed to reset payload: " << status;
      }
    });
    // Only re-enable if the server is enabled (to avoid unexpected motion).
    auto status =
        robot_connection_.MutableOperationalStateInterface().GetStatus();
    if (!status.ok()) {
      return ToGrpcStatus(absl::FailedPreconditionError(
          "Failed to set payload: GetStatus failed."));
    }
    if (IsFaulted(*status)) {
      return ToGrpcStatus(absl::FailedPreconditionError(
          absl::StrCat("Failed to set payload because server is faulted: ",
                       status->fault_reason())));
    }
    if (!IsEnabled(*status)) {
      return ToGrpcStatus(absl::FailedPreconditionError(absl::StrCat(
          "Failed to set payload because operational state is not enabled: ",
          status->state())));
    }
    // Disable/enable to apply the changes.
    INTR_RETURN_IF_ERROR_GRPC(
        robot_connection_.MutableOperationalStateInterface().Disable(
            /*skip_cell_control_hardware=*/false))
        << " while setting payload";
    INTR_RETURN_IF_ERROR_GRPC(
        robot_connection_.MutableOperationalStateInterface().Enable())
        << " while setting payload";
    std::move(reset_payload_on_any_error).Cancel();
    LOG(INFO) << "Set payload '" << request->payload_name() << "' for part '"
              << request->part_name() << "' successfully to: " << payload;
    return ToGrpcStatus(absl::OkStatus());
  }

  ::grpc::Status GetPayload(
      ::grpc::ServerContext* context,
      const intrinsic_proto::icon::v1::GetPayloadRequest* request,
      intrinsic_proto::icon::v1::GetPayloadResponse* response) override {
    INTR_ASSIGN_OR_RETURN_GRPC(
        auto properties,
        robot_connection_.MutablePartCollection().GetPartProperties());
    auto it = properties.properties.find(request->part_name());
    if (it == properties.properties.end()) {
      return ToGrpcStatus(absl::NotFoundError(absl::StrCat(
          "Part ", request->part_name(), " not found. Parts with properties: ",
          absl::StrJoin(gtl::key_view(properties.properties), ", "))));
    }

    INTR_ASSIGN_OR_RETURN_GRPC(
        std::optional<RobotPayload> payload,
        FromPartPropertyMap(it->second, request->payload_name()));
    if (payload.has_value()) {
      *response->mutable_payload() = intrinsic::ToProto(*payload);
    }
    return ToGrpcStatus(absl::OkStatus());
  }

 private:
  // Cleans up the session. Performs all cleanup tasks on the `session`,
  // and erases the action operator. `session` is deleted.
  void CleanupSession(const absl::flat_hash_set<std::string>& parts,
                      SessionInstance* session)
      ABSL_LOCKS_EXCLUDED(session_map_mutex_) {
    SessionId session_id = session->GetId();
    LOG(INFO) << "Cleaning up session " << session_id;
    // First stop reaction watchers, then end the sessions. If we ended the
    // session first, that would clobber any errors that the corresponding
    // watcher has not reported yet.
    session->stop_watch_reactions_.Notify();
    // We need to acquire both `session_id_to_session_map_` and
    // `session->watching_reactions_` in this order, but we should not lock
    // `session_id_to_session_map_` too long. So, we briefly acquire and release
    // `watch_reactions_` to ensure it is available, and then lock
    // `session_map_mutex_` and `watch_reactions_` in the right order.
    {
      absl::MutexLock l(session->watch_reactions_);
    }
    absl::MutexLock l(session_map_mutex_);
    {
      // Ensure WatchReactions stream has ended.
      absl::MutexLock l(session->watch_reactions_);

      // It is logically impossible for this to fail, since this is the same
      // part set that we successfully claimed for this session.
      {
        CHECK_OK(robot_connection_.MutablePartCollection()
                     .GetPartsManager()
                     .SetPartsAsAvailable(parts));
      }
    }
    LOG(INFO) << "Removed watchers and parts for session " << session_id;
    // Deletes the action operator
    session_id_to_session_map_.erase(session_id);
    LOG(INFO) << "Cleaned up session " << session_id;
    // The following is used to inject notification of call cleanup for
    // testing.
    testing::NotifyTestCallbackIfNeeded("icon_session_notify_cleanup_complete");
  }

  // Generates a human-readable debug message which can be attached to the
  // ExtendedStatus.
  //
  // `session_id` is the ID of the session that generated the error.
  // `deadline` is the deadline of the request that generated the error. This is
  // necessary to ensure that deadlines of parent calls are propagated to the
  // GetPartStatuses call.
  //
  // Returns a string containing a human-readable debug message, or an error if
  // there was an error generating the report.
  std::string GenerateDebugMessage(
      const SessionId session_id,
      const absl::Time deadline = absl::InfiniteFuture()) {
    std::string report = absl::StrCat("\nSession ID: ", session_id.value());
    {
      absl::StatusOr<
          absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>>
          part_status_map =
              robot_connection_.GetPartCollection().GetPartStatuses(deadline);
      if (part_status_map.ok()) {
        for (const auto& [part_name, part_status] : *part_status_map) {
          std::string part_status_string;
          absl::StrAppend(&report, "\nPart statuses at time of error:");
          static_cast<void>(google::protobuf::TextFormat::PrintToString(
              part_status, &part_status_string));  // TODO: Handle return value
          absl::StrAppend(&report, "\nPart: ", part_name, "\nPart Status:\n",
                          part_status_string);
        }

      } else {
        absl::StrAppend(&report, "\nPart statuses not available: ",
                        part_status_map.status().message());
      }
    }
    if (report.size() > (kMaxMetadataMessageSize)) {
      const std::string truncation_message =
          "\nThis error message was truncated because it was too long.";
      report.resize(kMaxMetadataMessageSize - truncation_message.size());
      absl::StrAppend(&report, truncation_message);
    }
    return report;
  }

  // Atomic sequence generator for session IDs.
  SequenceNumber<SessionId> session_id_sequence_;

  // Locks the session mapping. Mutable because it is used by otherwise const
  // methods.
  mutable absl::Mutex session_map_mutex_;
  // Stores the action session data. Insertion and deletion must be protected
  // by the `session_map_mutex_`. Handlers must only access/delete the
  // SessionInstance corresponding to their session's ID. Handlers can
  // store a raw pointer to the SessionInstance, but should not store an
  // iterator to the map, which may be invalidated by insertion/deletion
  // operations performed by other sessions.
  absl::flat_hash_map<SessionId, std::unique_ptr<SessionInstance>>
      session_id_to_session_map_ ABSL_GUARDED_BY(session_map_mutex_);

  // All of the methods on this should be thread safe.
  RobotConnectionInterface& robot_connection_;

  mutable absl::Mutex property_map_mutex_
      ABSL_ACQUIRED_AFTER(session_map_mutex_);
  // This map holds the last-set values for any part properties. We cache part
  // property values to ensure that the realtime thread does not "miss" an
  // update, for example if a client sets two different properties in quick
  // succession.
  // Its size is bounded by the number of available part properties.
  PartPropertyMap property_map_ ABSL_GUARDED_BY(property_map_mutex_);

  absl::Mutex open_streams_mutex_ ABSL_ACQUIRED_AFTER(property_map_mutex_);
  absl::flat_hash_set<::grpc::ServerContext*> open_grpc_streams_
      ABSL_GUARDED_BY(open_streams_mutex_);
  // This Notification tells any outstanding `ScopedCancellableGrpcContext`s
  // that the `ApplicationLayerService` is gone, and should not be accessed any
  // more.
  std::shared_ptr<absl::Notification> shutdown_notification_;
};

}  // namespace

std::unique_ptr<IconApiService> CreateApplicationLayerService(
    RobotConnectionInterface& robot_connection) {
  return std::make_unique<ApplicationLayerService>(robot_connection);
}

}  // namespace icon
}  // namespace intrinsic
