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

#include "intrinsic/longrunning/cc/grpc_operations_proxy.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/log/die_if_null.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.grpc.pb.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/support/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/longrunning/cc/operations_proxy.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

const absl::Duration kServiceGrpcMaxDuration = absl::Seconds(30);

namespace intrinsic::longrunning {
namespace {
absl::Status CoerceUnavailableToOk(absl::Status status) {
  if (absl::IsUnavailable(status)) {
    return absl::OkStatus();
  }
  return status;
}

absl::Status CoerceUnavailableToNotFound(absl::Status status) {
  if (absl::IsUnavailable(status)) {
    return absl::NotFoundError(status.message());
  }
  return status;
}

}  // namespace

GrpcOperationsProxy::GrpcOperationsProxy(
    std::unique_ptr<google::longrunning::Operations::StubInterface>
        operations_service_stub)
    : operations_service_stub_(
          std::move(ABSL_DIE_IF_NULL(operations_service_stub))) {}

absl::StatusOr<std::unique_ptr<google::longrunning::Operations::StubInterface>>
GrpcOperationsProxy::CreateStubFromAddress(
    absl::string_view address, absl::Duration timeout,
    bool use_default_application_credentials) {
  if (address.empty()) {
    // Returns nullptr because the address is not available, so this acts as
    // if the service is disabled. We don't need to create a proxy for this
    // target.
    return nullptr;
  }

  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<grpc::Channel> channel,
      connect::CreateClientChannel(address, absl::Now() + timeout,
                                   connect::DefaultGrpcChannelArgs(),
                                   use_default_application_credentials));

  auto service_stub =
      google::longrunning::Operations::NewStub(std::move(channel));
  if (!service_stub) {
    return absl::InternalError("Couldn't create stub for operations service.");
  }

  return std::move(service_stub);
}

absl::StatusOr<google::longrunning::ListOperationsResponse>
GrpcOperationsProxy::ListOperations(
    const google::longrunning::ListOperationsRequest& list_operations_request) {
  grpc::ClientContext context;
  context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

  google::longrunning::ListOperationsResponse list_operations_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(operations_service_stub_->ListOperations(
      &context, list_operations_request, &list_operations_response)));

  return list_operations_response;
}

absl::StatusOr<google::longrunning::Operation>
GrpcOperationsProxy::GetOperation(
    const google::longrunning::GetOperationRequest& get_operation_request) {
  grpc::ClientContext context;
  context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

  google::longrunning::Operation get_operation_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(operations_service_stub_->GetOperation(
      &context, get_operation_request, &get_operation_response)));

  return get_operation_response;
}

absl::Status GrpcOperationsProxy::DeleteOperation(
    const google::longrunning::DeleteOperationRequest&
        delete_operation_request) {
  grpc::ClientContext context;
  context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

  google::protobuf::Empty delete_operation_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(operations_service_stub_->DeleteOperation(
      &context, delete_operation_request, &delete_operation_response)));

  return absl::OkStatus();
}

absl::Status GrpcOperationsProxy::CancelOperation(
    const google::longrunning::CancelOperationRequest&
        cancel_operation_request) {
  grpc::ClientContext context;
  context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

  google::protobuf::Empty cancel_operation_response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(operations_service_stub_->CancelOperation(
      &context, cancel_operation_request, &cancel_operation_response)));

  return absl::OkStatus();
}

absl::StatusOr<google::longrunning::Operation>
GrpcOperationsProxy::WaitOperation(
    const google::longrunning::WaitOperationRequest& request) {
  grpc::ClientContext context;
  context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

  google::longrunning::Operation response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      operations_service_stub_->WaitOperation(&context, request, &response)));

  return response;
}

class TransientGrpcOperationsProxy : public OperationsProxy {
 public:
  explicit TransientGrpcOperationsProxy(
      std::unique_ptr<google::longrunning::Operations::StubInterface>
          operations_service_stub)
      : operations_service_stub_(
            std::move(ABSL_DIE_IF_NULL(operations_service_stub))) {}

