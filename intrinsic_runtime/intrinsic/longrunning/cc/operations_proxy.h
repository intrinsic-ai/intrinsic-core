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

#ifndef INTRINSIC_LONGRUNNING_OPERATIONS_PROXY_H_
#define INTRINSIC_LONGRUNNING_OPERATIONS_PROXY_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/longrunning/operations.pb.h"

namespace intrinsic::longrunning {

// A wrapper around an Operations style service. It allows the
// OperationsScheduler and other users to talk about long running operations
// services in a manner that does not require grpc. Useful for when they are
// external to the Operations service RPC services that also maintain long
// running operations. Those can be integrated into a single view using this
// shared proxy interface.
//
// See cs/google/longrunning/operations.proto for more details about the service
// interface.
class OperationsProxy {
 public:
  virtual ~OperationsProxy() = default;

  // Similar to the google.longrunning.Operations::ListOperations
  virtual absl::StatusOr<google::longrunning::ListOperationsResponse>
  ListOperations(const google::longrunning::ListOperationsRequest&
                     list_operations_request) = 0;

  // Similar to the google.longrunning.Operations::GetOperation
  virtual absl::StatusOr<google::longrunning::Operation> GetOperation(
      const google::longrunning::GetOperationRequest&
          get_operation_request) = 0;

  // Similar to the google.longrunning.Operations::DeleteOperation
  virtual absl::Status DeleteOperation(
      const google::longrunning::DeleteOperationRequest&
          delete_operation_request) = 0;

  // Similar to the google.longrunning.Operations::CancelOperation
  virtual absl::Status CancelOperation(
      const google::longrunning::CancelOperationRequest&
          cancel_operation_request) = 0;

  // Similar to the google.longrunning.Operations::WaitOperation
  virtual absl::StatusOr<google::longrunning::Operation> WaitOperation(
      const google::longrunning::WaitOperationRequest& request) = 0;
};

}  // namespace intrinsic::longrunning

#endif  // INTRINSIC_LONGRUNNING_OPERATIONS_PROXY_H_
