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

#include "intrinsic/skills/internal/skill_service_impl.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/message.h"  
#include "google/rpc/status.pb.h"
#include "grpcpp/channel.h"
#include "grpcpp/grpcpp.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/geometry/storage/geometry_library.h"  
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/motion_planning/motion_planner_client.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/cc/skill_logging_context.h"
#include "intrinsic/skills/internal/equipment_utilities.h"
#include "intrinsic/skills/internal/error_utils.h"
#include "intrinsic/skills/internal/execute_context_impl.h"
#include "intrinsic/skills/internal/get_footprint_context_impl.h"
#include "intrinsic/skills/internal/preview_context_impl.h"
#include "intrinsic/skills/internal/runtime_data.h"
#include "intrinsic/skills/internal/skill_repository.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/stats/scoped_span.h"  
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "opentelemetry/trace/span_context.h"       
#include "opentelemetry/trace/span_startoptions.h"  

namespace intrinsic {
namespace skills {

using ::intrinsic::assets::NameFrom;
using ::intrinsic::assets::RemoveVersionFrom;

namespace {

template <class Request>
absl::Status ValidateRequest(const Request& request) {
  if (request.world_id().empty()) {
    return absl::InvalidArgumentError(
        "Cannot load a world with an empty world_id");
  }
  return absl::OkStatus();
}

}  // namespace

namespace internal {

absl::Status SkillOperation::Start(
    absl::AnyInvocable<
        absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>()>
        op,
    absl::string_view op_name,
    std::optional<intrinsic_proto::data_logger::Context> log_context) {
  {
    absl::MutexLock lock(&thread_mutex_);

    thread_ = Thread(
        [this,
         op = std::make_unique<absl::AnyInvocable<
             absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>()>>(
             std::move(op)),
         op_name, log_context]() -> absl::Status {
          absl::StatusOr<std::unique_ptr<::google::protobuf::Message>> result =
              (*op)();

          {
            absl::MutexLock lock(&operation_mutex_);

            if (result.ok()) {
              operation_.mutable_response()->PackFrom(**result);
            } else {
              google::rpc::Status rpc_status = CreateSkillError(
                  result.status(), runtime_data().GetId(), op_name,
                  runtime_data().GetStatusSpecs(), log_context);
              LOG(ERROR) << ToAbslStatus(rpc_status);
              *operation_.mutable_error() = std::move(rpc_status);
            }

            operation_.set_done(true);
          }

          finished_notification_.Notify();

          return absl::OkStatus();
        });
  }

  return absl::OkStatus();
}

absl::Status SkillOperation::RequestCancellation() {
  if (!runtime_data_.GetExecutionOptions().SupportsCancellation()) {
    return absl::UnimplementedError(absl::StrFormat(
        "Skill does not support cancellation: %s.", runtime_data().GetId()));
  }
  if (canceller_->cancelled()) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "Skill was already cancelled: %s.", runtime_data().GetId()));
  }
  if (finished()) {
    LOG(INFO) << "Ignoring cancellation request, since operation %r has "
                 "already finished.";
    return absl::OkStatus();
  }

  return canceller_->Cancel();
}

absl::StatusOr<google::longrunning::Operation> SkillOperation::WaitExecution(
    std::optional<absl::Duration> timeout) {
  absl::Duration resolved_timeout = timeout.value_or(
      runtime_data_.GetExecutionOptions().GetExecutionTimeout());
  absl::Time deadline = absl::Now() + resolved_timeout;
  if (!finished_notification_.WaitForNotificationWithDeadline(deadline)) {
    return StatusBuilder(kSkillServiceComponent, kSkillServiceWaitTimeoutCode,
                         {.title = "Waiting for skill to finish timed out",
                          .user_message = absl::StrFormat(
                              "Skill operation '%s' did not finish within %s.",
                              name(), absl::FormatDuration(resolved_timeout)),
                          .generic_code = absl::StatusCode::kDeadlineExceeded});
  }

  return operation();
}

