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

#include "intrinsic/executive/engine/clips_skill_dispatcher.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/util/message_differencer.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/executive/clips_cpp/assert_facade.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/readonly_facade.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/engine/clips_world.h"
#include "intrinsic/executive/engine/skill_action.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/engine/skill_instance.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/geometry/proto/v1/exact_geometry.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.grpc.pb.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.pb.h"
#include "intrinsic/skills/internal/error_utils.h"
#include "intrinsic/skills/internal/skill_registry_client_interface.h"
#include "intrinsic/skills/internal/skill_service_client_interface.h"
#include "intrinsic/skills/proto/error.pb.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/skills/proto/prediction.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/stats/tracing_utils.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/tracer.h"

namespace intrinsic {
namespace executive {

using ::intrinsic_proto::geometry::v1::ExactGeometry;
using ::intrinsic_proto::skills::Footprint;
using ::intrinsic_proto::skills::VolumeReservation;

namespace {

std::string SkillActionNameNoun(SkillAction skill_action) {
  std::string action_name = "unknown";
  switch (skill_action) {
    case SkillAction::Prediction:
      action_name = "prediction";
      break;
    case SkillAction::Projection:
      action_name = "projection";
      break;
    case SkillAction::Execution:
      action_name = "execution";
      break;
    case SkillAction::PreviewExecution:
      action_name = "preview execution";
      break;
    case SkillAction::FootprintChecking:
      action_name = "footprint checking";
      break;
  }
  return action_name;
}

std::string SkillActionNameVerb(SkillAction skill_action) {
  std::string action_name = "unknown";
  switch (skill_action) {
    case SkillAction::Prediction:
      action_name = "Predicting";
      break;
    case SkillAction::Projection:
      action_name = "Projecting";
      break;
    case SkillAction::Execution:
      action_name = "Executing";
      break;
    case SkillAction::PreviewExecution:
      action_name = "PreviewExecuting";
      break;
    case SkillAction::FootprintChecking:
      action_name = "FootprintChecking";
      break;
  }
  return action_name;
}

std::optional<intrinsic_proto::status::ExtendedStatus> ExtractExtendedStatus(
    const absl::Status& status) {
  std::optional<absl::Cord> es_payload = status.GetPayload(
      intrinsic::AddTypeUrlPrefix<intrinsic_proto::status::ExtendedStatus>());
  if (es_payload) {
    intrinsic_proto::status::ExtendedStatus es;
    if (es.ParseFromString(*es_payload)) {
      return es;
    }
  }
  return std::nullopt;
}

std::optional<intrinsic_proto::skills::SkillErrorInfo> ExtractSkillErrorInfo(
    const absl::Status& status) {
  std::optional<absl::Cord> error_info_payload = status.GetPayload(
      intrinsic::AddTypeUrlPrefix<intrinsic_proto::skills::SkillErrorInfo>());
  if (error_info_payload) {
    intrinsic_proto::skills::SkillErrorInfo error_info;
    if (error_info.ParseFromString(*error_info_payload)) {
      return error_info;
    }
  }
  return std::nullopt;
}

// The passed in Status must come from a skill execution. Create an
// ExtendedStatus representing that skill execution failure. If available just
// propagates an ExtendedStatus from the failure directly. Otherwise investigate
// an attached SkillInstance to create an ExtendedStatus.
intrinsic_proto::status::ExtendedStatus GetExtendedStatusForSkillResult(
    const absl::Status& result_status, std::string_view skill_id_version,
    SkillAction skill_action) {
  std::optional<intrinsic_proto::status::ExtendedStatus> es =
      ExtractExtendedStatus(result_status);
  if (es.has_value()) {
    return *es;
  }

  std::string action_name = SkillActionNameNoun(skill_action);
  std::optional<intrinsic_proto::skills::SkillErrorInfo> error_info =
      ExtractSkillErrorInfo(result_status);
  if (error_info.has_value()) {
    switch (error_info->error_type()) {
      case intrinsic_proto::skills::SkillErrorInfo::ERROR_TYPE_GRPC:
        return CreateExtendedStatus(
            20000, absl::StrFormat(
                       "Skill '%s' failed during %s in the gRPC call: %s",
                       skill_id_version, action_name, result_status.message()));
        break;
      case intrinsic_proto::skills::SkillErrorInfo::ERROR_TYPE_SKILL:
        return CreateExtendedStatus(
            18000,
            absl::StrFormat("Skill '%s' reported an error during %s: %s",
                            skill_id_version, action_name,
                            result_status.message()),
            {.debug_message =
                 "The skill responded with an error. Check the skill logs for "
                 "further information."});
        break;
      case intrinsic_proto::skills::SkillErrorInfo::ERROR_TYPE_UNKNOWN:
      default:
        break;
    }
  }
  LOG(WARNING)
      << "Skill " << skill_id_version
      << " set unknown error type or didn't provide SkillErrorInfo during "
      << action_name << ". Status is: " << result_status.ToString();
  return CreateExtendedStatus(
      18001,
      absl::StrFormat("Skill '%s' reported an unknown error during %s: %s",
                      skill_id_version, action_name, result_status.message()));
}

// Tests if the status contains an ExtendedStatus with
// kSkillServiceWaitTimeoutCode from the skill service.
bool IsSkillTimeoutStatus(const absl::Status& status) {
  if (status.ok()) {
    return false;
  }

  std::optional<intrinsic_proto::status::ExtendedStatus> es =
      ExtractExtendedStatus(status);
  if (!es.has_value()) {
    return false;
  }
  return (es->has_status_code() &&
          es->status_code().component() == skills::kSkillServiceComponent &&
          es->status_code().code() == skills::kSkillServiceWaitTimeoutCode);
};

struct ClipsStatusInfo {
  std::string_view message = "";
  clips::ProtoMessageId result_proto_id = clips::ProtobufManager::kInvalidId;
  clips::ProtoMessageId extended_status_proto_id =
      clips::ProtobufManager::kInvalidId;
};

void ReportClipsStatus(clips::EnvironmentAssertFacade* assert_facade,
                       absl::string_view action_id,
                       absl::string_view skill_status,
                       const ClipsStatusInfo& info = ClipsStatusInfo())
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade->GetClipsMutex()) {
  const stats::ScopedSpan span("ClipsSkillDispatcher/ReportClipsStatus");
  INTR_RETURN_IF_ERROR(
      assert_facade
          ->AssertFact(
              "skill-status",
              {
                  {"action-id", clips::Symbol(action_id)},
                  {"status", clips::Symbol(skill_status)},
                  {"message", info.message},
                  {"return-value-proto-id", info.result_proto_id.value()},
                  {"extended-status-proto-id",
                   info.extended_status_proto_id.value()},
              })
          .status())
      .LogWarning()
      .With(intrinsic::ExtraMessage() << "Failed to report status to CLIPS")
      .With(ReturnVoid());
  assert_facade->NotifyRunner();
}

absl::StatusOr<intrinsic_proto::executive::BehaviorCall>
GetBehaviorCallProtoOrReportError(clips::EnvironmentAssertFacade* assert_facade,
                                  clips::ProtobufManager* proto_mgr,
                                  absl::string_view action_id,
                                  SkillAction skill_action,
                                  clips::ProtoMessageId behavior_call_proto_id)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade->GetClipsMutex()) {
  assert_facade->GetClipsMutex()->AssertHeld();
  absl::StatusOr<std::unique_ptr<intrinsic_proto::executive::BehaviorCall>>
      behavior_call_proto =
          proto_mgr->GetProtoAs<intrinsic_proto::executive::BehaviorCall>(
              behavior_call_proto_id);
  if (!behavior_call_proto.ok()) {
    intrinsic_proto::status::ExtendedStatus es_proto = CreateExtendedStatus(
        18100,
        absl::StrFormat("BehaviorCall proto not found during %s. Error: %s",
                        SkillActionNameNoun(skill_action),
                        behavior_call_proto.status().message()),
        {.debug_message =
             absl::StrFormat("Internal action id: %s", action_id)});
    clips::ProtoMessageId es_proto_id = proto_mgr->AddGeneratedProto(es_proto);
    ReportClipsStatus(assert_facade, action_id,
                      ClipsSkillDispatcher::kSkillStatusFailed,
                      {.message = absl::StrFormat(
                           "behavior_call proto for action '%s' not found: %s",
                           action_id, behavior_call_proto.status().message()),
                       .extended_status_proto_id = es_proto_id});
    return behavior_call_proto.status();
  }
  return **behavior_call_proto;
}

absl::StatusOr<intrinsic_proto::data_logger::Context>
GetContextProtoOrReportError(clips::EnvironmentAssertFacade* assert_facade,
                             clips::ProtobufManager* proto_mgr,
                             absl::string_view action_id,
                             absl::string_view skill_id,
                             SkillAction skill_action,
                             clips::ProtoMessageId context_proto_id)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade->GetClipsMutex()) {
  if (context_proto_id == clips::ProtobufManager::kInvalidId) {
    return intrinsic_proto::data_logger::Context();
  }

  absl::StatusOr<std::unique_ptr<intrinsic_proto::data_logger::Context>>
      context_proto =
          proto_mgr->GetProtoAs<intrinsic_proto::data_logger::Context>(
              context_proto_id);
  if (!context_proto.ok()) {
    intrinsic_proto::status::ExtendedStatus es_proto = CreateExtendedStatus(
        18100,
        absl::StrFormat("Logger context proto not found during %s. Error: %s",
                        SkillActionNameNoun(skill_action),
                        context_proto.status().message()),
        {.debug_message =
             absl::StrFormat("Internal action id: %s", action_id)});
    clips::ProtoMessageId es_proto_id = proto_mgr->AddGeneratedProto(es_proto);
    ReportClipsStatus(
        assert_facade, action_id, ClipsSkillDispatcher::kSkillStatusFailed,
        {.message =
             absl::StrFormat("Context proto for action '%s' not found: %s",
                             action_id, context_proto.status().message()),
         .extended_status_proto_id = es_proto_id});
    return context_proto.status();
  }
  return **context_proto;
}

