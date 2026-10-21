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

#ifndef INTRINSIC_LONGRUNNING_OPERATION_SCHEDULER_INTERFACE_H_
#define INTRINSIC_LONGRUNNING_OPERATION_SCHEDULER_INTERFACE_H_

#include <functional>
#include <optional>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/longrunning/cc/operation_context.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::longrunning {

// OperationSchedulerInterface is an interface that allows for managing long
// running operations through the super `OperationsProxy` class as well as
// extending it to allow for adding new operations.
class OperationSchedulerInterface : public OperationsProxy {
 public:
  ~OperationSchedulerInterface() override = default;

  // A lambda that can be called multiple times during the execution of the
  // operation to gauge the status of the operation and return that status as
  // a OperationMetadata type to be passed along to any monitoring process.
  //
  // Params are:
  //   absl::Time - start time of the operation
  //   std::optional<google::protobuf::Any> - custom data paassed in during
  //     creation
  //   OperationContext - the context of the operation, tracking progress.
  template <typename OperationMetadata>
  using StatusFuncT = std::function<absl::StatusOr<OperationMetadata>(
      absl::Time, std::optional<google::protobuf::Any>,
      const OperationContext&)>;

  // A lambda that runs a long running operation and returns either a status or
  // the result in the form of a OperationResult type.
  template <typename OperationResult>
  using OperationFuncT =
      std::function<absl::StatusOr<OperationResult>(OperationContext)>;

  // Adds a new operation to this scheduler with the given name, custom data,
  // context, and lambdas. If the operation is successfully added it will return
  // an Operation proto, or a status in the event of a failure to add.
  // The custom data will be maintained for later calls to the StatusFunc
  // lambda. The OperationFunc is the main lambda that will be run
  // in the scheduler to generate the results for the given long running
  // operation.
  virtual absl::StatusOr<google::longrunning::Operation> AddOperation(
      absl::string_view operation_name,
      std::optional<google::protobuf::Any> custom_data,
      StatusFuncT<google::protobuf::Any> status_func,
      OperationFuncT<google::protobuf::Any> operation_func) = 0;

  // A wrapper around the schedulers AddOperation call that converts from the
  // given template types to Any protos. It also defers the status update to the
  // GetOperationStatus methods.
  template <typename OperationMetadata, typename OperationResult>
  absl::StatusOr<google::longrunning::Operation> AddOperationWrapped(
      absl::string_view operation_name,
      std::optional<google::protobuf::Any> custom_data,
      StatusFuncT<OperationMetadata> status_func,
      OperationFuncT<OperationResult> result_func);
};

// -----------------------------
// Implementation details below.
// -----------------------------

template <typename OperationMetadata, typename OperationResult>
absl::StatusOr<google::longrunning::Operation>
OperationSchedulerInterface::AddOperationWrapped(
    absl::string_view operation_name,
    std::optional<google::protobuf::Any> custom_data,
    StatusFuncT<OperationMetadata> status_func,
    OperationFuncT<OperationResult> result_func) {
  OperationFuncT<google::protobuf::Any> outer_result_func =
      [result_func](
          OperationContext context) -> absl::StatusOr<google::protobuf::Any> {
    INTR_ASSIGN_OR_RETURN(OperationResult op_result,
                          result_func(std::move(context)));

    google::protobuf::Any proto_result;
    if (!proto_result.PackFrom(op_result)) {
      return absl::InternalError("Failed to pack operation result proto.");
    }
    return proto_result;
  };

  StatusFuncT<google::protobuf::Any> outer_status_func =
      [status_func](absl::Time start_time,
                    std::optional<google::protobuf::Any> custom_data,
                    const OperationContext& operation_context)
      -> absl::StatusOr<google::protobuf::Any> {
    INTR_ASSIGN_OR_RETURN(
        OperationMetadata op_status,
        status_func(start_time, custom_data, operation_context));

    google::protobuf::Any proto_status;
    if (!proto_status.PackFrom(op_status)) {
      return absl::InternalError("Failed to pack operation status proto.");
    }
    return proto_status;
  };

  return AddOperation(operation_name, custom_data, std::move(outer_status_func),
                      std::move(outer_result_func));
}

}  // namespace intrinsic::longrunning

#endif  // INTRINSIC_LONGRUNNING_OPERATION_SCHEDULER_INTERFACE_H_