void SkillOperation::WaitOperation(absl::string_view caller_name) {
  // Wait for the skill operation to finish.
  LOG(INFO) << caller_name << " waiting for operation to finish: \"" << name()
            << "\".";
  finished_notification_.WaitForNotification();

  // Wait until the thread that executed the operation is finished. This wait
  // shouldn't take long, since `finished_notification_` is notified as the
  // last step of execution.
  {
    absl::MutexLock lock(&thread_mutex_);
    if (thread_.joinable()) {
      LOG(INFO) << caller_name << " joining operation thread: \"" << name()
                << "\".";
      thread_.join();
    }
  }

  LOG(INFO) << caller_name << " finished waiting for operation: \"" << name()
            << "\".";
}

absl::Status SkillOperationCleaner::Watch(
    std::shared_ptr<SkillOperation> operation) {
  bool start_processing_queue = false;
  {
    absl::MutexLock lock(&queue_mutex_);

    queue_.push_back(operation);

    if (queue_processed_->HasBeenNotified()) {
      start_processing_queue = true;
      queue_processed_ = std::make_shared<absl::Notification>();
    }
  }

  if (start_processing_queue) {
    {
      absl::MutexLock lock(&thread_mutex_);

      WaitThread();
      thread_ = Thread([this]() { ProcessQueue(); });
    }
  }

  return absl::OkStatus();
}

void SkillOperationCleaner::WaitOperations(const std::string& caller_name) {
  std::shared_ptr<absl::Notification> queue_processed;
  {
    absl::MutexLock lock(&queue_mutex_);
    LOG(INFO) << caller_name << " waiting for cleaner to process "
              << queue_.size() << " operation(s)).";

    queue_processed = queue_processed_;
  }
  queue_processed->WaitForNotification();

  {
    absl::MutexLock lock(&thread_mutex_);
    WaitThread(caller_name);
  }

  LOG(INFO) << caller_name << " finished waiting.";
}

void SkillOperationCleaner::ProcessQueue() {
  while (true) {
    std::shared_ptr<SkillOperation> operation;
    {
      absl::MutexLock lock(&queue_mutex_);
      if (queue_.empty()) {
        queue_processed_->Notify();
        return;
      }

      LOG(INFO) << "Cleaner queue has " << queue_.size() << " operation(s).";

      operation = queue_.front();
      queue_.pop_front();
    }

    operation->WaitOperation("Cleaner");
  }
}

void SkillOperationCleaner::WaitThread(const std::string& caller_name) {
  if (!caller_name.empty()) {
    LOG(INFO) << caller_name << " joining cleaner thread.";
  }
  if (thread_.joinable()) {
    thread_.join();
  }
}

absl::Status SkillOperations::Add(std::shared_ptr<SkillOperation> operation) {
  absl::MutexLock lock(&update_mutex_);

  // First remove the oldest finished operation if we've reached our limit of
  // tracked operations.
  while (operation_names_.size() >= kMaxNumOperations) {
    bool finished_operation_found = false;
    for (auto name_it = operation_names_.begin(), end = operation_names_.end();
         name_it != end; ++name_it) {
      auto op_it = operations_.find(*name_it);
      if (op_it == operations_.end()) {
        return absl::InternalError(absl::StrFormat(
            "%s found in operation_names_ but not in operations_.", *name_it));
      }
      if (op_it->second->finished()) {
        finished_operation_found = true;
        operation_names_.erase(name_it);
        operations_.erase(op_it);
        break;
      }
    }
    if (!finished_operation_found) {
      return absl::FailedPreconditionError(
          absl::StrFormat("Cannot add operation %s, since there are already %d "
                          "unfinished operations.",
                          operation->name(), operations_.size()));
    }
  }

  std::string operation_name = operation->name();

  if (auto [_, inserted] = operations_.emplace(operation_name, operation);
      !inserted) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "An operation already exists with name '%s'.", operation_name));
  }

  operation_names_.push_back(operation_name);

  INTR_RETURN_IF_ERROR(cleaner_.Watch(operation));

  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<SkillOperation>> SkillOperations::Get(
    absl::string_view name) {
  absl::MutexLock lock(&update_mutex_);
  auto itr = operations_.find(name);
  if (itr == operations_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("No operation found with name '%s'.", name));
  }
  return itr->second;
}