  absl::StatusOr<google::longrunning::ListOperationsResponse> ListOperations(
      const google::longrunning::ListOperationsRequest& list_operations_request)
      override {
    grpc::ClientContext context;
    context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

    google::longrunning::ListOperationsResponse list_operations_response;
    // Translate unavailable as not running, therefore having no operations, so
    // returning the empty list message is acceptable.
    INTR_RETURN_IF_ERROR(CoerceUnavailableToOk(
        ToAbslStatus(operations_service_stub_->ListOperations(
            &context, list_operations_request, &list_operations_response))));

    return list_operations_response;
  }

  absl::StatusOr<google::longrunning::Operation> GetOperation(
      const google::longrunning::GetOperationRequest& get_operation_request)
      override {
    grpc::ClientContext context;
    context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

    google::longrunning::Operation get_operation_response;
    // Translate unavailable as not running, therefore having no operations.
    INTR_RETURN_IF_ERROR(CoerceUnavailableToNotFound(
        ToAbslStatus(operations_service_stub_->GetOperation(
            &context, get_operation_request, &get_operation_response))));

    return get_operation_response;
  }

  absl::Status DeleteOperation(
      const google::longrunning::DeleteOperationRequest&
          delete_operation_request) override {
    grpc::ClientContext context;
    context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

    google::protobuf::Empty delete_operation_response;
    // Translate unavailable as not running, therefore having no operations.
    INTR_RETURN_IF_ERROR(CoerceUnavailableToNotFound(
        ToAbslStatus(operations_service_stub_->DeleteOperation(
            &context, delete_operation_request, &delete_operation_response))));

    return absl::OkStatus();
  }

  absl::Status CancelOperation(
      const google::longrunning::CancelOperationRequest&
          cancel_operation_request) override {
    grpc::ClientContext context;
    context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

    google::protobuf::Empty cancel_operation_response;
    // Translate unavailable as not running, therefore having no operations.
    INTR_RETURN_IF_ERROR(CoerceUnavailableToNotFound(
        ToAbslStatus(operations_service_stub_->CancelOperation(
            &context, cancel_operation_request, &cancel_operation_response))));

    return absl::OkStatus();
  }

  absl::StatusOr<google::longrunning::Operation> WaitOperation(
      const google::longrunning::WaitOperationRequest& request) override {
    grpc::ClientContext context;
    context.set_deadline(::grpc::DeadlineFromDuration(kServiceGrpcMaxDuration));

    google::longrunning::Operation response;
    // Translate unavailable as not running, therefore having no operations.
    INTR_RETURN_IF_ERROR(CoerceUnavailableToNotFound(
        ToAbslStatus(operations_service_stub_->WaitOperation(&context, request,
                                                             &response))));

    return response;
  }

 private:
  std::unique_ptr<google::longrunning::Operations::StubInterface>
      operations_service_stub_;
};

std::unique_ptr<OperationsProxy> CreateTransientGrpcProxy(
    std::unique_ptr<google::longrunning::Operations::StubInterface>
        service_stub) {
  return std::make_unique<TransientGrpcOperationsProxy>(
      std::move(service_stub));
}

absl::StatusOr<std::unique_ptr<OperationsProxy>> CreateTransientGrpcProxy(
    absl::string_view address) {
  // For the transient channel, we don't use the intrinsic gRPC libraries, as
  // those have logic to wait for the start of a connection and check its
  // health. That's exactly what we don't want to do here, we we construct the
  // channel directly.
  std::shared_ptr<grpc::Channel> channel = ::grpc::CreateCustomChannel(
      std::string(address),
      ::grpc::                       // NOLINTNEXTLINE
      InsecureChannelCredentials(),  // NO_LINT(grpc_insecure_credential_linter)
      connect::DefaultGrpcChannelArgs());

  auto service_stub =
      google::longrunning::Operations::NewStub(std::move(channel));
  if (!service_stub) {
    return absl::InternalError("Couldn't create stub for operations service.");
  }

  return std::make_unique<TransientGrpcOperationsProxy>(
      std::move(service_stub));
}

}  // namespace intrinsic::longrunning
