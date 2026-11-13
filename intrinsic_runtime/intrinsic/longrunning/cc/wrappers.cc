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

#include "intrinsic/longrunning/cc/wrappers.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::longrunning::wrap_internal {
namespace {

template <typename ProtoT>
absl::StatusOr<ProtoT> MaybeUpdateOperationName(absl::string_view prefix,
                                                const ProtoT& input) {
  absl::string_view operation_name = input.name();
  if (!absl::ConsumePrefix(&operation_name, prefix)) {
    return absl::NotFoundError(
        absl::StrCat("Could not find Operation '", operation_name, "'"));
  }

  ProtoT updated_input = input;
  updated_input.set_name(operation_name);
  return updated_input;
}

class WrappedOperationSchedulerInterface : public OperationSchedulerInterface {
 public:
  WrappedOperationSchedulerInterface(
      absl::string_view prefix,
      std::shared_ptr<OperationSchedulerInterface> wrapped_scheduler)
      : full_prefix_(absl::StrCat(prefix, "/")),
        wrapped_scheduler_(std::move(wrapped_scheduler)) {}

  absl::StatusOr<google::longrunning::Operation> AddOperation(
      absl::string_view operation_name,
      std::optional<google::protobuf::Any> custom_data,
      StatusFuncT<google::protobuf::Any> status_func,
      OperationFuncT<google::protobuf::Any> operation_func) override {
    INTR_ASSIGN_OR_RETURN(auto result, wrapped_scheduler_->AddOperation(
                                           operation_name, custom_data,
                                           status_func, operation_func));
    result.set_name(absl::StrCat(full_prefix_, result.name()));
    return result;
  }

  absl::StatusOr<google::longrunning::ListOperationsResponse> ListOperations(
      const google::longrunning::ListOperationsRequest& list_operations_request)
      override {
    INTR_ASSIGN_OR_RETURN(auto operations, wrapped_scheduler_->ListOperations(
                                               list_operations_request));
    for (auto& operation : *operations.mutable_operations()) {
      operation.set_name(absl::StrCat(full_prefix_, operation.name()));
    }
    return operations;
  }

  absl::StatusOr<google::longrunning::Operation> GetOperation(
      const google::longrunning::GetOperationRequest& get_operation_request)
      override {
    INTR_ASSIGN_OR_RETURN(
        auto updated_get_operation_request,
        MaybeUpdateOperationName(full_prefix_, get_operation_request));
    INTR_ASSIGN_OR_RETURN(
        auto inner_result,
        wrapped_scheduler_->GetOperation(updated_get_operation_request));

    inner_result.set_name(absl::StrCat(full_prefix_, inner_result.name()));
    return inner_result;
  }

  absl::Status DeleteOperation(
      const google::longrunning::DeleteOperationRequest&
          delete_operation_request) override {
    INTR_ASSIGN_OR_RETURN(
        auto updated_delete_operation_request,
        MaybeUpdateOperationName(full_prefix_, delete_operation_request));
    return wrapped_scheduler_->DeleteOperation(
        updated_delete_operation_request);
  }

  absl::Status CancelOperation(
      const google::longrunning::CancelOperationRequest&
          cancel_operation_request) override {
    INTR_ASSIGN_OR_RETURN(
        auto updated_cancel_operation_request,
        MaybeUpdateOperationName(full_prefix_, cancel_operation_request));
    return wrapped_scheduler_->CancelOperation(
        updated_cancel_operation_request);
  }

  absl::StatusOr<google::longrunning::Operation> WaitOperation(
      const google::longrunning::WaitOperationRequest& request) override {
    INTR_ASSIGN_OR_RETURN(auto updated_request,
                          MaybeUpdateOperationName(full_prefix_, request));
    INTR_ASSIGN_OR_RETURN(auto inner_result,
                          wrapped_scheduler_->WaitOperation(updated_request));

    inner_result.set_name(absl::StrCat(full_prefix_, inner_result.name()));
    return inner_result;
  }

 private:
  std::string full_prefix_;
  std::shared_ptr<OperationSchedulerInterface> wrapped_scheduler_;
};

}  // namespace

absl::StatusOr<std::shared_ptr<OperationSchedulerInterface>>
WrapSchedulerWithPrefix(
    absl::string_view prefix,
    std::shared_ptr<OperationSchedulerInterface> scheduler) {
  if (scheduler == nullptr) {
    return absl::InvalidArgumentError(
        "OperationSchedulerInterface cannot be nullptr");
  }

  // Don't bother wrapping it if we have an empty prefix
  if (prefix.empty()) {
    return std::move(scheduler);
  }

  return std::make_shared<WrappedOperationSchedulerInterface>(prefix,
                                                              scheduler);
}

}  // namespace intrinsic::longrunning::wrap_internal
