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

#include "intrinsic/executive/engine/clips_world.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/client_context.h"
#include "intrinsic/executive/clips_cpp/assert_facade.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/any_list.pb.h"
#include "intrinsic/executive/proto/world_query.pb.h"
#include "intrinsic/geometry/proto/v1/transformed_geometry.pb.h"
#include "intrinsic/icon/release/grpc_time_support.h"  // for grpc::TimePoint<absl::Time>
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/skills/internal/conflicts.h"
#include "intrinsic/skills/proto/footprint.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/geometry_component.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/service/updater/world_updater.grpc.pb.h"
#include "intrinsic/world/service/updater/world_updater.pb.h"
#include "intrinsic/world/service/world_compatibility_service.grpc.pb.h"
#include "intrinsic/world/service/world_compatibility_service.pb.h"
#include "intrinsic/world/util/remove_redundant_updates.h"
#include "intrinsic/world/world.pb.h"
#include "opentelemetry/trace/span.h"
#include "re2/re2.h"

namespace intrinsic {
namespace executive {
namespace {

using ::intrinsic::ConfigureClientContext;
using ::intrinsic::Pose3d;
using ::intrinsic::RootObjectId;
using ::intrinsic::toAffineMatrix4d;
using ::intrinsic::ToProto;
using ::intrinsic::eigenmath::Matrix4d;
using ::intrinsic::eigenmath::MatrixXd;
using ::intrinsic_proto::FromProto;
using ::intrinsic_proto::Matrixd;
using ::intrinsic_proto::Pose;
using ::intrinsic_proto::geometry::GeometryStorageRefs;
using ::intrinsic_proto::geometry::TransformedGeometryStorageRefs;
using ::intrinsic_proto::scene_object::v1::Entity;
using ::intrinsic_proto::scene_object::v1::SceneObject;
using ::intrinsic_proto::skills::Footprint;
using ::intrinsic_proto::skills::VolumeReservation;
using ::intrinsic_proto::world::CreateObjectRequest;
using ::intrinsic_proto::world::DeleteObjectRequest;
using ::intrinsic_proto::world::GeometryComponent;
using ::intrinsic_proto::world::Object;
using ::intrinsic_proto::world::ObjectReferenceWithEntityFilter;
using ::intrinsic_proto::world::ObjectWorldService;

constexpr char kObjectWorldDownload[] = "world-download-object-world";
constexpr char kWorldClone[] = "world-clone";
constexpr char kWorldDelete[] = "world-delete";
constexpr char kWorldPauseUpdater[] = "world-updater-pause";
constexpr char kWorldResumeUpdater[] = "world-updater-resume";
constexpr char kCheckFootprint[] = "world-footprint-conflict";
constexpr char kWorldQuery[] = "world-query";

constexpr absl::Duration kWorldUpdaterTimeout = absl::Seconds(30);

template <typename M, typename = std::enable_if_t<
                          std::is_base_of_v<google::protobuf::Message, M>>>
intrinsic_proto::executive::AnyList ConvertIdNameToAnyList(
    const std::vector<intrinsic_proto::world::IdAndName>& id_name_list) {
  intrinsic_proto::executive::AnyList any_list;
  for (const intrinsic_proto::world::IdAndName& id_name : id_name_list) {
    M ref;
    ref.set_id(id_name.id());
    any_list.add_items()->PackFrom(ref);
  }
  return any_list;
}

intrinsic_proto::executive::AnyList ExtractNamedJointConfigurations(
    const intrinsic_proto::world::KinematicObjectComponent&
        kinematic_object_component,
    absl::Span<const intrinsic_proto::world::IdAndName> id_name_list) {
  intrinsic_proto::executive::AnyList any_list;
  absl::flat_hash_map<std::string,
                      intrinsic_proto::world::NamedJointConfiguration>
      joint_configs;
  for (const intrinsic_proto::world::NamedJointConfiguration& joint_config :
       kinematic_object_component.named_joint_configurations()) {
    joint_configs[joint_config.name()] = joint_config;
  }
  for (const intrinsic_proto::world::IdAndName& id_name : id_name_list) {
    if (joint_configs.contains(id_name.name())) {
      any_list.add_items()->PackFrom(joint_configs[id_name.name()]);
    }
  }
  return any_list;
}

absl::Status FilterIdNameListByNameRegEx(
    std::vector<intrinsic_proto::world::IdAndName>& id_name_list,
    std::string_view name_regex) {
  RE2 re(name_regex);
  if (!re.ok()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Invalid name filter regex: %s (error fragment '%s')",
                        re.error(), re.error_arg()));
  }
  id_name_list.erase(
      std::remove_if(id_name_list.begin(), id_name_list.end(),
                     [&re](const intrinsic_proto::world::IdAndName& id_name) {
                       return !RE2::FullMatch(id_name.name(), re);
                     }),
      id_name_list.end());
  return absl::OkStatus();
}

absl::Status SortIdNameListByName(
    std::vector<intrinsic_proto::world::IdAndName>& id_name_list,
    intrinsic_proto::executive::WorldQuery::Order::Direction direction) {
  switch (direction) {
    case intrinsic_proto::executive::WorldQuery::Order::ASCENDING:
      absl::c_sort(id_name_list,
                   [](const intrinsic_proto::world::IdAndName& a,
                      const intrinsic_proto::world::IdAndName& b) {
                     return a.name() < b.name();
                   });
      break;
    case intrinsic_proto::executive::WorldQuery::Order::DESCENDING:
      absl::c_sort(id_name_list,
                   [](const intrinsic_proto::world::IdAndName& a,
                      const intrinsic_proto::world::IdAndName& b) {
                     return a.name() > b.name();
                   });
      break;
    default:
      return absl::InvalidArgumentError("Unknown ordering direction");
  }
  return absl::OkStatus();
}

}  // namespace

