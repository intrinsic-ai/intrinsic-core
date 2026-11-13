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

#include "intrinsic/longrunning/cc/operations_service_impl.h"

#include <memory>

#include "absl/log/die_if_null.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"
#include "intrinsic/util/status/status_macros_grpc.h"

namespace intrinsic::longrunning {

OperationsServiceImpl::OperationsServiceImpl(
    std::shared_ptr<OperationsProxy> proxy)
    : proxy_(ABSL_DIE_IF_NULL(proxy)) {}

grpc::Status OperationsServiceImpl::ListOperations(
    grpc::ServerContext* context,
    const google::longrunning::ListOperationsRequest* list_operations_request,
    google::longrunning::ListOperationsResponse* list_operations_response) {
  INTR_ASSIGN_OR_RETURN_GRPC(*list_operations_response,
                             proxy_->ListOperations(*list_operations_request),
                             _.LogError());
  return grpc::Status::OK;
}

grpc::Status OperationsServiceImpl::GetOperation(
    grpc::ServerContext* context,
    const google::longrunning::GetOperationRequest* get_operation_request,
    google::longrunning::Operation* get_operation_response) {
  INTR_ASSIGN_OR_RETURN_GRPC(*get_operation_response,
                             proxy_->GetOperation(*get_operation_request),
                             _.LogError());
  return grpc::Status::OK;
}

grpc::Status OperationsServiceImpl::DeleteOperation(
    grpc::ServerContext* context,
    const google::longrunning::DeleteOperationRequest* delete_operation_request,
    google::protobuf::Empty* delete_operation_response) {
  INTR_RETURN_IF_ERROR_GRPC(proxy_->DeleteOperation(*delete_operation_request))
      .LogError();
  return grpc::Status::OK;
}

grpc::Status OperationsServiceImpl::CancelOperation(
    grpc::ServerContext* context,
    const google::longrunning::CancelOperationRequest* cancel_operation_request,
    google::protobuf::Empty* cancel_operation_response) {
  INTR_RETURN_IF_ERROR_GRPC(proxy_->CancelOperation(*cancel_operation_request))
      .LogError();
  return grpc::Status::OK;
}

grpc::Status OperationsServiceImpl::WaitOperation(
    grpc::ServerContext* context,
    const google::longrunning::WaitOperationRequest* request,
    google::longrunning::Operation* response) {
  INTR_ASSIGN_OR_RETURN_GRPC(*response, proxy_->WaitOperation(*request),
                             _.LogError());
  return grpc::Status::OK;
}

}  // namespace intrinsic::longrunning