absl::Status SkillOperations::Clear(bool wait_for_operations) {
  absl::MutexLock lock(&update_mutex_);

  std::vector<std::string> unfinished_operation_names;
  for (auto& [_, operation] : operations_) {
    if (operation->finished() || wait_for_operations) {
      // Wait until the operation thread is finished.
      operation->WaitOperation("Clear operations");
    } else {
      unfinished_operation_names.push_back(operation->name());
    }
  }
  if (!unfinished_operation_names.empty()) {
    return absl::FailedPreconditionError(
        absl::StrFormat("The following operations are not yet finished: '%s'.",
                        absl::StrJoin(unfinished_operation_names, ", ")));
  }

  operations_ = {};
  operation_names_ = {};

  // Wait until the cleaner thread is finished.
  cleaner_.WaitOperations("Clear operations");

  return absl::OkStatus();
}

}  // namespace internal

SkillProjectorServiceImpl::SkillProjectorServiceImpl(
    SkillRepository& skill_repository,

    GeometryLibrary& geometry_library,

    std::shared_ptr<ObjectWorldService::StubInterface> object_world_service,
    std::shared_ptr<MotionPlannerService::StubInterface> motion_planner_service)
    : object_world_service_(std::move(object_world_service)),
      motion_planner_service_(std::move(motion_planner_service)),
      geometry_library_(geometry_library),  
      skill_repository_(skill_repository),
      message_factory_(google::protobuf::MessageFactory::generated_factory()) {}

absl::StatusOr<GetFootprintRequest>
SkillProjectorServiceImpl::ProtoToGetFootprintRequest(
    const intrinsic_proto::skills::GetFootprintRequest& request) {
  INTR_ASSIGN_OR_RETURN(std::string id,
                        RemoveVersionFrom(request.instance().id_version()));
  INTR_ASSIGN_OR_RETURN(std::string skill_name, NameFrom(id));
  INTR_ASSIGN_OR_RETURN(internal::SkillRuntimeData runtime_data,
                        skill_repository_.GetSkillRuntimeData(skill_name));

  return GetFootprintRequest(
      request.parameters(), runtime_data.GetParameterData().GetDefault());
}

