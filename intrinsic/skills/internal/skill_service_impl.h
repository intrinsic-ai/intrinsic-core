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

#ifndef INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_IMPL_H_
#define INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_IMPL_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/empty.pb.h"
#include "google/protobuf/message.h"
#include "grpcpp/channel.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/geometry/storage/geometry_library.h"  
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/skills/cc/skill_canceller.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/internal/runtime_data.h"
#include "intrinsic/skills/internal/skill_repository.h"
#include "intrinsic/skills/proto/skill_service.grpc.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/proto/skills.pb.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"

namespace intrinsic {
namespace skills {

namespace internal {

// Maximum number of operations to keep in a SkillOperations instance.
// This value places a hard upper limit on the number of one type of skill that
// can execute simultaneously.
constexpr int32_t kMaxNumOperations = 100;

// Encapsulates a single skill operation.
class SkillOperation {
 public:
  SkillOperation(absl::string_view name,
                 const internal::SkillRuntimeData& runtime_data)
      : canceller_(std::make_shared<SkillCancellationManager>(
            runtime_data.GetExecutionOptions().GetCancellationReadyTimeout(),
            /*operation_name=*/name)),
        runtime_data_(runtime_data) {
    absl::MutexLock lock(&operation_mutex_);
    operation_.set_name(name);
  }

  // Supports cooperative cancellation of the operation.
  std::shared_ptr<SkillCancellationManager> canceller() { return canceller_; }

  // True if the skill execution has finished.
  bool finished() { return finished_notification_.HasBeenNotified(); }

  // A unique name for the operation.
  std::string name() {
    absl::ReaderMutexLock lock(&operation_mutex_);
    return operation_.name();
  }

  // A copy of the underlying Operation proto.
  google::longrunning::Operation operation() {
    absl::ReaderMutexLock lock(&operation_mutex_);
    return operation_;
  }

  // The skill's runtime data.
  const internal::SkillRuntimeData& runtime_data() const {
    return runtime_data_;
  }

  // Starts executing the skill operation.
  absl::Status Start(
      absl::AnyInvocable<
          absl::StatusOr<std::unique_ptr<::google::protobuf::Message>>()>
          op,
      absl::string_view op_name,
      std::optional<intrinsic_proto::data_logger::Context> log_context =
          std::nullopt) ABSL_LOCKS_EXCLUDED(thread_mutex_);

  // Requests cancellation of the operation.
  absl::Status RequestCancellation();

  // Waits for the operation to finish.
  //
  // Returns the state of the operation when it finished or the wait timed out.
  absl::StatusOr<google::longrunning::Operation> WaitExecution(
      std::optional<absl::Duration> timeout);

  // Waits for the entire operation to finish.
  //
  // This wait is similar to `WaitExecution`, except that it also waits for
  // post-execution cleanup, like joining and deleting the thread that executed
  // the skill.
  //
  // `caller_name` is specified for logging.
  void WaitOperation(absl::string_view caller_name)
      ABSL_LOCKS_EXCLUDED(thread_mutex_);

 private:
  std::shared_ptr<SkillCancellationManager> canceller_;

  absl::Mutex operation_mutex_;
  google::longrunning::Operation operation_ ABSL_GUARDED_BY(operation_mutex_);

  internal::SkillRuntimeData runtime_data_;

  // Notified when the operation is finished.
  absl::Notification finished_notification_;

  absl::Mutex thread_mutex_;
  Thread thread_ ABSL_GUARDED_BY(thread_mutex_);
};

// Cleans up skill execution operations once they are finished.
class SkillOperationCleaner {
 public:
  SkillOperationCleaner() {
    absl::MutexLock lock(&queue_mutex_);
    queue_processed_ = std::make_shared<absl::Notification>();
    queue_processed_->Notify();
  }

  // Start watching an operation, and clean it up when it finishes.
  absl::Status Watch(std::shared_ptr<SkillOperation> operation)
      ABSL_LOCKS_EXCLUDED(queue_mutex_, thread_mutex_);

  // Waits for all operations to be cleaned up and for the queue processing
  // thread to finish.
  //
  // `caller_name` is specified for logging.
  void WaitOperations(const std::string& caller_name)
      ABSL_LOCKS_EXCLUDED(queue_mutex_, thread_mutex_);

 private:
  // Process the queue of operations.
  void ProcessQueue() ABSL_LOCKS_EXCLUDED(queue_mutex_);

