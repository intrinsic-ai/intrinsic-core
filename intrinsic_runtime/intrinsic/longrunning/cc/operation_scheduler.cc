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

#include "intrinsic/longrunning/cc/operation_scheduler.h"

#include <chrono>  // NOLINT(build/c++11)
#include <future>  // NOLINT(build/c++11)
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "google/rpc/status.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::longrunning {

OperationScheduler::OperationScheduler() = default;
OperationScheduler::~OperationScheduler() {
  for (auto& active_operation : active_operations_) {
    if (active_operation.second.operation_future.valid()) {
      active_operation.second.operation_context.GetStopSource().request_stop();
      active_operation.second.operation_future.wait();
    }
  }
}

bool OperationScheduler::IsCancelled(const OperationData& operation_data) {
  return operation_data.operation_context.GetStopToken().stop_requested();
};

absl::StatusOr<google::longrunning::Operation> OperationScheduler::GetOperation(
    absl::string_view operation_name, OperationData& operation_data) {
  google::longrunning::Operation result_operation;
  result_operation.set_name(operation_name);

  INTR_ASSIGN_OR_RETURN(*result_operation.mutable_metadata(),
                        operation_data.CallStatus());

  // 'has_data' is true if the operation is still running or if the data has not
  // yet been retrieved.
  const bool has_data = operation_data.operation_future.valid();
  // 'data_is_ready' is true, if the data has not yet been retrieved, but can be
  // retrieved via .get() now (i.e. w/o waiting).
  const bool data_is_ready =
      has_data &&
      operation_data.operation_future.wait_for(std::chrono::milliseconds(10)) ==
          std::future_status::ready;
  // 'data_has_been_retrieved' is true, if the job has executed and if the data
  // has been retrieved before.
  const bool data_has_been_retrieved = !operation_data.operation_future.valid();

  // The operation may still be running even though it is cancelled. We set the
  // result respective error state in any case.
  if (IsCancelled(operation_data)) {
    *result_operation.mutable_error() =
        SaveStatusAsRpcStatus(absl::CancelledError(absl::StrFormat(
            "The Operation '%s' has been cancelled.", operation_name)));
    result_operation.set_done(true);
  } else if (data_is_ready || data_has_been_retrieved) {
    // This function can be called multiple times. While the future is still
    // valid, get and persist its result.
    if (!data_has_been_retrieved) {
      operation_data.future_result = operation_data.operation_future.get();
    }
    if (OperationResult result = operation_data.future_result.value();
        result.ok()) {
      *result_operation.mutable_response() = result.value();
    } else {
      *result_operation.mutable_error() =
          SaveStatusAsRpcStatus(result.status());
    }
    result_operation.set_done(true);
  } else {
    result_operation.set_done(false);
  }

  return result_operation;
}

absl::StatusOr<google::longrunning::Operation> OperationScheduler::AddOperation(
    absl::string_view operation_name,
    std::optional<google::protobuf::Any> custom_data,
    OperationScheduler::StatusFuncT<google::protobuf::Any> status_func,
    OperationScheduler::OperationFuncT<google::protobuf::Any> operation_func) {
  absl::MutexLock lock(mutex_);
  if (active_operations_.contains(operation_name)) {
    return absl::AlreadyExistsError(absl::StrCat(
        "Operation with the name '", operation_name, "' already exists"));
  }

  OperationContext operation_context;
  auto operation_future = std::async(
      std::launch::async, absl::bind_front(operation_func, operation_context));

  auto [itr, success] = active_operations_.emplace(
      operation_name, OperationData{
                          .operation_future = std::move(operation_future),
                          .custom_data = custom_data,
                          .start_time = absl::Now(),
                          .operation_context = std::move(operation_context),
                          .status_func = status_func,
                      });
  CHECK(success) << "Unable to add operation";

  INTR_ASSIGN_OR_RETURN(
      auto operation, GetOperation(operation_name, itr->second), _.LogError());
  return operation;
}