grpc::Status SkillProjectorServiceImpl::GetFootprint(
    grpc::ServerContext* context,
    const intrinsic_proto::skills::GetFootprintRequest* request,
    intrinsic_proto::skills::GetFootprintResult* result) {

  const stats::ScopedSpan span(
      absl::StrCat(request->instance().id_version(), "/GetFootprint"), context);

  LOG(INFO) << "Attempting to get footprint '"
            << request->instance().id_version() << "' skill with world id '"
            << request->world_id() << "'";

  INTR_RETURN_IF_ERROR_GRPC(ValidateRequest(*request));

  INTR_ASSIGN_OR_RETURN_GRPC(const std::string skill_name,
                             NameFrom(request->instance().id_version()));
  LOG(INFO) << "Calling GetFootprint for skill name: " << skill_name;

  // TODO(b/263495118): change from using name in repository to using id.
  INTR_ASSIGN_OR_RETURN_GRPC(std::unique_ptr<SkillProjectInterface> skill,
                             skill_repository_.GetSkillProject(skill_name));

  INTR_ASSIGN_OR_RETURN_GRPC(GetFootprintRequest get_footprint_request,
                             ProtoToGetFootprintRequest(*request));

  INTR_ASSIGN_OR_RETURN_GRPC(EquipmentPack equipment,
                             EquipmentPack::GetEquipmentPack(*request));

  const std::string context_id =
      std::to_string(request->context().executive_plan_action_id());

  GetFootprintContextImpl footprint_context(
      std::move(equipment),
      /*motion_planner=*/
      motion_planning::MotionPlannerClient(request->world_id(),
                                           motion_planner_service_),
      /*object_world=*/
      world::ObjectWorldClient(request->world_id(), object_world_service_)

      ,
      geometry_library_

      ,
      context_id);

  INTR_ASSIGN_OR_RETURN_GRPC(internal::SkillRuntimeData runtime_data,
                             skill_repository_.GetSkillRuntimeData(skill_name));

  auto skill_result =
      skill->GetFootprint(get_footprint_request, footprint_context);

  if (!skill_result.ok()) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        const std::string skill_id,
        RemoveVersionFrom(request->instance().id_version()));
    std::optional<intrinsic_proto::data_logger::Context> log_context;
    if (request->has_context()) {
      log_context = request->context();
    }
    // This will exit, we have already ensured non-ok status above, but this way
    // we can invoke LogWarning.
    INTR_RETURN_IF_ERROR_GRPC(
        ToAbslStatus(
            CreateSkillError(skill_result.status(), skill_id, "GetFootprint",
                             runtime_data.GetStatusSpecs(), log_context)))
        .LogWarning();
  }

  // Populate the footprint in the result with equipment reservations.
  *result->mutable_footprint() = std::move(skill_result).value();
  INTR_ASSIGN_OR_RETURN_GRPC(
      auto resource_reservations,
      ReserveEquipmentRequired(
          runtime_data.GetResourceData().GetRequiredResources(),
          request->instance().resource_handles()));
  for (const auto& resource_reservation : resource_reservations) {
    *result->mutable_footprint()->add_resource_reservation() =
        resource_reservation;
  }

  return ::grpc::Status::OK;
}

SkillExecutorServiceImpl::SkillExecutorServiceImpl(
    SkillRepository& skill_repository,

    GeometryLibrary& geometry_library,

    std::shared_ptr<grpc::Channel> world_service_channel,
    std::shared_ptr<ObjectWorldService::StubInterface> object_world_service,
    std::shared_ptr<MotionPlannerService::StubInterface> motion_planner_service,
    RequestWatcher* request_watcher)
    : skill_repository_(skill_repository),
      geometry_library_(geometry_library),  
      world_service_channel_(std::move(world_service_channel)),
      object_world_service_(std::move(object_world_service)),
      motion_planner_service_(std::move(motion_planner_service)),
      request_watcher_(request_watcher),
      message_factory_(google::protobuf::MessageFactory::generated_factory()) {}

SkillExecutorServiceImpl::~SkillExecutorServiceImpl() {
  operations_.Clear(true).IgnoreError();
}

