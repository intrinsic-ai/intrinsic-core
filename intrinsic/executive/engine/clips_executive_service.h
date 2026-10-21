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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_CLIPS_EXECUTIVE_SERVICE_H_
#define INTRINSIC_EXECUTIVE_ENGINE_CLIPS_EXECUTIVE_SERVICE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/executive/engine/clips_executor.h"
#include "intrinsic/executive/proto/blackboard_service.grpc.pb.h"
#include "intrinsic/executive/proto/blackboard_service.pb.h"
#include "intrinsic/executive/proto/executive_debug_service.grpc.pb.h"
#include "intrinsic/executive/proto/executive_debug_service.pb.h"
#include "intrinsic/executive/proto/executive_service.grpc.pb.h"
#include "intrinsic/executive/proto/executive_service.pb.h"
#include "intrinsic/executive/proto/run_metadata.pb.h"
#include "intrinsic/util/pagination/page_size.h"

namespace intrinsic {
namespace executive {

constexpr int64_t kListBlackboardSnapshotsMaxPageSize = 200;
constexpr int64_t kListBlackboardSnapshotsDefaultPageSize = 200;

class ClipsExecutiveService
    : public intrinsic_proto::executive::ExecutiveService::Service {
  ClipsExecutiveService(const ClipsExecutiveService&) = delete;
  ClipsExecutiveService& operator=(const ClipsExecutiveService&) = delete;

  friend class ExecutiveDebugService;
  friend class ExecutiveBlackboardService;

 public:
  // Default factory for service.
  static absl::StatusOr<std::unique_ptr<ClipsExecutiveService>> CreateService();

  // Factory where an initialised executor is injected. Note that the injected
  // executor is reset if 'Reset' is called.
  static absl::StatusOr<std::unique_ptr<ClipsExecutiveService>> CreateService(
      std::unique_ptr<intrinsic::executive::ClipsExecutor> executor);

  ~ClipsExecutiveService() override = default;

  void Shutdown();

  grpc::Status CreateOperation(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::CreateOperationRequest* request,
      google::longrunning::Operation* response) override;

  grpc::Status ListOperations(
      grpc::ServerContext* context,
      const google::longrunning::ListOperationsRequest* request,
      google::longrunning::ListOperationsResponse* response) override;

  grpc::Status GetOperation(
      grpc::ServerContext* context,
      const google::longrunning::GetOperationRequest* request,
      google::longrunning::Operation* response) override;

  grpc::Status GetOperationView(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::GetOperationViewRequest* request,
      google::longrunning::Operation* response) override;

  grpc::Status GetOperationMetadata(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::GetOperationMetadataRequest* request,
      intrinsic_proto::executive::RunMetadata* response) override;

  grpc::Status DeleteOperation(
      grpc::ServerContext* context,
      const google::longrunning::DeleteOperationRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status StartOperation(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::StartOperationRequest* request,
      google::longrunning::Operation* response) override;

  grpc::Status CancelOperation(
      grpc::ServerContext* context,
      const google::longrunning::CancelOperationRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status WaitOperation(
      grpc::ServerContext* context,
      const google::longrunning::WaitOperationRequest* request,
      google::longrunning::Operation* response) override;

  grpc::Status SuspendOperation(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::SuspendOperationRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status ResumeOperation(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::ResumeOperationRequest* request,
      google::longrunning::Operation* response) override;

  grpc::Status ResetOperation(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::ResetOperationRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status ForceAbandonOperation(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::ForceAbandonOperationRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status CreateBreakpoint(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::CreateBreakpointRequest* request,
      intrinsic_proto::executive::BehaviorTree::Breakpoint* response) override;

  grpc::Status DeleteBreakpoint(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::DeleteBreakpointRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status DeleteAllBreakpoints(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::DeleteAllBreakpointsRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status ListBreakpoints(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::ListBreakpointsRequest* request,
      intrinsic_proto::executive::ListBreakpointsResponse* response) override;

  grpc::Status SetNodeExecutionSettings(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::SetNodeExecutionSettingsRequest*
          request,
      intrinsic_proto::executive::SetNodeExecutionSettingsResponse* response)
      override;

 private:
  // Struct holding information related to a specific operation.
  struct OperationData {
    // Operation, before emitting call UpdateOperationData.
    google::longrunning::Operation operation;
    // The metadata is also contained in the operation, but in a packed Any
    // proto. We keep it around here for efficiency to avoid frequent unpacking.
    // Update the field here, then pack it into operation.metadata.
    intrinsic_proto::executive::RunMetadata metadata;
  };

  ClipsExecutiveService() = default;
  explicit ClipsExecutiveService(
      std::unique_ptr<intrinsic::executive::ClipsExecutor> executor);

  absl::Status InitExecutor();

  // Only once at startup.
  absl::Status InitLogging(absl::Duration grpc_connect_timeout);

  absl::StatusOr<OperationData*> FindOperationData(std::string_view name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_);

  void UpdateOperationData(OperationData* operation_data)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_)
          ABSL_SHARED_LOCKS_REQUIRED(executor_mutex_);

  absl::Status SetStartOperationModes(
      const intrinsic_proto::executive::StartOperationRequest* request)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_, executor_mutex_);
  absl::Status StartOperation(
      OperationData* operation_data,
      const std::optional<google::protobuf::Any>& parameters,
      const absl::flat_hash_map<std::string, std::string>& resources,
      const std::optional<std::string_view>& scene_id,
      std::vector<intrinsic_proto::executive::BehaviorTree::NodeIdentifier>
          recovery_nodes)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_, executor_mutex_);

  std::unique_ptr<intrinsic::executive::ClipsExecutor> executor_
      ABSL_GUARDED_BY(executor_mutex_);

  // All methods except 'InitExecutor' can share a mutex. 'InitExecutor' needs
  // to acquire an exclusive lock.
  absl::Mutex executor_mutex_;
  bool logging_initialized_ = false;

  std::atomic<bool> quit_ = false;

  // Currently we expect this to contain at most one entry. Once we extend the
  // executive to support multiple operations at a time this constraint will be
  // removed.
  absl::Mutex operations_mutex_ ABSL_ACQUIRED_BEFORE(executor_mutex_);
  absl::node_hash_map<std::string, OperationData> operations_
      ABSL_GUARDED_BY(operations_mutex_);
};

class ExecutiveDebugService
    : public intrinsic_proto::executive::ExecutiveDebug::Service {
 public:
  // Create an ExecutiveDebugService with a ClipsExecutiveService. The
  // ClipsExecutiveService must already be initialized.
  static absl::StatusOr<std::unique_ptr<ExecutiveDebugService>> CreateService(
      ClipsExecutiveService* clips_executive_service);

  ~ExecutiveDebugService() override = default;

  grpc::Status SetClipsTracing(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::SetClipsTracingRequest* request,
      intrinsic_proto::executive::SetClipsTracingResponse* response) override;

  grpc::Status GetClipsTracing(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::GetClipsTracingRequest* request,
      intrinsic_proto::executive::GetClipsTracingResponse* response) override;

  grpc::Status SetSpanFileWriting(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::SetSpanFileWritingRequest* request,
      intrinsic_proto::executive::SetSpanFileWritingResponse* response)
      override;

  grpc::Status GetSpanFileWriting(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::GetSpanFileWritingRequest* request,
      intrinsic_proto::executive::GetSpanFileWritingResponse* response)
      override;

  grpc::Status GetSpanTrace(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::GetSpanTraceRequest* request,
      grpc::ServerWriter<intrinsic_proto::executive::GetSpanTraceResponse>*
          writer) override;

  grpc::Status GetClipsTrace(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::GetClipsTraceRequest* request,
      intrinsic_proto::executive::GetClipsTraceResponse* response) override;

  grpc::Status GetClipsData(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::GetClipsDataRequest* request,
      intrinsic_proto::executive::GetClipsDataResponse* response) override;

 private:
  explicit ExecutiveDebugService(ClipsExecutiveService* clips_executive_service)
      : clips_executive_service_(clips_executive_service) {}

  ClipsExecutiveService* clips_executive_service_ =
      nullptr;  // externally owned
};

class ExecutiveBlackboardService
    : public intrinsic_proto::executive::ExecutiveBlackboard::Service {
 public:
  // Create an ExecutiveDebugService with a ClipsExecutiveService. The
  // ClipsExecutiveService must already be initialized.
  static absl::StatusOr<std::unique_ptr<ExecutiveBlackboardService>>
  CreateService(ClipsExecutiveService* clips_executive_service);

  ExecutiveBlackboardService(const ExecutiveBlackboardService&) = delete;
  ExecutiveBlackboardService& operator=(const ExecutiveBlackboardService&) =
      delete;

  grpc::Status GetBlackboardValue(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::GetBlackboardValueRequest* request,
      intrinsic_proto::executive::BlackboardValue* response) override;

  grpc::Status ListBlackboardValues(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::ListBlackboardValuesRequest* request,
      intrinsic_proto::executive::ListBlackboardValuesResponse* response)
      override;

  grpc::Status UpdateBlackboardValue(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::UpdateBlackboardValueRequest* request,
      intrinsic_proto::executive::BlackboardValue* response) override;

  grpc::Status DeleteBlackboardValue(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::DeleteBlackboardValueRequest* request,
      google::protobuf::Empty* response) override;

  grpc::Status CreateBlackboardSnapshot(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::CreateBlackboardSnapshotRequest*
          request,
      intrinsic_proto::executive::CreateBlackboardSnapshotResponse* response)
      override;

  grpc::Status LoadBlackboardSnapshot(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::LoadBlackboardSnapshotRequest* request,
      intrinsic_proto::executive::LoadBlackboardSnapshotResponse* response)
      override;

  grpc::Status ListBlackboardSnapshots(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::ListBlackboardSnapshotsRequest* request,
      intrinsic_proto::executive::ListBlackboardSnapshotsResponse* response)
      override;

  grpc::Status DeleteBlackboardSnapshot(
      grpc::ServerContext* context,
      const intrinsic_proto::executive::DeleteBlackboardSnapshotRequest*
          request,
      google::protobuf::Empty* response) override;

 private:
  explicit ExecutiveBlackboardService(
      ClipsExecutiveService* clips_executive_service)
      : clips_executive_service_(clips_executive_service),
        list_snapshots_page_size_validator_(
            kListBlackboardSnapshotsDefaultPageSize,
            kListBlackboardSnapshotsMaxPageSize) {}

  ClipsExecutiveService* clips_executive_service_ =
      nullptr;  // externally owned

  intrinsic::PageSizeValidator list_snapshots_page_size_validator_;
};

}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_ENGINE_CLIPS_EXECUTIVE_SERVICE_H_