absl::StatusOr<google::longrunning::ListOperationsResponse>
OperationScheduler::ListOperations(
    const google::longrunning::ListOperationsRequest& list_operations_request) {
  if (!list_operations_request.name().empty()) {
    return absl::UnimplementedError(
        "Parent name of operations during ListOperations() is not supported.");
  }
  if (!list_operations_request.filter().empty()) {
    return absl::UnimplementedError(
        "Filtering of operations during ListOperations() is not supported.");
  }
  if (list_operations_request.page_size() != 0 ||
      !list_operations_request.page_token().empty()) {
    return absl::UnimplementedError(
        "Pagination of operations during ListOperations() is not supported.");
  }

  absl::MutexLock lock(mutex_);
  google::longrunning::ListOperationsResponse list_operations_response;
  for (auto& [operation_name, operation_data] : active_operations_) {
    INTR_ASSIGN_OR_RETURN(auto active_operation,
                          GetOperation(operation_name, operation_data));
    *list_operations_response.add_operations() = std::move(active_operation);
  }

  return std::move(list_operations_response);
}

absl::StatusOr<google::longrunning::Operation> OperationScheduler::GetOperation(
    const google::longrunning::GetOperationRequest& get_operation_request) {
  absl::MutexLock lock(mutex_);
  const auto& op_name = get_operation_request.name();
  auto itr = active_operations_.find(op_name);
  if (itr == active_operations_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("Operation '%s' not found.", op_name));
  }

  INTR_ASSIGN_OR_RETURN(auto get_operation_response,
                        GetOperation(op_name, itr->second), _.LogError());
  return std::move(get_operation_response);
}

absl::Status OperationScheduler::DeleteOperation(
    const google::longrunning::DeleteOperationRequest&
        delete_operation_request) {
  absl::MutexLock lock(mutex_);
  if (auto it = active_operations_.find(delete_operation_request.name());
      it != active_operations_.end()) {
    // The check below ensures that DeleteOperation() does not block by
    // skipping still running operations.
    const bool is_cancelled = IsCancelled(it->second);
    const bool data_has_been_retrieved = !it->second.operation_future.valid();
    if (data_has_been_retrieved || is_cancelled) {
      active_operations_.erase(it);
      return absl::OkStatus();
    } else {
      return absl::InvalidArgumentError(
          absl::StrFormat("The Operation '%s' is still running. Only cancelled "
                          "operations can be deleted.",
                          delete_operation_request.name()));
    }
  }

  return absl::NotFoundError(
      absl::StrFormat("Cannot delete unknown operation '%s'.",
                      delete_operation_request.name()));
}

absl::Status OperationScheduler::CancelOperation(
    const google::longrunning::CancelOperationRequest&
        cancel_operation_request) {
  absl::MutexLock lock(mutex_);
  if (auto it = active_operations_.find(cancel_operation_request.name());
      it != active_operations_.end()) {
    // If operation is finished, we don't need to cancel the operation future.
    // Note that this prevents side effects such as loosing operation results of
    // finished operations.

    // 'has_data' is true if the operation is still running or if the data has
    // not yet been retrieved.
    const bool has_data = it->second.operation_future.valid();
    // 'data_is_ready' is true, if the data has not yet been retrieved, but can
    // be retrieved via .get() now (i.e. w/o waiting).
    const bool data_is_ready = has_data && it->second.operation_future.wait_for(
                                               std::chrono::milliseconds(10)) ==
                                               std::future_status::ready;
    // 'data_has_been_retrieved' is true, if the job has executed and if the
    // data has been retrieved before.
    const bool data_has_been_retrieved = !it->second.operation_future.valid();

    if (!data_is_ready && !data_has_been_retrieved) {
      it->second.operation_context.GetStopSource().request_stop();
    }
    return absl::OkStatus();
  }

  return absl::NotFoundError(
      absl::StrFormat("Cannot cancel unknown operation '%s'.",
                      cancel_operation_request.name()));
}

absl::StatusOr<google::longrunning::Operation>
OperationScheduler::WaitOperation(
    const google::longrunning::WaitOperationRequest& request) {
  // The current structure of this scheduler means that the mutex guarding all
  // operations would need to be held for the duration of this call.  That would
  // block all requests in the meantime.  If someone wants to look at this in
  // the future, then we'd probably want to change this to have a mutex per
  // operation, such that we can await the done condition.  This is what is done
  // in the go operations library.
  return absl::UnimplementedError("WaitOperation is not implemented");
}

}  // namespace intrinsic::longrunning