grpc::Status SkillExecutorServiceImpl::StartExecute(
    grpc::ServerContext* context,
    const intrinsic_proto::skills::ExecuteRequest* request,
    google::longrunning::Operation* result) {

  const stats::ScopedSpan span(
      absl::StrCat(request->instance().id_version(), "/StartExecute"), context);


  LOG(INFO) << "Attempting to start execution of '"
            << request->instance().id_version() << "' skill with world id '"
            << request->world_id() << "'";

  if (request_watcher_ != nullptr) {
    request_watcher_->AddRequest(*request);
  }

  INTR_RETURN_IF_ERROR_GRPC(ValidateRequest(*request));

  INTR_ASSIGN_OR_RETURN_GRPC(std::string skill_name,
                             NameFrom(request->instance().id_version()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::shared_ptr<internal::SkillOperation> operation,
      MakeOperation(/*name=*/request->instance().instance_name(), skill_name,
                    context));

  INTR_ASSIGN_OR_RETURN_GRPC(std::unique_ptr<SkillExecuteInterface> skill,
                             skill_repository_.GetSkillExecute(skill_name));

  auto skill_request = std::make_unique<ExecuteRequest>(
      /*params=*/request->parameters(),
      /*param_defaults=*/
      operation->runtime_data().GetParameterData().GetDefault());

  INTR_ASSIGN_OR_RETURN_GRPC(EquipmentPack equipment,
                             EquipmentPack::GetEquipmentPack(*request));

  SkillLoggingContext logging_context = {
      .data_logger_context = request->context(),
      .skill_id = operation->runtime_data().GetId(),
  };

  const std::string context_id =
      std::to_string(request->context().executive_plan_action_id());

  auto skill_context = std::make_unique<ExecuteContextImpl>(
      /*canceller=*/operation->canceller(), std::move(equipment),
      /*logging_context=*/logging_context,
      /*motion_planner=*/
      motion_planning::MotionPlannerClient(request->world_id(),
                                           motion_planner_service_),
      /*object_world=*/
      world::ObjectWorldClient(request->world_id(), object_world_service_),
      geometry_library_,  
      world_service_channel_, context_id);

  std::optional<intrinsic_proto::data_logger::Context> log_context;
  if (request->has_context()) {
    log_context = request->context();
  }

  INTR_RETURN_IF_ERROR_GRPC(operation->Start(
      [skill = std::move(skill),
       span_context = span.context(),  
       skill_request = std::move(skill_request),
       skill_context = std::move(skill_context), request = *request]()
          -> absl::StatusOr<
              std::unique_ptr<intrinsic_proto::skills::ExecuteResult>> {

        intrinsic::stats::ScopedSpan skill_execute_span(
            "Skill Execute", {.parent = span_context});


        INTR_ASSIGN_OR_RETURN(
            std::unique_ptr<::google::protobuf::Message> skill_result,
            skill->Execute(*skill_request, *skill_context));

        auto result =
            std::make_unique<intrinsic_proto::skills::ExecuteResult>();
        if (skill_result != nullptr) {
          result->mutable_result()->PackFrom(*skill_result);
          if (result->result().Is<intrinsic_proto::skills::ExecuteResult>()) {
            return absl::InternalError(
                "Skill returned an ExecuteResult rather than a skill result "
                "message.");
          }
        }

        return result;
      },
      /*op_name=*/"Execute", log_context));

  *result = operation->operation();

  return grpc::Status::OK;
}

grpc::Status SkillExecutorServiceImpl::StartPreview(
    grpc::ServerContext* context,
    const intrinsic_proto::skills::PreviewRequest* request,
    google::longrunning::Operation* result) {

  const stats::ScopedSpan span(
      absl::StrCat(request->instance().id_version(), "/StartPreview"), context);


  LOG(INFO) << "Attempting to start preview of '"
            << request->instance().id_version() << "' skill with world id '"
            << request->world_id() << "'";

  INTR_RETURN_IF_ERROR_GRPC(ValidateRequest(*request));

  INTR_ASSIGN_OR_RETURN_GRPC(std::string skill_name,
                             NameFrom(request->instance().id_version()));
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::shared_ptr<internal::SkillOperation> operation,
      MakeOperation(/*name=*/request->instance().instance_name(), skill_name,
                    context));

  INTR_ASSIGN_OR_RETURN_GRPC(std::unique_ptr<SkillExecuteInterface> skill,
                             skill_repository_.GetSkillExecute(skill_name));

  auto skill_request = std::make_unique<PreviewRequest>(
      /*params=*/request->parameters(),
      /*param_defaults=*/
      operation->runtime_data().GetParameterData().GetDefault());

  INTR_ASSIGN_OR_RETURN_GRPC(EquipmentPack equipment,
                             EquipmentPack::GetEquipmentPack(*request));

  SkillLoggingContext logging_context = {
      .data_logger_context = request->context(),
      .skill_id = operation->runtime_data().GetId(),
  };

  const std::string preview_context_id =
      std::to_string(request->context().executive_plan_action_id());

  auto skill_context = std::make_unique<PreviewContextImpl>(
      /*canceller=*/operation->canceller(),
      /*equipment=*/std::move(equipment),
      /*logging_context=*/logging_context,
      /*motion_planner=*/
      motion_planning::MotionPlannerClient(request->world_id(),
                                           motion_planner_service_),
      /*object_world=*/
      world::ObjectWorldClient(request->world_id(), object_world_service_)


      ,
      /*geometry_library=*/geometry_library_

      ,
      preview_context_id);

  std::optional<intrinsic_proto::data_logger::Context> log_context;
  if (request->has_context()) {
    log_context = request->context();
  }

  INTR_RETURN_IF_ERROR_GRPC(operation->Start(
      [skill = std::move(skill),
       span_context = span.context(),  
       skill_request = std::move(skill_request),
       skill_context = std::move(skill_context)]()
          -> absl::StatusOr<
              std::unique_ptr<intrinsic_proto::skills::PreviewResult>> {

        intrinsic::stats::ScopedSpan skill_preview_span(
            "Skill Preview", {.parent = span_context});


        INTR_ASSIGN_OR_RETURN(
            std::unique_ptr<::google::protobuf::Message> skill_result,
            skill->Preview(*skill_request, *skill_context));

        auto result =
            std::make_unique<intrinsic_proto::skills::PreviewResult>();
        if (skill_result != nullptr) {
          result->mutable_result()->PackFrom(*skill_result);
        }
        result->mutable_expected_states()->Add(
            skill_context->GetWorldUpdates().begin(),
            skill_context->GetWorldUpdates().end());

        return result;
      },
      /*op_name=*/"Preview", log_context));

  *result = operation->operation();

  return grpc::Status::OK;
}