absl::StatusOr<SkillInstance> CreateSkillInstanceOrReportError(
    clips::EnvironmentAssertFacade* assert_facade,
    clips::ProtobufManager* proto_mgr,
    SkillClientGenerator* skill_client_generator, absl::string_view action_id,
    SkillAction skill_action,
    intrinsic_proto::executive::BehaviorCall& behavior_call_proto,
    const clips::DescriptorPoolId descriptor_pool_id)
    ABSL_LOCKS_EXCLUDED(assert_facade->GetClipsMutex()) {
  const stats::ScopedSpan span(
      "ClipsSkillDispatcher/CreateSkillInstanceOrReportError");
  absl::StatusOr<SkillInstance> skill_instance =
      skill_client_generator->CreateSkillInstance(
          behavior_call_proto, descriptor_pool_id, skill_action);
  if (!skill_instance.ok()) {
    absl::MutexLock clips_lock(*assert_facade->GetClipsMutex());
    LOG(ERROR) << "Failed to CreateSkillInstance for "
               << behavior_call_proto.skill_id() << " using descriptor pool "
               << descriptor_pool_id << ": " << skill_instance.status();

    intrinsic_proto::status::ExtendedStatus es_proto = CreateExtendedStatus(
        18100,
        absl::StrFormat("Could not create skill client to call skill '%s' "
                        "during %s. Error: %s",
                        behavior_call_proto.skill_id(),
                        SkillActionNameNoun(skill_action),
                        skill_instance.status().message()),
        {.debug_message =
             absl::StrFormat("Internal action id: %s", action_id)});
    clips::ProtoMessageId es_proto_id = proto_mgr->AddGeneratedProto(es_proto);
    ReportClipsStatus(
        assert_facade, action_id, ClipsSkillDispatcher::kSkillStatusFailed,
        {.message =
             absl::StrFormat("Cannot create skill instance: %s",
                             skill_instance.status().ToString(
                                 absl::StatusToStringMode::kWithNoExtraData)),
         .extended_status_proto_id = es_proto_id});
  }
  return skill_instance;
}

std::optional<absl::Duration> GetProjectTimeoutOrDefault(
    const intrinsic_proto::executive::BehaviorCall& behavior_call_proto) {
  if (behavior_call_proto.skill_execution_options().has_project_timeout()) {
    absl::StatusOr<absl::Duration> timeout = ToAbslDuration(
        behavior_call_proto.skill_execution_options().project_timeout());
    if (timeout.ok()) {
      return *timeout;
    }
    LOG(WARNING)
        << "Could not parse project_timeout duration, using default timeout";
  }
  return std::nullopt;
}

std::optional<absl::Duration> GetExecuteTimeoutOrDefault(
    const intrinsic_proto::executive::BehaviorCall& behavior_call_proto) {
  if (behavior_call_proto.skill_execution_options().has_execute_timeout()) {
    absl::StatusOr<absl::Duration> timeout = ToAbslDuration(
        behavior_call_proto.skill_execution_options().execute_timeout());
    if (timeout.ok()) {
      return *timeout;
    }
    LOG(WARNING)
        << "Could not parse execute_timeout duration, using default timeout";
  }

  return std::nullopt;
}