  // Wait for the queue processing thread to finish.
  //
  // `caller_name` is specified for logging.
  void WaitThread(const std::string& caller_name = "")
      ABSL_SHARED_LOCKS_REQUIRED(thread_mutex_);

  absl::Mutex queue_mutex_;
  std::deque<std::shared_ptr<SkillOperation>> queue_
      ABSL_GUARDED_BY(queue_mutex_);
  std::shared_ptr<absl::Notification> queue_processed_
      ABSL_GUARDED_BY(queue_mutex_);

  absl::Mutex thread_mutex_;
  Thread thread_ ABSL_GUARDED_BY(thread_mutex_);
};

// A collection of skill operations.
class SkillOperations {
 public:
  // Adds an operation to the collection.
  absl::Status Add(std::shared_ptr<SkillOperation> operation)
      ABSL_LOCKS_EXCLUDED(update_mutex_);

  // Gets an operation by name.
  absl::StatusOr<std::shared_ptr<SkillOperation>> Get(absl::string_view name)
      ABSL_LOCKS_EXCLUDED(update_mutex_);

  // Clears all operations in the collection.
  //
  // NOTE: If wait_for_operations is false, the operations must all be
  // finished before clearing them. If any operation is not yet finished, no
  // operations will be cleared, and an error status will be returned.
  absl::Status Clear(bool wait_for_operations)
      ABSL_LOCKS_EXCLUDED(update_mutex_);

 private:
  // Protects access while updates occur.
  mutable absl::Mutex update_mutex_;

  // Tracked operation names, in order of addition, so we can remove the oldest
  // finished operation whenever operations_ is full.
  std::vector<std::string> operation_names_ ABSL_GUARDED_BY(update_mutex_);

  // Map from operation name to operation. Limited in `Add` to have at most
  // kMaxNumOperations elements.
  absl::flat_hash_map<std::string, std::shared_ptr<SkillOperation>> operations_
      ABSL_GUARDED_BY(update_mutex_);

  SkillOperationCleaner cleaner_;
};

}  // namespace internal

// Can be provided to the service to inspect the requests that it handles.
class RequestWatcher {
 public:
  RequestWatcher() = default;

  // Not copyable or movable, unless needed in the future.
  RequestWatcher(const RequestWatcher&) = delete;
  RequestWatcher& operator=(const RequestWatcher&) = delete;
  RequestWatcher(const RequestWatcher&&) = delete;
  RequestWatcher& operator=(const RequestWatcher&&) = delete;

  // Adds a request.
  void AddRequest(const ::google::protobuf::Message& request) {
    google::protobuf::Any request_any;
    request_any.PackFrom(request);
    requests_.push_back(request_any);
  }

  // Gets the list of skill IDVersions of requests that match the template type
  // TRequest.
  template <typename TRequest>
  std::vector<std::string> GetSkillIdVersions() const {
    std::vector<std::string> skill_id_versions;
    for (const auto& request : GetRequests<TRequest>()) {
      skill_id_versions.push_back(request.instance().id_version());
    }
    return skill_id_versions;
  }

  // Gets the list of added requests that match the template type TRequest.
  template <typename TRequest>
  std::vector<TRequest> GetRequests() const {
    std::vector<TRequest> requests;
    for (const auto& request_any : requests_) {
      if (!request_any.Is<TRequest>()) continue;

      TRequest request;
      request_any.UnpackTo(&request);
      requests.push_back(request);
    }
    return requests;
  }

