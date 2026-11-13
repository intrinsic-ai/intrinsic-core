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

#ifndef INTRINSIC_LONGRUNNING_OPERATION_SCHEDULER_H_
#define INTRINSIC_LONGRUNNING_OPERATION_SCHEDULER_H_

#include <future>  // NOLINT(build/c++11)
#include <memory>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"

namespace intrinsic::longrunning {

// An implementation of OperationSchedulerInterface that maintains a scheduler
// for running operations.
class OperationScheduler : public OperationSchedulerInterface {
 public:
  OperationScheduler();
  ~OperationScheduler() override;

  absl::StatusOr<google::longrunning::Operation> AddOperation(
      absl::string_view operation_name,
      std::optional<google::protobuf::Any> custom_data,
      StatusFuncT<google::protobuf::Any> status_func,
      OperationFuncT<google::protobuf::Any> operation_func) override;

  absl::StatusOr<google::longrunning::ListOperationsResponse> ListOperations(
      const google::longrunning::ListOperationsRequest& list_operations_request)
      override ABSL_LOCKS_EXCLUDED(mutex_);

  absl::StatusOr<google::longrunning::Operation> GetOperation(
      const google::longrunning::GetOperationRequest& get_operation_request)
      override ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status DeleteOperation(
      const google::longrunning::DeleteOperationRequest&
          delete_operation_request) override ABSL_LOCKS_EXCLUDED(mutex_);

  absl::Status CancelOperation(
      const google::longrunning::CancelOperationRequest&
          cancel_operation_request) override ABSL_LOCKS_EXCLUDED(mutex_);

  absl::StatusOr<google::longrunning::Operation> WaitOperation(
      const google::longrunning::WaitOperationRequest& request) override;

 private:
  // Result type for a long running operations
  using OperationResult = absl::StatusOr<google::protobuf::Any>;
  // Future type for a long running operations
  using OperationFuture = std::future<OperationResult>;

  // The metadata surrounding an ongoing operation. It contains useful data as
  // well as some references to the underlying lambda being run in the form a
  // future.
  struct OperationData {
    // Note: if we plan to integrate process information, we can do it here.
    // A future which holds the operation result (on success) or the status
    // information about process failures.
    OperationFuture operation_future;

    // Once we retrieve the result from the future, we store it here since it
    // can only be retrieved once from an actual future.
    std::optional<OperationResult> future_result;

    // User specified custom data that will be returned in the response.
    std::optional<google::protobuf::Any> custom_data;

    // Timestamp when this operation was submitted.
    absl::Time start_time;

    // Operation context for worker cancellation and progress reporting.
    OperationContext operation_context;

    // The status function used to gauge how the long running operation is doing
    // and what the progress might be.
    OperationSchedulerInterface::StatusFuncT<google::protobuf::Any> status_func;

    // Helper method for calling the status lambda with the data stored within
    // this struct itself.
    absl::StatusOr<google::protobuf::Any> CallStatus() const {
      return status_func(start_time, custom_data, operation_context);
    }
  };

  static bool IsCancelled(const OperationData& operation_data);
  static absl::StatusOr<google::longrunning::Operation> GetOperation(
      absl::string_view operation_name, OperationData& operation_data);

  absl::Mutex mutex_;

  // A map between unique operation identifiers and data associated with the
  // operation.
  absl::flat_hash_map<std::string, OperationData> active_operations_
      ABSL_GUARDED_BY(mutex_);
};

}  // namespace intrinsic::longrunning

#endif  // INTRINSIC_LONGRUNNING_OPERATION_SCHEDULER_H_