// Adds footprint volumes specified in `behavior_call_proto` to the object world
// as a standalone scene object.
//
// If the behavior call contains no footprint or no footprint volumes, returns
// an empty string without contacting the object world service. On failure,
// asserts an error status to the CLIPS environment and returns the error
// status.
//
// Returns the name of the created footprint object on success, or an empty
// string if no volumes were added.
absl::StatusOr<std::string> AddFootprintVolumesOrReportError(
    clips::EnvironmentAssertFacade* assert_facade,
    clips::ProtobufManager* proto_mgr,
    intrinsic_proto::world::ObjectWorldService::StubInterface*
        object_world_service_stub,
    absl::string_view world_id, absl::string_view action_id,
    const intrinsic_proto::executive::BehaviorCall& behavior_call_proto)
    ABSL_LOCKS_EXCLUDED(assert_facade->GetClipsMutex()) {
  const stats::ScopedSpan span(
      "ClipsSkillDispatcher/AddFootprintVolumesOrReportError");
  if (!behavior_call_proto.has_skill_execution_data() ||
      !behavior_call_proto.skill_execution_data().has_footprint()) {
    return "";
  }

  const Footprint& footprint =
      behavior_call_proto.skill_execution_data().footprint();
  if (footprint.volume().empty()) {
    return "";
  }

  if (object_world_service_stub == nullptr) {
    absl::MutexLock clips_lock(*assert_facade->GetClipsMutex());
    intrinsic_proto::status::ExtendedStatus es_proto = CreateExtendedStatus(
        18101,
        absl::StrFormat(
            "Failed to create footprint object for skill '%s' during "
            "Execution. Error: Clips Skill Dispatcher configured without an "
            "object world service.",
            behavior_call_proto.skill_id()),
        {.debug_message =
             absl::StrFormat("Internal action id: %s", action_id)});
    clips::ProtoMessageId es_proto_id = proto_mgr->AddGeneratedProto(es_proto);
    ReportClipsStatus(
        assert_facade, action_id, ClipsSkillDispatcher::kSkillStatusFailed,
        {.message = absl::StrFormat(
             "Failed to create footprint object for '%s'", action_id),
         .extended_status_proto_id = es_proto_id});
    return absl::NotFoundError(
        "Clips Skill Dispatcher configured without an object world service.");
  }

  absl::StatusOr<std::string> result = AddFootprintVolumesToWorld(
      *object_world_service_stub, world_id, action_id, footprint);
  if (!result.ok()) {
    absl::MutexLock clips_lock(*assert_facade->GetClipsMutex());
    intrinsic_proto::status::ExtendedStatus es_proto = CreateExtendedStatus(
        18101,
        absl::StrFormat(
            "Failed to create footprint object for skill '%s' during "
            "Execution. Error: %s",
            behavior_call_proto.skill_id(), result.status().message()),
        {.debug_message =
             absl::StrFormat("Internal action id: %s", action_id)});
    clips::ProtoMessageId es_proto_id = proto_mgr->AddGeneratedProto(es_proto);
    ReportClipsStatus(
        assert_facade, action_id, ClipsSkillDispatcher::kSkillStatusFailed,
        {.message = absl::StrFormat(
             "Failed to create footprint object for '%s'", action_id),
         .extended_status_proto_id = es_proto_id});
    return result.status();
  }

  return *result;
}

// Removes footprint scene object specified by `footprint_object_name` from the
// object world.
//
// If `footprint_object_name` is empty, returns OkStatus immediately.
// On failure, asserts an error status to the CLIPS environment and returns the
// error status.
absl::Status RemoveFootprintVolumesOrReportError(
    clips::EnvironmentAssertFacade* assert_facade,
    clips::ProtobufManager* proto_mgr,
    intrinsic_proto::world::ObjectWorldService::StubInterface*
        object_world_service_stub,
    absl::string_view world_id, absl::string_view action_id,
    absl::string_view skill_id, absl::string_view footprint_object_name)
    ABSL_LOCKS_EXCLUDED(assert_facade->GetClipsMutex()) {
  const stats::ScopedSpan span(
      "ClipsSkillDispatcher/RemoveFootprintVolumesOrReportError");
  if (footprint_object_name.empty()) {
    return absl::OkStatus();
  }

  if (object_world_service_stub == nullptr) {
    absl::MutexLock clips_lock(*assert_facade->GetClipsMutex());
    intrinsic_proto::status::ExtendedStatus es_proto = CreateExtendedStatus(
        18101,
        absl::StrFormat(
            "Failed to remove footprint object for skill '%s' during "
            "Execution. Error: Clips Skill Dispatcher configured without an "
            "object world service.",
            skill_id),
        {.debug_message =
             absl::StrFormat("Internal action id: %s", action_id)});
    clips::ProtoMessageId es_proto_id = proto_mgr->AddGeneratedProto(es_proto);
    ReportClipsStatus(
        assert_facade, action_id, ClipsSkillDispatcher::kSkillStatusFailed,
        {.message = absl::StrFormat(
             "Failed to remove footprint object for '%s'", action_id),
         .extended_status_proto_id = es_proto_id});
    return absl::NotFoundError(
        "Clips Skill Dispatcher configured without an object world service.");
  }

  absl::Status status = RemoveFootprintVolumesFromWorld(
      *object_world_service_stub, world_id, footprint_object_name);
  if (!status.ok()) {
    absl::MutexLock clips_lock(*assert_facade->GetClipsMutex());
    intrinsic_proto::status::ExtendedStatus es_proto = CreateExtendedStatus(
        18101,
        absl::StrFormat(
            "Failed to remove footprint object for skill '%s' during "
            "Execution. Error: %s",
            skill_id, status.message()),
        {.debug_message =
             absl::StrFormat("Internal action id: %s", action_id)});
    clips::ProtoMessageId es_proto_id = proto_mgr->AddGeneratedProto(es_proto);
    ReportClipsStatus(
        assert_facade, action_id, ClipsSkillDispatcher::kSkillStatusFailed,
        {.message = absl::StrFormat(
             "Failed to remove footprint object for '%s'", action_id),
         .extended_status_proto_id = es_proto_id});
    return status;
  }

  return absl::OkStatus();
}

bool OverwritesSameWorldUpdates(
    const intrinsic_proto::world::ObjectWorldUpdates& previous,
    const intrinsic_proto::world::ObjectWorldUpdates& next) {
  if (!previous.entity_updates().empty() || !next.entity_updates().empty()) {
    return false;
  }
  if (previous.updates_size() != 1 || next.updates_size() != 1) {
    return false;
  }
  const intrinsic_proto::world::ObjectWorldUpdate& previous_update =
      previous.updates(0);
  const intrinsic_proto::world::ObjectWorldUpdate& next_update =
      next.updates(0);
  if (!previous_update.has_update_object_joints() ||
      !next_update.has_update_object_joints()) {
    return false;
  }

  const google::protobuf::FieldDescriptor* joint_positions_field =
      intrinsic_proto::world::UpdateObjectJointsRequest::GetDescriptor()
          ->FindFieldByName("joint_positions");
  if (!joint_positions_field) {
    return false;
  }
  google::protobuf::util::MessageDifferencer md;
  md.IgnoreField(joint_positions_field);
  bool same = md.Compare(previous_update.update_object_joints(),
                         next_update.update_object_joints());
  return same;
}