 private:
  std::vector<::google::protobuf::Any> requests_;
};

class SkillProjectorServiceImpl
    : public intrinsic_proto::skills::Projector::Service {
  using ObjectWorldService = ::intrinsic_proto::world::ObjectWorldService;
  using MotionPlannerService =
      ::intrinsic_proto::motion_planning::v1::MotionPlannerService;

 public:
  // All of the given references will be kept for the lifetime of the created
  // instance.
  explicit SkillProjectorServiceImpl(
      SkillRepository& skill_repository,

      GeometryLibrary& geometry_library,

      std::shared_ptr<ObjectWorldService::StubInterface> object_world_service,
      std::shared_ptr<MotionPlannerService::StubInterface>
          motion_planner_service);

  grpc::Status GetFootprint(
      grpc::ServerContext* context,
      const intrinsic_proto::skills::GetFootprintRequest* request,
      intrinsic_proto::skills::GetFootprintResult* result) override;

 private:
  absl::StatusOr<GetFootprintRequest> ProtoToGetFootprintRequest(
      const intrinsic_proto::skills::GetFootprintRequest& request);

  std::shared_ptr<ObjectWorldService::StubInterface> object_world_service_;
  std::shared_ptr<MotionPlannerService::StubInterface> motion_planner_service_;

  GeometryLibrary& geometry_library_;

  SkillRepository& skill_repository_;
  absl::Mutex message_mutex_;
  google::protobuf::MessageFactory* message_factory_
      ABSL_GUARDED_BY(message_mutex_);
  absl::flat_hash_map<std::string, const google::protobuf::Message* const>
      message_prototype_by_skill_name_ ABSL_GUARDED_BY(message_mutex_);
};

class SkillExecutorServiceImpl
    : public intrinsic_proto::skills::Executor::Service {
  using ObjectWorldService = ::intrinsic_proto::world::ObjectWorldService;
  using MotionPlannerService =
      ::intrinsic_proto::motion_planning::v1::MotionPlannerService;

 public:
  // All of the given references will be kept for the lifetime of the created
  // instance.
  //
  // A request watcher can be specified for testing. The service will use it to
  // record all requests that it handles.
  explicit SkillExecutorServiceImpl(
      SkillRepository& skill_repository,

      GeometryLibrary& geometry_library,

      std::shared_ptr<grpc::Channel> world_service_channel,
      std::shared_ptr<ObjectWorldService::StubInterface> object_world_service,
      std::shared_ptr<MotionPlannerService::StubInterface>
          motion_planner_service,
      RequestWatcher* request_watcher = nullptr);

  ~SkillExecutorServiceImpl() override;

  grpc::Status StartExecute(
      grpc::ServerContext* context,
      const intrinsic_proto::skills::ExecuteRequest* request,
      google::longrunning::Operation* result) override;

  grpc::Status StartPreview(
      grpc::ServerContext* context,
      const intrinsic_proto::skills::PreviewRequest* request,
      google::longrunning::Operation* result) override;

  grpc::Status GetOperation(
      grpc::ServerContext* context,
      const google::longrunning::GetOperationRequest* request,
      google::longrunning::Operation* result) override;

  grpc::Status CancelOperation(
      grpc::ServerContext* context,
      const google::longrunning::CancelOperationRequest* request,
      google::protobuf::Empty* result) override;

  grpc::Status WaitOperation(
      grpc::ServerContext* context,
      const google::longrunning::WaitOperationRequest* request,
      google::longrunning::Operation* result) override;

  grpc::Status ClearOperations(grpc::ServerContext* context,
                               const google::protobuf::Empty* request,
                               google::protobuf::Empty* result) override;

 private:
  absl::StatusOr<std::shared_ptr<internal::SkillOperation>> MakeOperation(
      absl::string_view name, absl::string_view skill_name,
      grpc::ServerContext* context);

  SkillRepository& skill_repository_;

  GeometryLibrary& geometry_library_;

  std::shared_ptr<grpc::Channel> world_service_channel_;
  std::shared_ptr<ObjectWorldService::StubInterface> object_world_service_;
  std::shared_ptr<MotionPlannerService::StubInterface> motion_planner_service_;

  RequestWatcher* request_watcher_;

  absl::Mutex message_mutex_;
  google::protobuf::MessageFactory* message_factory_
      ABSL_GUARDED_BY(message_mutex_);
  absl::flat_hash_map<std::string, const google::protobuf::Message* const>
      message_prototype_by_skill_name_ ABSL_GUARDED_BY(message_mutex_);
  internal::SkillOperations operations_;
};

// This class implements the SkillInformation service. The skill registry can\
// query it to get information about a single skill.
class SkillInformationServiceImpl
    : public intrinsic_proto::skills::SkillInformation::Service {
 public:
  explicit SkillInformationServiceImpl(intrinsic_proto::skills::Skill skill)
      : skill_(std::move(skill)) {}
  ::grpc::Status GetSkillInfo(
      ::grpc::ServerContext* context, const google::protobuf::Empty* request,
      intrinsic_proto::skills::SkillInformationResult* result) override;

 private:
  const intrinsic_proto::skills::Skill skill_;
};

}  // namespace skills
}  // namespace intrinsic

#endif  // INTRINSIC_SKILLS_INTERNAL_SKILL_SERVICE_IMPL_H_
