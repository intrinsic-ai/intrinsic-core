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

#ifndef INTRINSIC_LONGRUNNING_GRPC_OPERATIONS_PROXY_H_
#define INTRINSIC_LONGRUNNING_GRPC_OPERATIONS_PROXY_H_

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "google/longrunning/operations.pb.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"

namespace intrinsic::longrunning {

// A generic implementation of a OperationsProxy that talks to a remote
// service that implements the google.longrunning.Operations interface.
class GrpcOperationsProxy : public OperationsProxy {
 public:
  explicit GrpcOperationsProxy(
      std::unique_ptr<google::longrunning::Operations::StubInterface>
          operations_service_stub);

  static absl::StatusOr<
      std::unique_ptr<google::longrunning::Operations::StubInterface>>
  CreateStubFromAddress(absl::string_view address, absl::Duration timeout,
                        bool use_default_application_credentials);

  absl::StatusOr<google::longrunning::ListOperationsResponse> ListOperations(
      const google::longrunning::ListOperationsRequest& list_operations_request)
      override;

  absl::StatusOr<google::longrunning::Operation> GetOperation(
      const google::longrunning::GetOperationRequest& get_operation_request)
      override;

  absl::Status DeleteOperation(
      const google::longrunning::DeleteOperationRequest&
          delete_operation_request) override;

  absl::Status CancelOperation(
      const google::longrunning::CancelOperationRequest&
          cancel_operation_request) override;

  absl::StatusOr<google::longrunning::Operation> WaitOperation(
      const google::longrunning::WaitOperationRequest& request) override;

 private:
  std::unique_ptr<google::longrunning::Operations::StubInterface>
      operations_service_stub_;
};

// Returns an implementation of an OperationsProxy that, unlike
// GrpcOperationsProxy tolerates the operations service at the address being
// unavailable.  It will behave as if it has no operations if the unavailable.
// This also means that incorrect addresses be silently hidden.
absl::StatusOr<std::unique_ptr<OperationsProxy>> CreateTransientGrpcProxy(
    absl::string_view address);

// Creates a transient proxy from an existing stub.  See above for details.
std::unique_ptr<OperationsProxy> CreateTransientGrpcProxy(
    std::unique_ptr<google::longrunning::Operations::StubInterface>
        service_stub);

}  // namespace intrinsic::longrunning

#endif  // INTRINSIC_LONGRUNNING_GRPC_OPERATIONS_PROXY_H_