// TODO(b/275357655): Move to the visualization service
absl::Status VisualizePredictions(
    intrinsic_proto::world::ObjectWorldService::StubInterface*
        object_world_service_stub,
    intrinsic_proto::simulation::first_party::SimulationService::
        StubInterface* absl_nullable simulation_service,
    const intrinsic_proto::skills::Prediction& prediction, bool fast_preview) {
  const stats::ScopedSpan span("ClipsSkillDispatcher/VisualizePredictions");
  if (object_world_service_stub == nullptr) {
    return absl::NotFoundError(
        "Clips Skill Dispatcher configured without a world service.");
  }

  // Visualize the states.
  absl::Duration elapsed_time = absl::ZeroDuration();
  for (int i = 0; i < prediction.expected_states_size(); ++i) {
    const auto& state = prediction.expected_states(i);
    if (fast_preview) {
      // Skip all updates if the very next one would overwrite the same values.
      // No need to send that world update out additionally.
      if (i + 1 < prediction.expected_states_size()) {
        if (OverwritesSameWorldUpdates(
                prediction.expected_states(i).world_updates(),
                prediction.expected_states(i + 1).world_updates())) {
          continue;
        }
      }
    }
    INTR_ASSIGN_OR_RETURN(absl::Duration time_until_update,
                          ToAbslDuration(state.time_until_update()));
    absl::Duration update_duration = time_until_update - elapsed_time;

    if (!fast_preview && simulation_service != nullptr) {
      // Visualize interpolated updates in the belief world using the simulation
      // service.
      grpc::ClientContext ctx;
      intrinsic_proto::simulation::first_party::VisualizeWorldUpdatesRequest
          visualizer_request;
      visualizer_request.set_start_world_id("world");
      visualizer_request.set_visualized_world_id("world");
      *visualizer_request.mutable_world_updates() = state.world_updates();

      INTR_RETURN_IF_ERROR(FromAbslDuration(
          update_duration, visualizer_request.mutable_duration()));

      google::protobuf::Empty response;
      // `RunVisualization` takes at least `update_duration` time to complete.
      INTR_RETURN_IF_ERROR(ToAbslStatus(simulation_service->RunVisualization(
          &ctx, visualizer_request, &response)));
    } else {
      // Sleep for the update duration to ensure updates are applied at the
      // appropriate time to the world to ensure that the visualization is
      // smooth.
      if (!fast_preview) {
        absl::SleepFor(update_duration);
      }

      // Update the belief world at each point to mimic e.g. having the
      // world-updater running.
      {
        grpc::ClientContext ctx;
        intrinsic_proto::world::UpdateWorldResourcesRequest request;
        *request.mutable_world_updates() = state.world_updates();
        request.set_world_id("world");

        intrinsic_proto::world::UpdateWorldResourcesResponse response;
        INTR_RETURN_IF_ERROR(
            ToAbslStatus(object_world_service_stub->UpdateWorldResources(
                &ctx, request, &response)));
      }
    }

    elapsed_time = time_until_update;
  }

  return absl::OkStatus();
}

}  // namespace

ClipsSkillDispatcher::ClipsSkillDispatcher(
    clips::EnvironmentAssertFacade* assert_facade,
    clips::ProtobufManager* proto_manager,
    clips::TraceSpanManager* span_manager,
    SkillClientGenerator* skill_client_generator,
    intrinsic_proto::world::ObjectWorldService::StubInterface*
        object_world_service_stub,
    intrinsic_proto::simulation::first_party::SimulationService::StubInterface*
        simulation_service)
    : assert_facade_(assert_facade),
      proto_manager_(proto_manager),
      span_manager_(span_manager),
      skill_client_generator_(skill_client_generator),
      object_world_service_stub_(object_world_service_stub),
      simulation_service_(simulation_service) {}

absl::Status ClipsSkillDispatcher::Init(
    clips::EnvironmentFunctionFacade* function_facade,
    clips::EnvironmentReadonlyFacade* readonly_facade) {
  if (bundle_.has_value()) {
    return absl::FailedPreconditionError(
        "ClipsSkillsDispatcher::Init() must be called exactly once (and it "
        "already has been called before)");
  }

  // ensure that skill_info.clp has been loaded
  INTR_RETURN_IF_ERROR(readonly_facade->GetTemplate("skill-info").status());
  INTR_RETURN_IF_ERROR(readonly_facade->GetTemplate("skill-status").status());

  // Create a thread pool with 50 threads.
  bundle_.emplace(50);
  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "skill-start",
      std::function(
          [this](const std::string& action_id, const std::string& world_id,
                 int64_t behavior_call_proto_id, int64_t context_proto_id,
                 int64_t parent_span_reference_id, int64_t descriptor_pool_id,
                 const clips::Symbol& sim_mode) {
            GetAssertFacade()->GetClipsMutex()->AssertHeld();
            StartSkillExecution(
                action_id, world_id, sim_mode,
                clips::ProtoMessageId(behavior_call_proto_id),
                clips::ProtoMessageId(context_proto_id),
                clips::TraceSpanReferenceId(parent_span_reference_id),
                clips::DescriptorPoolId(descriptor_pool_id));
          })));
  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "skill-cancel-async",
      std::function(
          [this](const std::string& action_id, const clips::Symbol& sim_mode) {
            CancelSkillExecution(action_id, sim_mode);
          })));
  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "skill-abandon-call-async",
      std::function([this](const std::string& action_id) {
        AbandonSkillExecution(action_id);
      })));
  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "skill-project-async",
      std::function(
          [this](const std::string& action_id, const std::string& world_id,
                 int64_t behavior_call_proto_id, int64_t context_proto_id,
                 int64_t parent_span_reference_id, int64_t descriptor_pool_id) {
            GetAssertFacade()->GetClipsMutex()->AssertHeld();
            StartSkillProjection(
                action_id, world_id,
                clips::ProtoMessageId(behavior_call_proto_id),
                clips::ProtoMessageId(context_proto_id),
                clips::TraceSpanReferenceId(parent_span_reference_id),
                clips::DescriptorPoolId(descriptor_pool_id));
          })));
  INTR_RETURN_IF_ERROR(function_facade->AddFunction(
      "skill-reset-instance-ids", std::function([this]() -> clips::Values {
        INTR_RETURN_IF_ERROR(ResetSkillInstanceIds())
            .With([](const absl::Status& status) {
              return clips::Values{clips::Symbol::False(),
                                   clips::Value(status.message())};
            });
        return clips::Values{clips::Symbol::True(), clips::Value("")};
      })));

  return absl::OkStatus();
}

absl::Status ClipsSkillDispatcher::TearDown() {
  // Invokes the dtor for all threads and cancels prior to joining.
  bundle_.reset();
  return absl::OkStatus();
}