ClipsWorld::ClipsWorld(
    clips::EnvironmentAssertFacade* assert_facade,
    clips::ProtobufManager* proto_manager,
    clips::TraceSpanManager* span_manager,
    intrinsic_proto::world::ObjectWorldService::StubInterface* object_stub,
    intrinsic_proto::world::WorldUpdater::StubInterface* world_updater_stub,
    intrinsic_proto::world::WorldCompatibilityService::StubInterface*
        compatibility_stub)
    : assert_facade_(assert_facade),
      proto_manager_(proto_manager),
      span_manager_(span_manager),
      object_world_service_stub_(object_stub),
      world_updater_stub_(world_updater_stub),
      world_compatibility_service_stub_(compatibility_stub) {}

absl::Status ClipsWorld::Init(clips::EnvironmentFunctionFacade* facade) {
  // Create a thread pool with 10 threads.
  bundle_.emplace(10);

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kObjectWorldDownload,
      std::function([this](const std::string& world_id) -> int64_t {
        INTR_ASSIGN_OR_RETURN(
            clips::ProtoMessageId proto_id, DownloadObjectWorld(world_id),
            (_.LogError() << "Failed to download object world '" << world_id
                          << "'")
                .With(Return(clips::ProtobufManager::kInvalidId.value())));
        return proto_id.value();
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kWorldClone,
      std::function([this](const std::string& from_world_id,
                           const std::string& clone_source,
                           int64_t trace_parent_span_id) -> std::string {
        INTR_ASSIGN_OR_RETURN(
            std::string cloned_world_id,
            CloneWorld(from_world_id, clone_source,
                       clips::TraceSpanReferenceId(trace_parent_span_id)),
            (_.LogError() << "Failed to clone world '" << from_world_id << "'")
                .With(Return("")));
        return cloned_world_id;
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kWorldDelete,
      std::function([this](const std::string& world_id) -> clips::Symbol {
        INTR_RETURN_IF_ERROR(DeleteWorld(world_id))
            .LogWarning()
            .With(Return(clips::Symbol::False()));
        return clips::Symbol::True();
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kWorldPauseUpdater, std::function([this]() -> clips::Symbol {
        INTR_RETURN_IF_ERROR(PauseWorldUpdater())
            .LogWarning()
            .With(Return(clips::Symbol::False()));
        return clips::Symbol::True();
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kWorldResumeUpdater, std::function([this]() -> clips::Symbol {
        INTR_RETURN_IF_ERROR(ResumeWorldUpdater())
            .LogWarning()
            .With(Return(clips::Symbol::False()));
        return clips::Symbol::True();
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kCheckFootprint,
      std::function([this](const std::string& world_id,
                           const int64_t footprint_proto_id,
                           const std::string action_uid,
                           const clips::Values& running_action_uids,
                           const clips::Values& running_footprints) {
        CheckFootprintForConflict(
            world_id, clips::ProtoMessageId(footprint_proto_id), action_uid,
            running_action_uids, running_footprints);
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kWorldQuery,
      std::function([this](const std::string& world_id,
                           const int64_t world_query_proto_id) -> int64_t {
        INTR_ASSIGN_OR_RETURN(
            clips::ProtoMessageId result_proto_id,
            Query(world_id, clips::ProtoMessageId(world_query_proto_id)),
            (_.LogError() << "Failed to query from world '" << world_id << "'")
                .With(Return(0)));
        return result_proto_id.value();
      })));

  return absl::OkStatus();
}

absl::Status ClipsWorld::TearDown() {
  // Invokes the dtor for all threads and calls cancel prior to joining.
  bundle_.reset();
  return absl::OkStatus();
}

absl::StatusOr<clips::ProtoMessageId> ClipsWorld::DownloadObjectWorld(
    const std::string& world_id) {
  grpc::ClientContext context;
  intrinsic::ConfigureClientContext(&context);
  intrinsic_proto::world::GetWorldWithEntitiesRequest request;
  request.set_world_id(world_id);
  intrinsic_proto::world::WorldWithEntities response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(world_compatibility_service_stub_->GetWorldWithEntities(
          &context, request, &response)));
  return proto_manager_->AddGeneratedProto(response);
}

absl::StatusOr<std::string> ClipsWorld::CloneWorld(
    absl::string_view from_world_id, absl::string_view clone_source,
    clips::TraceSpanReferenceId trace_parent_span_id) {
  if (from_world_id.empty()) {
    return absl::InvalidArgumentError("Cannot clone world without a world id");
  }

  absl::StatusOr<std::shared_ptr<opentelemetry::trace::Span>> parent_span =
      span_manager_->GetSpanForParenting(trace_parent_span_id);
  std::optional<intrinsic::stats::ScopedSpan> clone_span;
  if (parent_span.ok()) {
    clone_span.emplace(absl::StrCat("Clone World ", clone_source),
                       *parent_span);
  }

  grpc::ClientContext context;
  intrinsic::ConfigureClientContext(&context);
  intrinsic_proto::world::CloneWorldRequest request;
  request.set_world_id(from_world_id);
  std::string cloned_world_hint = "executive_cb";
  if (!clone_source.empty()) {
    absl::StrAppend(&cloned_world_hint, "_", clone_source);
  }
  request.set_cloned_world_hint(cloned_world_hint);
  intrinsic_proto::world::WorldMetadata response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_stub_->CloneWorld(&context, request, &response)));
  return response.id();
}

absl::Status ClipsWorld::DeleteWorld(const std::string& world_id) {
  grpc::ClientContext context;
  intrinsic::ConfigureClientContext(&context);
  intrinsic_proto::world::DeleteWorldRequest request;
  request.set_world_id(world_id);
  google::protobuf::Empty response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_stub_->DeleteWorld(&context, request, &response)));
  return absl::OkStatus();
}

absl::Status ClipsWorld::PauseWorldUpdater() {
  if (world_updater_stub_ == nullptr) {
    return absl::FailedPreconditionError(
        "WorldUpdater stub to pause not initialized");
  }
  grpc::ClientContext context;
  intrinsic::ConfigureClientContext(&context);
  context.set_deadline(absl::Now() + kWorldUpdaterTimeout);
  intrinsic_proto::world::PauseUpdaterRequest request;
  intrinsic_proto::world::PauseUpdaterResponse response;
  LOG(INFO) << "Pausing world updater...";
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(world_updater_stub_->Pause(&context, request, &response)));
  LOG(INFO) << "World updater paused.";
  return absl::OkStatus();
}

absl::Status ClipsWorld::ResumeWorldUpdater() {
  if (world_updater_stub_ == nullptr) {
    return absl::FailedPreconditionError(
        "WorldUpdater stub to resume not initialized");
  }
  grpc::ClientContext context;
  intrinsic::ConfigureClientContext(&context);
  context.set_deadline(absl::Now() + kWorldUpdaterTimeout);
  intrinsic_proto::world::ResumeUpdaterRequest request;
  intrinsic_proto::world::ResumeUpdaterResponse response;
  LOG(INFO) << "Resuming world updater...";
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(world_updater_stub_->Resume(&context, request, &response)));
  LOG(INFO) << "World updater resumed.";
  return absl::OkStatus();
}

void ClipsWorld::CheckFootprintForConflict(
    absl::string_view world_id, const clips::ProtoMessageId footprint_proto_id,
    absl::string_view action_uid, const clips::Values& running_action_uids,
    const clips::Values& running_footprints)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(assert_facade_->GetClipsMutex()) {
  std::vector<intrinsic_proto::skills::Footprint> left_footprints;
  std::vector<intrinsic_proto::skills::Footprint> right_footprints;
  // Add current footprint to compare
  INTR_ASSIGN_OR_RETURN(
      auto footprint_proto,
      proto_manager_->GetProtoAs<intrinsic_proto::skills::Footprint>(
          footprint_proto_id),
      _.With(ReturnVoid()));
  left_footprints.push_back(*footprint_proto);
  // Add running + ready footprints
  for (const clips::Value& footprint_proto_id : running_footprints) {
    absl::StatusOr<int64_t> footprint_value = footprint_proto_id.GetInteger();
    if (!footprint_value.ok()) {
      LOG(WARNING) << "Failed to get value from footprint action proto id.";
      continue;
    }
    INTR_ASSIGN_OR_RETURN(
        auto footprint_proto,
        proto_manager_->GetProtoAs<intrinsic_proto::skills::Footprint>(
            clips::ProtoMessageId(footprint_value.value())),
        _.With(ReturnVoid()));
    right_footprints.push_back(*footprint_proto);
  }

  if (auto status = bundle_->Schedule([this, left_footprints, right_footprints,
                                       world_id = std::string(world_id),
                                       action_uid = std::string(action_uid),
                                       running_action_uids,
                                       running_footprints] {
        grpc::ClientContext context;
        intrinsic::ConfigureClientContext(&context);
        auto const kNoConflict = clips::Symbol("NO-CONFLICT");
        auto const kFootprintConflict = clips::Symbol("FOOTPRINT-CONFLICT");
        auto const kSkillStatusFailed = clips::Symbol("FAILED");
        auto const kSkillFailureClassPreparation = clips::Symbol("PREPARATION");

        auto error_handler = [&, action_uid](const absl::Status& status) {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          intrinsic_proto::status::ExtendedStatus es_proto =
              CreateExtendedStatus(
                  18200, absl::StrFormat("Failed to call footprint conflict "
                                         "check service. Error: %s",
                                         status.message()));
          clips::ProtoMessageId es_proto_id =
              proto_manager_->AddGeneratedProto(es_proto);
          INTR_RETURN_IF_ERROR(
              assert_facade_
                  ->AssertFact(
                      "skill-status",
                      {{"action-id", clips::Symbol(action_uid)},
                       {"status", clips::Symbol(kSkillStatusFailed)},
                       {"message", status.message()},
                       {"extended-status-proto-id", es_proto_id.value()}})
                  .status())
              .LogWarning()
              .With(intrinsic::ExtraMessage()
                    << "Failed to report status to CLIPS")
              .With(ReturnVoid());
        };

        if (running_footprints.size() != running_action_uids.size()) {
          error_handler(absl::InternalError(absl::StrFormat(
              "Non-matching conflict inputs. %d running footprints <> %d "
              "running actions",
              running_footprints.size(), running_action_uids.size())));
          return;
        }

        // The footprints contain either object or entity world resources.
        // ObjectWorldService.AreFootprintsCompatible can handle both.
        INTR_ASSIGN_OR_RETURN(
            std::vector<ConflictIndices> conflicting_footprints,
            CheckConflicts(left_footprints, right_footprints, world_id,
                           *object_world_service_stub_),
            _.With(error_handler));

        clips::Symbol clips_status = kNoConflict;
        clips::Values conflict_ids;
        clips::Values conflict_action_uids;
        if (!conflicting_footprints.empty()) {
          clips_status = kFootprintConflict;
          for (const ConflictIndices& conflict_pair : conflicting_footprints) {
            conflict_ids.push_back(
                running_footprints.at(conflict_pair.right_footprint_index));
            conflict_action_uids.push_back(
                running_action_uids.at(conflict_pair.right_footprint_index));
          }
        }

        {
          absl::MutexLock clips_lock(*assert_facade_->GetClipsMutex());
          INTR_RETURN_IF_ERROR(
              assert_facade_
                  ->AssertFact(
                      "world-update",
                      {{"action-uid", clips::Symbol(action_uid)},
                       {"status", clips_status},
                       {"footprint-conflict-proto-ids", conflict_ids},
                       {"conflicting-action-uids", conflict_action_uids}})
                  .status())
              .LogWarning()
              .With(intrinsic::ExtraMessage()
                    << "Failed to report status to CLIPS")
              .With(ReturnVoid());
          assert_facade_->NotifyRunner();
        }
      });
      !status.ok()) {
    LOG(ERROR) << "Failed to check footprint for conflict: " << status;
  }
}

absl::StatusOr<clips::ProtoMessageId> ClipsWorld::Query(
    absl::string_view world_id, clips::ProtoMessageId world_query_proto_id) {
  INTR_ASSIGN_OR_RETURN(
      auto world_query,
      proto_manager_->GetProtoAs<intrinsic_proto::executive::WorldQuery>(
          world_query_proto_id));

  intrinsic_proto::world::GetObjectRequest get_object_request;
  get_object_request.set_world_id(world_id);

  switch (world_query->select().query_type_case()) {
    case intrinsic_proto::executive::WorldQuery::Query::kChildFramesOf:
      *get_object_request.mutable_object() =
          world_query->select().child_frames_of();
      break;
    case intrinsic_proto::executive::WorldQuery::Query::kChildObjectsOf:
      *get_object_request.mutable_object() =
          world_query->select().child_objects_of();
      break;
    case intrinsic_proto::executive::WorldQuery::Query::kChildrenOf:
      *get_object_request.mutable_object() =
          world_query->select().children_of();
      break;
    case intrinsic_proto::executive::WorldQuery::Query::kJointConfigurationsOf:
      *get_object_request.mutable_object() =
          world_query->select().joint_configurations_of();
      get_object_request.set_view(::intrinsic_proto::world::FULL);
      break;
    default:
      return absl::InvalidArgumentError("No query type set");
  }

  intrinsic_proto::world::Object object;

  grpc::ClientContext get_object_context;
  intrinsic::ConfigureClientContext(&get_object_context);
  INTR_RETURN_IF_ERROR(ToAbslStatus(object_world_service_stub_->GetObject(
      &get_object_context, get_object_request, &object)));

  std::vector<intrinsic_proto::world::IdAndName> id_name_list;

  switch (world_query->select().query_type_case()) {
    case intrinsic_proto::executive::WorldQuery::Query::kChildFramesOf: {
      for (const intrinsic_proto::world::Frame& frame : object.frames()) {
        intrinsic_proto::world::IdAndName id_name;
        id_name.set_id(frame.id());
        id_name.set_name(frame.name());
        id_name_list.emplace_back(std::move(id_name));
      }
      break;
    }
    case intrinsic_proto::executive::WorldQuery::Query::kChildObjectsOf: {
      for (const intrinsic_proto::world::IdAndName& object :
           object.children()) {
        id_name_list.push_back(object);
      }
      break;
    }
    case intrinsic_proto::executive::WorldQuery::Query::kChildrenOf: {
      for (const intrinsic_proto::world::Frame& frame : object.frames()) {
        intrinsic_proto::world::IdAndName id_name;
        id_name.set_id(frame.id());
        id_name.set_name(frame.name());
        id_name_list.emplace_back(std::move(id_name));
      }
      for (const intrinsic_proto::world::IdAndName& object :
           object.children()) {
        id_name_list.push_back(object);
      }
      break;
    }

    case intrinsic_proto::executive::WorldQuery::Query::
        kJointConfigurationsOf: {
      if (object.type() != intrinsic_proto::world::KINEMATIC_OBJECT) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Object %s is not a kinematic object", object.name()));
      }
      const intrinsic_proto::world::KinematicObjectComponent&
          kinematic_object_component = object.kinematic_object_component();
      for (const intrinsic_proto::world::NamedJointConfiguration& joint_config :
           kinematic_object_component.named_joint_configurations()) {
        intrinsic_proto::world::IdAndName id_name;
        id_name.set_name(joint_config.name());
        id_name_list.emplace_back(std::move(id_name));
      }
      break;
    }

    default:
      return absl::InvalidArgumentError("No query type set");
  }

  if (world_query->has_filter()) {
    const intrinsic_proto::executive::WorldQuery::Filter& filter =
        world_query->filter();
    switch (filter.filter_type_case()) {
      case intrinsic_proto::executive::WorldQuery::Filter::kNameRegex:
        INTR_RETURN_IF_ERROR(
            FilterIdNameListByNameRegEx(id_name_list, filter.name_regex()));
        break;

      default:
        return absl::InvalidArgumentError("Invalid filter spec in world query");
    }
  }

  if (world_query->has_order()) {
    const intrinsic_proto::executive::WorldQuery::Order& order =
        world_query->order();
    switch (order.by()) {
      case intrinsic_proto::executive::WorldQuery::Order::NAME:
        INTR_RETURN_IF_ERROR(
            SortIdNameListByName(id_name_list, order.direction()));
        break;

      default:
        return absl::InvalidArgumentError("No ordering criterion specified");
    }
  }

  intrinsic_proto::executive::AnyList any_list;
  switch (world_query->select().query_type_case()) {
    case intrinsic_proto::executive::WorldQuery::Query::kChildFramesOf: {
      any_list = ConvertIdNameToAnyList<intrinsic_proto::world::FrameReference>(
          id_name_list);
      break;
    }
    case intrinsic_proto::executive::WorldQuery::Query::kChildObjectsOf: {
      any_list =
          ConvertIdNameToAnyList<intrinsic_proto::world::ObjectReference>(
              id_name_list);
      break;
    }
    case intrinsic_proto::executive::WorldQuery::Query::kChildrenOf: {
      any_list = ConvertIdNameToAnyList<
          intrinsic_proto::world::TransformNodeReference>(id_name_list);
      break;
    }

    case intrinsic_proto::executive::WorldQuery::Query::
        kJointConfigurationsOf: {
      any_list = ExtractNamedJointConfigurations(
          object.kinematic_object_component(), id_name_list);
      break;
    }

    default:
      return absl::InvalidArgumentError("No query type set");
  }

  return proto_manager_->AddGeneratedProto(any_list);
}

void ClipsWorld::WaitForFinishAll() {
  // Stop the bundle, wait for all threads to finish and create a new thread
  // pool. This is required here since semantically we just want to wait for all
  // jobs to finish. The ThreadPool itself does not offer this API.
  bundle_.emplace(10);
}

namespace {

// Converts an intrinsic_proto::Matrixd (4x4 matrix) into an
// intrinsic_proto::Pose. If the matrix proto is empty (0 rows and 0 cols),
// returns an Identity Pose proto. Otherwise, converts from Matrixd to
// Eigen::Matrix4d, validates affine properties, and serializes to Pose proto.
absl::StatusOr<Pose> MatrixToPoseProto(const Matrixd& matrix_proto) {
  if (matrix_proto.rows() == 0 && matrix_proto.cols() == 0) {
    return ToProto(Pose3d::Identity());
  }
  INTR_ASSIGN_OR_RETURN(const MatrixXd matrix_xd, FromProto(matrix_proto));
  INTR_ASSIGN_OR_RETURN(const Matrix4d matrix_4d, toAffineMatrix4d(matrix_xd));
  return ToProto(Pose3d(matrix_4d));
}

}  // namespace

absl::StatusOr<CreateObjectRequest> FootprintToCreateObjectRequest(
    absl::string_view world_id, absl::string_view local_name,
    const Footprint& footprint) {
  CreateObjectRequest request;
  request.set_world_id(world_id);
  request.set_name(local_name);
  request.set_name_is_global_alias(true);

  ObjectReferenceWithEntityFilter* parent_object =
      request.mutable_parent_object();
  parent_object->mutable_reference()->set_id(RootObjectId().value());
  parent_object->mutable_entity_filter()->set_include_base_entity(true);

  SceneObject* scene_object =
      request.mutable_create_from_scene_object()->mutable_scene_object();
  scene_object->set_name(local_name);
  scene_object->mutable_simulation_spec()->set_disabled(true);
  scene_object->mutable_entities()->Reserve(footprint.volume().size() + 1);

  // 1. Root Link Entity
  const std::string root_entity_name = absl::StrCat(local_name, "_root");
  Entity* root_entity = scene_object->add_entities();
  root_entity->set_name(root_entity_name);
  root_entity->mutable_link()->mutable_physics_component();

  // 2. N Volume Link Entities
  int volume_index = 0;
  for (const VolumeReservation& volume_reservation : footprint.volume()) {
    if (volume_reservation.type() != VolumeReservation::WRITE) {
      return absl::InvalidArgumentError(
          "CreateFootprintObject only supports WRITE based volume resources");
    }

    switch (volume_reservation.volume_oneof_case()) {
      case VolumeReservation::kTransformedGeometry: {
        Entity* volume_entity = scene_object->add_entities();
        volume_entity->set_name(
            absl::StrFormat("volume_%s_%d", local_name, volume_index++));
        volume_entity->set_parent_name(root_entity_name);
        *volume_entity->mutable_parent_t_this() = ToProto(Pose3d::Identity());

        volume_entity->mutable_link()->mutable_physics_component();

        GeometryComponent* geometry_component =
            volume_entity->mutable_link()->mutable_geometry_component();

        GeometryComponent::GeometrySet& collision_set =
            (*geometry_component
                  ->mutable_named_geometries())["Intrinsic_Collision"];
        (*collision_set.mutable_named_geometries())["0"] =
            volume_reservation.transformed_geometry();

        GeometryComponent::GeometrySet& visual_set =
            (*geometry_component
                  ->mutable_named_geometries())["Intrinsic_Visual"];
        (*visual_set.mutable_named_geometries())["0"] =
            volume_reservation.transformed_geometry();
        break;
      }
      case VolumeReservation::kShape: {
        const TransformedGeometryStorageRefs& old_refs =
            volume_reservation.shape();
        Entity* volume_entity = scene_object->add_entities();
        volume_entity->set_name(
            absl::StrFormat("volume_%s_%d", local_name, volume_index++));
        volume_entity->set_parent_name(root_entity_name);
        INTR_ASSIGN_OR_RETURN(*volume_entity->mutable_parent_t_this(),
                              MatrixToPoseProto(old_refs.ref_t_shape_aff()));

        volume_entity->mutable_link()->mutable_physics_component();

        GeometryComponent* geometry_component =
            volume_entity->mutable_link()->mutable_geometry_component();

        GeometryComponent::GeometrySet& collision_set =
            (*geometry_component
                  ->mutable_named_geometries())["Intrinsic_Collision"];
        GeometryComponent::Geometry* collision_geo =
            collision_set.add_geometries();
        *collision_geo->mutable_geometry_storage_refs() =
            old_refs.geometry_storage_refs();

        GeometryComponent::GeometrySet& visual_set =
            (*geometry_component
                  ->mutable_named_geometries())["Intrinsic_Visual"];
        GeometryComponent::Geometry* visual_geo = visual_set.add_geometries();
        *visual_geo->mutable_geometry_storage_refs() =
            old_refs.geometry_storage_refs();
        break;
      }
      case VolumeReservation::VOLUME_ONEOF_NOT_SET:
      default:
        return absl::InvalidArgumentError(
            "CreateFootprintObject requires each VolumeReservation to specify "
            "a volume.");
    }
  }
  return request;
}

absl::StatusOr<std::string> AddFootprintVolumesToWorld(
    ObjectWorldService::StubInterface& object_world_service_stub,
    absl::string_view world_id, absl::string_view action_id,
    const Footprint& footprint) {
  // If the footprint has no volumes, we can skip making the call to the object
  // world service and just return an empty string.
  if (footprint.volume().empty()) {
    return "";
  }

  // Validate the local_name before creating the object request.
  const std::string local_name = object_world::GetObjectViewCompatibleName(
      absl::StrCat(action_id, "_footprint"));

  INTR_ASSIGN_OR_RETURN(
      CreateObjectRequest request,
      FootprintToCreateObjectRequest(world_id, local_name, footprint));

  grpc::ClientContext context;
  ConfigureClientContext(&context);

  Object response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      object_world_service_stub.CreateObject(&context, request, &response)));

  return request.name();
}

absl::Status RemoveFootprintVolumesFromWorld(
    ObjectWorldService::StubInterface& object_world_service_stub,
    absl::string_view world_id, absl::string_view footprint_object_name) {
  // An empty footprint object name means no need to delete anything.
  if (footprint_object_name.empty()) {
    return absl::OkStatus();
  }

  grpc::ClientContext context;
  ConfigureClientContext(&context);

  DeleteObjectRequest request;
  request.set_world_id(world_id);
  request.mutable_object()->mutable_by_name()->set_object_name(
      footprint_object_name);
  request.set_force(true);

  google::protobuf::Empty response;
  return ToAbslStatus(
      object_world_service_stub.DeleteObject(&context, request, &response));
}

}  // namespace executive
}  // namespace intrinsic
