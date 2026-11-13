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

#ifndef INTRINSIC_LONGRUNNING_OPERATIONS_SERVICE_IMPL_H_
#define INTRINSIC_LONGRUNNING_OPERATIONS_SERVICE_IMPL_H_

#include <memory>

#include "google/longrunning/operations.grpc.pb.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"

namespace intrinsic::longrunning {

// A generic implementation of the google longrunning Operations service. It
// defers the majority of its implementation to the given OperationScheduler
// and the OperationsProxy instances.
class OperationsServiceImpl final
    : public google::longrunning::Operations::Service {
 public:
  explicit OperationsServiceImpl(std::shared_ptr<OperationsProxy> proxy);
  ~OperationsServiceImpl() override = default;

  ::grpc::Status ListOperations(
      ::grpc::ServerContext* context,
      const google::longrunning::ListOperationsRequest* list_operations_request,
      google::longrunning::ListOperationsResponse* list_operations_response)
      override;

  ::grpc::Status GetOperation(
      ::grpc::ServerContext* context,
      const google::longrunning::GetOperationRequest* get_operation_request,
      google::longrunning::Operation* get_operation_response) override;

  ::grpc::Status DeleteOperation(
      ::grpc::ServerContext* context,
      const google::longrunning::DeleteOperationRequest*
          delete_operation_request,
      google::protobuf::Empty* delete_operation_response) override;

  ::grpc::Status CancelOperation(
      ::grpc::ServerContext* context,
      const google::longrunning::CancelOperationRequest*
          cancel_operation_request,
      google::protobuf::Empty* cancel_operation_response) override;

  ::grpc::Status WaitOperation(
      ::grpc::ServerContext* context,
      const google::longrunning::WaitOperationRequest* request,
      google::longrunning::Operation* response) override;

 private:
  // The underlying data being represented by this service is pulled from the
  // proxy provided during construction.
  std::shared_ptr<OperationsProxy> proxy_;
};

}  // namespace intrinsic::longrunning

#endif  // INTRINSIC_LONGRUNNING_OPERATIONS_SERVICE_IMPL_H_