std::shared_ptr<opentelemetry::trace::Span>
ClipsSkillDispatcher::StartSkillSpan(
    absl::string_view skill_name,
    const opentelemetry::trace::SpanContext& parent_context,
    SkillAction skill_action) {
  std::string action_name = SkillActionNameVerb(skill_action);
  std::string span_name =
      absl::StrCat(action_name, " skill: '", skill_name, "'");

  if (!parent_context.IsValid()) {
    LOG(ERROR) << "StartSkillSpan for " << skill_name
               << " called with invalid parent_context.";
    return stats::StartSampledRootSpan(span_name);
  }

  std::shared_ptr<opentelemetry::trace::Span> span =
      stats::GetTracer()->StartSpan(span_name, {.parent = parent_context});
  span->SetAttribute("skill_name", skill_name);

  // only output in project, so that the printout appears once per skill
  // invocation
  if (skill_action == SkillAction::Projection) {
    LOG(INFO)
        << "Started tracing span for skill " << skill_name << " ("
        << action_name
        << "). Trace: https://console.cloud.google.com/traces/explorer;traceId="
        << stats::GetTraceIdHex(span->GetContext());
  }
  return span;
}

absl::StatusOr<std::optional<::google::protobuf::Any>>
ClipsSkillDispatcher::PerformSkillPreview(
    absl::string_view action_id, SkillInstance& skill_instance,
    const clips::Symbol& sim_mode, absl::string_view world_id,
    const intrinsic_proto::executive::BehaviorCall& behavior_call,
    const std::optional<absl::Duration>& execute_timeout,
    const intrinsic_proto::data_logger::Context& context) {
  {
    absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
    ReportClipsStatus(assert_facade_, action_id, kSkillStatusRunning);
  }
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::skills::Prediction prediction,
                        PreviewSkill(skill_instance, world_id, behavior_call,
                                     execute_timeout, context));

  std::optional<::google::protobuf::Any> result_any;
  if (prediction.has_result()) {
    result_any = prediction.result();
  }

  // Visualize the preview; this is potentially long-running.
  {
    // TODO(dornhege): I think we can lock this after VisualizePredictions as
    // that should only do world updates/gRPC calls. Otherwise we block any
    // parallel calls during VisualizePredictions.
    absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());

    LOG(INFO) << "Visualizing skill preview for " << behavior_call.skill_id()
              << ".";

    absl::Status visualization_status = VisualizePredictions(
        object_world_service_stub_, simulation_service_, prediction,
        /*fast_preview=*/sim_mode == clips::Symbol("FAST-PREVIEW"));

    if (!visualization_status.ok()) {
      return CreateStatus(
          18103,
          absl::StrFormat("Failed to visualize preview predictions for skill "
                          "'%s' during Execution. Error: %s",
                          behavior_call.skill_id(),
                          visualization_status.message()),
          absl::StatusCode::kAborted,
          {.debug_message =
               absl::StrFormat("Internal action id: %s", action_id)});
    }
  }

  return result_any;
}

// The skill framework has reported a timeout when waiting for the skill
// execution. In this case, we need to ask the skill to cancel and then wait for
// it to finish. Since we cannot force-cancel a skill, the only safe behavior we
// have is to wait (potentially indefinitely) for the skill to cancel/finish.
absl::StatusOr<intrinsic_proto::skills::ExecuteResult>
ClipsSkillDispatcher::PerformSkillExecuteTimeoutHandling(
    absl::string_view action_id, SkillInstance& skill_instance) {
  {
    absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
    ReportClipsStatus(assert_facade_, action_id,
                      kSkillStatusCancelingExecutionTimeout);
  }

  // The status of the cancel call, not of the skill itself.
  // The loop will retry sending the cancel request until it succeeds once.
  absl::Status cancel_status = absl::UnknownError("");

  // Wait (indefinitely) until the skill finishes. Do so in periods so that we
  // can keep logging.
  absl::Time start_wait_cancel = absl::Now();
  do {
    if (skill_instance.IsAbandoned()) {
      // Skill is abandoned. Do not go into another timeout loop.
      return absl::CancelledError("Skill was abandoned");
    }
    if (!cancel_status.ok()) {
      LOG(INFO) << "Trying to cancel skill "
                << skill_instance.GetInstanceProto().id_version()
                << " after timeout";
      cancel_status = skill_instance.GetClient()->TryCancelExecute();
      if (!cancel_status.ok()) {
        // Even if cancellation failed outright in the following we still need
        // to wait for the skill to finish to not have it "lingering around" in
        // an active state.
        LOG(WARNING) << "Failed to initiate timeout cancellation: "
                     << cancel_status.message();
      }
    }

    grpc::ClientContext wait_context;
    if (!skill_instance.UpdateWaitContextUnlessAbandoned(&wait_context)) {
      // Skill was abandoned in the mean time. Declare it finished without
      // waiting for a result
      return absl::CancelledError("Skill was abandoned");
    }

    absl::StatusOr<intrinsic_proto::skills::ExecuteResult> execute_result =
        skill_instance.GetClient()->WaitForExecuteWithContext(
            wait_context, absl::Seconds(30));
    skill_instance.UpdateWaitContext(nullptr);

    // Skill either succeeded or reported an error other than a timeout.
    if (!IsSkillTimeoutStatus(execute_result.status())) {
      return execute_result;
    }

    LOG(WARNING) << absl::StrFormat(
        "Waiting for skill '%s' to cancel after timeout (for %s)",
        skill_instance.GetInstanceProto().id_version(),
        absl::FormatDuration(absl::Now() - start_wait_cancel));

    // while true: Until the skill replies anything else than timeout, the only
    // safe behavior is to wait for the skill indefinitely if it doesn't stop.
  } while (true);
}

absl::StatusOr<std::optional<::google::protobuf::Any>>
ClipsSkillDispatcher::PerformSkillExecute(
    absl::string_view action_id, SkillInstance& skill_instance,
    absl::string_view world_id,
    const intrinsic_proto::executive::BehaviorCall& behavior_call,
    const std::optional<absl::Duration>& execute_timeout,
    const intrinsic_proto::data_logger::Context& context) {
  absl::StatusOr<
      std::unique_ptr<intrinsic::skills::SkillServiceClientInterface::
                          ExecuteClientContextInterface>>
      execute_context = skill_instance.GetClient()->StartExecute(
          world_id, behavior_call.skill_execution_data().footprint(),
          behavior_call.parameters(),
          context);
  {
    absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
    // It is important to report RUNNING only after the Start call has been run.
    // Otherwise a concurrent cancellation call could be called on the skill.
    ReportClipsStatus(assert_facade_, action_id, kSkillStatusRunning);
  }
  if (!execute_context.ok()) {
    return execute_context.status();
  }

  grpc::ClientContext wait_context;
  skill_instance.UpdateWaitContext(&wait_context);
  absl::StatusOr<intrinsic_proto::skills::ExecuteResult> execute_result =
      skill_instance.GetClient()->WaitForExecuteWithContext(wait_context,
                                                            execute_timeout);
  skill_instance.UpdateWaitContext(nullptr);

  if (skill_instance.IsAbandoned()) {
    return absl::CancelledError("Skill was abandoned");
  }

  if (IsSkillTimeoutStatus(execute_result.status())) {
    LOG(INFO) << "Skill service reported a skill timeout - canceling skill "
              << skill_instance.GetInstanceProto().id_version();
    execute_result =
        PerformSkillExecuteTimeoutHandling(action_id, skill_instance);
  }

  (*execute_context)->LogSummary(execute_result);

  if (!execute_result.ok()) {
    return execute_result.status();
  }

  std::optional<::google::protobuf::Any> result_any;
  if (execute_result->has_result()) {
    result_any = execute_result->result();
  }
  return result_any;
}