grpc::Status SkillExecutorServiceImpl::GetOperation(
    grpc::ServerContext* context,
    const google::longrunning::GetOperationRequest* request,
    google::longrunning::Operation* result) {
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::shared_ptr<internal::SkillOperation> operation,
      operations_.Get(request->name()));

  *result = operation->operation();
  return grpc::Status::OK;
}

grpc::Status SkillExecutorServiceImpl::CancelOperation(
    grpc::ServerContext* context,
    const google::longrunning::CancelOperationRequest* request,
    google::protobuf::Empty* result) {
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::shared_ptr<internal::SkillOperation> operation,
      operations_.Get(request->name()));

  INTR_RETURN_IF_ERROR_GRPC(operation->RequestCancellation());

  return grpc::Status::OK;
}

grpc::Status SkillExecutorServiceImpl::WaitOperation(
    grpc::ServerContext* context,
    const google::longrunning::WaitOperationRequest* request,
    google::longrunning::Operation* result) {
  std::optional<absl::Duration> timeout;
  if (request->has_timeout()) {
    INTR_ASSIGN_OR_RETURN_GRPC(timeout, ToAbslDuration(request->timeout()));
  }
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::shared_ptr<internal::SkillOperation> operation,
      operations_.Get(request->name()));
  INTR_ASSIGN_OR_RETURN_GRPC(*result, operation->WaitExecution(timeout));

  return grpc::Status::OK;
}

grpc::Status SkillExecutorServiceImpl::ClearOperations(
    grpc::ServerContext* context, const google::protobuf::Empty* request,
    google::protobuf::Empty* result) {
  return ToGrpcStatus(operations_.Clear(false));
}

absl::StatusOr<std::shared_ptr<internal::SkillOperation>>
SkillExecutorServiceImpl::MakeOperation(absl::string_view name,
                                        absl::string_view skill_name,
                                        grpc::ServerContext* context) {
  INTR_ASSIGN_OR_RETURN(internal::SkillRuntimeData runtime_data,
                        skill_repository_.GetSkillRuntimeData(skill_name));

  auto operation =
      std::make_shared<internal::SkillOperation>(name, runtime_data);

  INTR_RETURN_IF_ERROR(operations_.Add(operation));

  return operation;
}

::grpc::Status SkillInformationServiceImpl::GetSkillInfo(
    ::grpc::ServerContext* context, const google::protobuf::Empty* request,
    intrinsic_proto::skills::SkillInformationResult* result) {
  *result->mutable_skill() = skill_;
  return ::grpc::Status::OK;
}

}  // namespace skills
}  // namespace intrinsic