void ClipsSkillDispatcher::StartSkillExecution(
    absl::string_view action_id, absl::string_view world_id,
    const clips::Symbol& sim_mode, clips::ProtoMessageId behavior_call_proto_id,
    clips::ProtoMessageId context_proto_id,
    clips::TraceSpanReferenceId parent_span,
    clips::DescriptorPoolId descriptor_pool_id) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::executive::BehaviorCall behavior_call_proto,
      GetBehaviorCallProtoOrReportError(GetAssertFacade(), proto_manager_,
                                        action_id, SkillAction::Execution,
                                        behavior_call_proto_id),
      _.With(ReturnVoid()));

  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::data_logger::Context context_proto,
      GetContextProtoOrReportError(GetAssertFacade(), proto_manager_, action_id,
                                   behavior_call_proto.skill_id(),
                                   SkillAction::Execution, context_proto_id),
      _.With(ReturnVoid()));
  opentelemetry::trace::SpanContext parent_context =
      opentelemetry::trace::SpanContext::GetInvalid();
  absl::StatusOr<opentelemetry::trace::SpanContext> status_or_context =
      span_manager_->GetSpanContext(parent_span);
  if (!status_or_context.ok()) {
    LOG(ERROR) << "Failed to GetSpanContext " << status_or_context.status();
  } else {
    parent_context = *status_or_context;
  }

  if (auto status = bundle_->Schedule([this, action_id = std::string(action_id),
                                       world_id = std::string(world_id),
                                       sim_mode,
                                       original_behavior_call_proto =
                                           behavior_call_proto,
                                       descriptor_pool_id, context_proto,
                                       parent_context]() {
        intrinsic_proto::executive::BehaviorCall behavior_call_proto =
            original_behavior_call_proto;
        SkillAction skill_action;
        if (sim_mode == clips::Symbol("PREVIEW") ||
            sim_mode == clips::Symbol("FAST-PREVIEW")) {
          skill_action = SkillAction::PreviewExecution;
        } else {
          skill_action = SkillAction::Execution;
        }

        const stats::ScopedSpan span(StartSkillSpan(
            behavior_call_proto.skill_id(), parent_context, skill_action));

        // Get Skill info, also creates client, hence do within bundle
        INTR_ASSIGN_OR_RETURN(
            SkillInstance skill_instance_to_move,
            CreateSkillInstanceOrReportError(
                assert_facade_, proto_manager_, skill_client_generator_,
                action_id, SkillAction::Execution, behavior_call_proto,
                descriptor_pool_id),
            _.With(ReturnVoid()));

        std::shared_ptr<SkillInstance> skill_instance;
        {
          absl::MutexLock lock(action_id_to_skill_instance_mutex_);
          action_id_to_skill_instance_[action_id] =
              std::make_shared<SkillInstance>(
                  std::move(skill_instance_to_move));
          // This thread added the skill_instance to
          // action_id_to_skill_instance_ and it is the only place where it is
          // erased again. Other threads may use it (while locked) but never
          // erase it.
          skill_instance = action_id_to_skill_instance_.at(action_id);
        }

        INTR_ASSIGN_OR_RETURN(
            const std::string footprint_object_name,
            AddFootprintVolumesOrReportError(
                assert_facade_, proto_manager_, object_world_service_stub_,
                world_id, action_id, behavior_call_proto),
            _.With(ReturnVoid()));

        std::optional<absl::Duration> execute_timeout =
            GetExecuteTimeoutOrDefault(behavior_call_proto);

        skill_instance->GetClient()->InitConcurrentLogging();

        absl::Cleanup cleanup_skill_instance =
            [skill_instance, action_id = std::string(action_id), this] {
              // Retain the skill_instance shared_ptr here to be able to call
              // TearDownConcurrentLogging, but already remove the entry from
              // the map, so that concurrent threads already know that this
              // skill call has concluded.
              // TearDownConcurrentLogging must happen at the very end and
              // outside a lock as this sometimes can take significant time.
              {
                absl::MutexLock lock(action_id_to_skill_instance_mutex_);
                action_id_to_skill_instance_.erase(action_id);
              }

              const stats::ScopedSpan span("TearDownConcurrentLogging");
              skill_instance->GetClient()->TearDownConcurrentLogging();
            };

        // actual execution follows, this is potentially long-running
        absl::Status result_status;
        std::optional<::google::protobuf::Any> result_any;
        if (sim_mode == clips::Symbol("PREVIEW") ||
            sim_mode == clips::Symbol("FAST-PREVIEW")) {
          absl::StatusOr<std::optional<::google::protobuf::Any>>
              preview_result = PerformSkillPreview(
                  action_id, *skill_instance, sim_mode, world_id,
                  behavior_call_proto, execute_timeout, context_proto);
          result_status = preview_result.status();
          if (preview_result.ok()) {
            result_any = *preview_result;
          }
        } else {
          absl::StatusOr<std::optional<::google::protobuf::Any>>
              execute_result = PerformSkillExecute(
                  action_id, *skill_instance, world_id, behavior_call_proto,
                  execute_timeout, context_proto);
          result_status = execute_result.status();
          if (execute_result.ok()) {
            result_any = *execute_result;
          }
        }

        INTR_RETURN_IF_ERROR(RemoveFootprintVolumesOrReportError(
                                 assert_facade_, proto_manager_,
                                 object_world_service_stub_, world_id,
                                 action_id, behavior_call_proto.skill_id(),
                                 footprint_object_name))
            .With(ReturnVoid());

        // Skill call done. Report its outcome (and possible result)
        {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());

          if (skill_instance->IsAbandoned()) {
            std::string skill_id =
                skill_instance->GetInstanceProto().id_version();
            absl::StatusOr<std::string> skill_id_or =
                assets::RemoveVersionFrom(skill_id);
            if (skill_id_or.ok()) {
              skill_id = *skill_id_or;
            }
            intrinsic_proto::status::ExtendedStatus skill_es =
                CreateExtendedStatus(
                    18003,
                    absl::StrFormat("Call was abandoned. The skill '%s' is "
                                    "in an unknown state.",
                                    skill_id),
                    {.severity =
                         intrinsic_proto::status::ExtendedStatus::ERROR});
            clips::ProtoMessageId es_proto_id =
                proto_manager_->AddGeneratedProto(skill_es);
            ReportClipsStatus(assert_facade_, action_id, kSkillStatusFailed,
                              {.message = result_status.ToString(
                                   absl::StatusToStringMode::kWithNoExtraData),
                               .extended_status_proto_id = es_proto_id});
            return;
          }

          if (!result_status.ok()) {
            if (result_status.code() == absl::StatusCode::kCancelled) {
              ReportClipsStatus(assert_facade_, action_id,
                                kSkillStatusCanceled);
              return;
            }

            // Skill failed
            intrinsic_proto::status::ExtendedStatus skill_es =
                GetExtendedStatusForSkillResult(
                    result_status,
                    skill_instance->GetInstanceProto().id_version(),
                    SkillAction::Execution);
            clips::ProtoMessageId es_proto_id =
                proto_manager_->AddGeneratedProto(skill_es);
            ReportClipsStatus(assert_facade_, action_id, kSkillStatusFailed,
                              {.message = result_status.ToString(
                                   absl::StatusToStringMode::kWithNoExtraData),
                               .extended_status_proto_id = es_proto_id});
            return;
          }

          clips::ProtoMessageId result_proto_id =
              clips::ProtobufManager::kInvalidId;
          if (result_any.has_value()) {
            result_proto_id = proto_manager_->AddGeneratedProto(*result_any);
          }
          ReportClipsStatus(assert_facade_, action_id, kSkillStatusSucceeded,
                            {.result_proto_id = result_proto_id});
        }
      });
      !status.ok()) {
    intrinsic_proto::status::ExtendedStatus extended_status =
        CreateExtendedStatus(
            18102,
            absl::StrFormat("Failed to schedule skill execution for skill "
                            "'%s'. Error: %s",
                            behavior_call_proto.skill_id(), status.message()),
            {.debug_message =
                 absl::StrFormat("Internal action id: %s", action_id)});
    ReportClipsStatus(GetAssertFacade(), action_id, kSkillStatusFailed,
                      {.message = "Failed to enqueue skill execution",
                       .extended_status_proto_id =
                           proto_manager_->AddGeneratedProto(extended_status)});
  }
}

void ClipsSkillDispatcher::CancelSkillExecution(const std::string& action_id,
                                                const clips::Symbol& sim_mode) {
  absl::MutexLock lock(action_id_to_skill_instance_mutex_);
  auto itr = action_id_to_skill_instance_.find(action_id);
  if (itr == action_id_to_skill_instance_.end()) {
    LOG(ERROR) << "Cannot find SkillInstance for action '" << action_id
               << "'; cancellation will not happen.";
    return;
  }

  std::shared_ptr<SkillInstance> skill_instance = itr->second;
  LOG(INFO) << "Trying to cancel action '" << action_id << "'.";
  INTR_RETURN_IF_ERROR(
      bundle_->Schedule([this, action_id, sim_mode, skill_instance] {
        if (sim_mode == clips::Symbol("PREVIEW") ||
            sim_mode == clips::Symbol("FAST-PREVIEW")) {
          INTR_RETURN_IF_ERROR(skill_instance->GetClient()->TryCancelPreview())
              .LogError()
              .With(ReturnVoid());
        } else {
          INTR_RETURN_IF_ERROR(skill_instance->GetClient()->TryCancelExecute())
              .LogError()
              .With(ReturnVoid());
        }

        {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          ReportClipsStatus(assert_facade_, action_id, kSkillStatusCanceling);
        }
      }))
      .LogError()
      .With(ReturnVoid());
}

void ClipsSkillDispatcher::AbandonSkillExecution(const std::string& action_id) {
  absl::MutexLock lock(action_id_to_skill_instance_mutex_);
  auto itr = action_id_to_skill_instance_.find(action_id);
  if (itr == action_id_to_skill_instance_.end()) {
    LOG(ERROR) << "Cannot find SkillInstance for action '" << action_id
               << "'; abandoning will not happen.";
    return;
  }

  std::shared_ptr<SkillInstance> skill_instance = itr->second;
  LOG(INFO) << "Trying to abandon call for action '" << action_id << "'.";

  INTR_RETURN_IF_ERROR(
      bundle_->Schedule([skill_instance] { skill_instance->AbandonCall(); }))
      .LogError()
      .With(ReturnVoid());
}

absl::StatusOr<intrinsic_proto::skills::Prediction>
ClipsSkillDispatcher::PreviewSkill(
    SkillInstance& skill_instance, absl::string_view world_id,
    const intrinsic_proto::executive::BehaviorCall& behavior_call_proto,
    std::optional<absl::Duration> execute_timeout,
    const intrinsic_proto::data_logger::Context& context_proto) {
  // Preview the skill execution.
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::skills::PreviewResult preview_result,
      skill_instance.GetClient()->Preview(
          world_id, behavior_call_proto.skill_execution_data().footprint(),
          behavior_call_proto.parameters(),
          execute_timeout, context_proto));

  // Get the preview result and corresponding prediction.
  intrinsic_proto::skills::Prediction prediction;
  prediction.set_probability(1.0);
  if (preview_result.has_result()) {
    *prediction.mutable_result() = preview_result.result();
  }
  prediction.mutable_expected_states()->Assign(
      preview_result.expected_states().begin(),
      preview_result.expected_states().end());

  return prediction;
}

void ClipsSkillDispatcher::StartSkillProjection(
    absl::string_view action_id, absl::string_view world_id,
    clips::ProtoMessageId behavior_call_proto_id,
    clips::ProtoMessageId context_proto_id,
    clips::TraceSpanReferenceId parent_span,
    clips::DescriptorPoolId descriptor_pool_id) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::executive::BehaviorCall behavior_call_proto,
      GetBehaviorCallProtoOrReportError(GetAssertFacade(), proto_manager_,
                                        action_id, SkillAction::Projection,
                                        behavior_call_proto_id),
      _.With(ReturnVoid()));

  INTR_ASSIGN_OR_RETURN(
      const intrinsic_proto::data_logger::Context context_proto,
      GetContextProtoOrReportError(GetAssertFacade(), proto_manager_, action_id,
                                   behavior_call_proto.skill_id(),
                                   SkillAction::Projection, context_proto_id),
      _.With(ReturnVoid()));

  opentelemetry::trace::SpanContext parent_context =
      opentelemetry::trace::SpanContext::GetInvalid();
  absl::StatusOr<opentelemetry::trace::SpanContext> status_or_context =
      span_manager_->GetSpanContext(parent_span);
  if (!status_or_context.ok()) {
    LOG(ERROR) << "Failed to GetSpanContext " << status_or_context.status();
  } else {
    parent_context = *status_or_context;
  }

  if (auto status = bundle_->Schedule([this, action_id = std::string(action_id),
                                       world_id = std::string(world_id),
                                       behavior_call_proto_id,
                                       original_behavior_call_proto =
                                           behavior_call_proto,
                                       context_proto, parent_context,
                                       descriptor_pool_id]() {
        intrinsic_proto::executive::BehaviorCall behavior_call_proto =
            original_behavior_call_proto;
        const stats::ScopedSpan span(
            StartSkillSpan(behavior_call_proto.skill_id(), parent_context,
                           SkillAction::Projection));

        // Get Skill info, also creates client, hence do within bundle
        INTR_ASSIGN_OR_RETURN(
            SkillInstance skill_instance,
            CreateSkillInstanceOrReportError(
                assert_facade_, proto_manager_, skill_client_generator_,
                action_id, SkillAction::Projection, behavior_call_proto,
                descriptor_pool_id),
            _.With(ReturnVoid()));

        std::optional<absl::Duration> project_timeout =
            GetProjectTimeoutOrDefault(behavior_call_proto);

        skill_instance.GetClient()->InitConcurrentLogging();
        absl::Cleanup cleanup_conclog = [&skill_instance] {
          skill_instance.GetClient()->TearDownConcurrentLogging();
        };

        LOG(INFO) << "Calling skill " << behavior_call_proto.skill_id()
                  << " (Projecting)";
        // actual get footprint call, this is potentially long-running
        absl::StatusOr<intrinsic_proto::skills::GetFootprintResult>
            footprint_result = skill_instance.GetClient()->GetFootprint(
                world_id, behavior_call_proto.parameters(),
                project_timeout, context_proto);

        {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          if (!footprint_result.ok()) {
            intrinsic_proto::status::ExtendedStatus skill_es =
                GetExtendedStatusForSkillResult(
                    footprint_result.status(),
                    skill_instance.GetInstanceProto().id_version(),
                    SkillAction::FootprintChecking);
            clips::ProtoMessageId es_proto_id =
                proto_manager_->AddGeneratedProto(skill_es);

            // Assert fact to indicate success (if unimplemented) or
            // failure
            ReportClipsStatus(assert_facade_, action_id, kSkillStatusFailed,
                              {.message = footprint_result.status().ToString(),
                               .extended_status_proto_id = es_proto_id});
            return;
          }

          if (absl::Status status = ValidateFootprintVolumeReservations(
                  footprint_result->footprint(),
                  behavior_call_proto.skill_id());
              !status.ok()) {
            intrinsic_proto::status::ExtendedStatus es_proto =
                CreateExtendedStatus(
                    18101, status.message(),
                    {.debug_message =
                         absl::StrFormat("Internal action id: %s", action_id)});
            clips::ProtoMessageId es_proto_id =
                proto_manager_->AddGeneratedProto(es_proto);
            ReportClipsStatus(assert_facade_, action_id, kSkillStatusFailed,
                              {.message = status.ToString(),
                               .extended_status_proto_id = es_proto_id});
            return;
          }

          clips::ProtoMessageId footprint_proto_id =
              proto_manager_->AddGeneratedProto(footprint_result->footprint());
          clips::ScopedProto footprint_proto(footprint_proto_id,
                                             proto_manager_);
          if (absl::Status s = proto_manager_->SetFieldValue(
                  behavior_call_proto_id, "skill_execution_data.footprint",
                  clips::Value(footprint_proto_id.value()));
              !s.ok()) {
            intrinsic_proto::status::ExtendedStatus es_proto =
                CreateExtendedStatus(
                    18104,
                    absl::StrFormat("Failed to record footprint for skill "
                                    "'%s' during projection. Error: %s",
                                    behavior_call_proto.skill_id(),
                                    s.message()),
                    {.debug_message =
                         absl::StrFormat("Internal action id: %s", action_id)});
            clips::ProtoMessageId es_proto_id =
                proto_manager_->AddGeneratedProto(es_proto);
            ReportClipsStatus(assert_facade_, action_id, kSkillStatusFailed,
                              {.message = s.ToString(),
                               .extended_status_proto_id = es_proto_id});
            return;
          }

          ReportClipsStatus(assert_facade_, action_id, kSkillStatusProjected);
        }
      });
      !status.ok()) {
    intrinsic_proto::status::ExtendedStatus extended_status =
        CreateExtendedStatus(
            18102,
            absl::StrFormat("Failed to schedule skill projection for skill "
                            "'%s'. Error: %s",
                            behavior_call_proto.skill_id(), status.message()),
            {.debug_message =
                 absl::StrFormat("Internal action id: %s", action_id)});
    ReportClipsStatus(
        GetAssertFacade(), action_id, kSkillStatusFailed,
        {.message = absl::StrFormat(
             "Failed to schedule skill projection for action '%s'.", action_id),
         .extended_status_proto_id =
             proto_manager_->AddGeneratedProto(extended_status)});
  }
}

absl::Status ValidateFootprintVolumeReservations(const Footprint& footprint,
                                                 absl::string_view skill_id) {
  for (const VolumeReservation& volume_reservation : footprint.volume()) {
    if (volume_reservation.transformed_geometry()
            .geometry()
            .has_inline_geometry_data()) {
      const ExactGeometry& exact_geo = volume_reservation.transformed_geometry()
                                           .geometry()
                                           .inline_geometry_data()
                                           .exact_geometry();
      if (exact_geo.data_case() != ExactGeometry::kPrimitiveSet) {
        const google::protobuf::OneofDescriptor* oneof_desc =
            exact_geo.GetDescriptor()->FindOneofByName("data");
        const google::protobuf::FieldDescriptor* field_desc =
            oneof_desc != nullptr
                ? exact_geo.GetReflection()->GetOneofFieldDescriptor(exact_geo,
                                                                     oneof_desc)
                : nullptr;
        absl::string_view geometry_type =
            (field_desc != nullptr) ? field_desc->name() : "none";
        return absl::InvalidArgumentError(absl::StrFormat(
            "Footprint volume reservation for skill '%s' contains inline "
            "geometry '%s'. TransformedGeometry for footprint volume "
            "reservation only supports primitives when inline geometry is "
            "provided, not meshes or point clouds.",
            skill_id, geometry_type));
      }
    }
  }
  return absl::OkStatus();
}

absl::Status ClipsSkillDispatcher::ResetSkillInstanceIds() {
  return skill_client_generator_->ResetSkillInstances();
}

void ClipsSkillDispatcher::WaitForFinishAll() {
  // We are not clearing the bundle here, since semantically we just want
  // to wait for all threads to finish.
  bundle_.reset();
}

}  // namespace executive
}  // namespace intrinsic
