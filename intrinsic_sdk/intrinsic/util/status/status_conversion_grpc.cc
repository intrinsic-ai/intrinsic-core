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

#include "intrinsic/util/status/status_conversion_grpc.h"

#include <cstdint>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "google/rpc/code.pb.h"
#include "google/rpc/status.pb.h"
#include "grpcpp/support/status.h"
#include "intrinsic/util/status/status_conversion_rpc.h"

namespace intrinsic {

namespace {
static grpc::StatusCode FromAbslStatusCode(const absl::StatusCode& absl_code) {
  switch (absl_code) {
    case absl::StatusCode::kOk:
      return grpc::StatusCode::OK;
    case absl::StatusCode::kCancelled:
      return grpc::StatusCode::CANCELLED;
    case absl::StatusCode::kUnknown:
      return grpc::StatusCode::UNKNOWN;
    case absl::StatusCode::kInvalidArgument:
      return grpc::StatusCode::INVALID_ARGUMENT;
    case absl::StatusCode::kDeadlineExceeded:
      return grpc::StatusCode::DEADLINE_EXCEEDED;
    case absl::StatusCode::kNotFound:
      return grpc::StatusCode::NOT_FOUND;
    case absl::StatusCode::kAlreadyExists:
      return grpc::StatusCode::ALREADY_EXISTS;
    case absl::StatusCode::kPermissionDenied:
      return grpc::StatusCode::PERMISSION_DENIED;
    case absl::StatusCode::kResourceExhausted:
      return grpc::StatusCode::RESOURCE_EXHAUSTED;
    case absl::StatusCode::kFailedPrecondition:
      return grpc::StatusCode::FAILED_PRECONDITION;
    case absl::StatusCode::kAborted:
      return grpc::StatusCode::ABORTED;
    case absl::StatusCode::kOutOfRange:
      return grpc::StatusCode::OUT_OF_RANGE;
    case absl::StatusCode::kUnimplemented:
      return grpc::StatusCode::UNIMPLEMENTED;
    case absl::StatusCode::kInternal:
      return grpc::StatusCode::INTERNAL;
    case absl::StatusCode::kUnavailable:
      return grpc::StatusCode::UNAVAILABLE;
    case absl::StatusCode::kDataLoss:
      return grpc::StatusCode::DATA_LOSS;
    case absl::StatusCode::kUnauthenticated:
      return grpc::StatusCode::UNAUTHENTICATED;
    default:
      LOG(ERROR) << "Unknown absl::StatusCode: " << absl_code
                 << ". Falling back to UNKNOWN.";
      return grpc::StatusCode::UNKNOWN;
  }
}

static absl::StatusCode ToAbslStatusCode(const grpc::StatusCode& code) {
  switch (code) {
    case grpc::StatusCode::OK:
      return absl::StatusCode::kOk;
    case grpc::StatusCode::CANCELLED:
      return absl::StatusCode::kCancelled;
    case grpc::StatusCode::UNKNOWN:
      return absl::StatusCode::kUnknown;
    case grpc::StatusCode::INVALID_ARGUMENT:
      return absl::StatusCode::kInvalidArgument;
    case grpc::StatusCode::DEADLINE_EXCEEDED:
      return absl::StatusCode::kDeadlineExceeded;
    case grpc::StatusCode::NOT_FOUND:
      return absl::StatusCode::kNotFound;
    case grpc::StatusCode::ALREADY_EXISTS:
      return absl::StatusCode::kAlreadyExists;
    case grpc::StatusCode::PERMISSION_DENIED:
      return absl::StatusCode::kPermissionDenied;
    case grpc::StatusCode::RESOURCE_EXHAUSTED:
      return absl::StatusCode::kResourceExhausted;
    case grpc::StatusCode::FAILED_PRECONDITION:
      return absl::StatusCode::kFailedPrecondition;
    case grpc::StatusCode::ABORTED:
      return absl::StatusCode::kAborted;
    case grpc::StatusCode::OUT_OF_RANGE:
      return absl::StatusCode::kOutOfRange;
    case grpc::StatusCode::UNIMPLEMENTED:
      return absl::StatusCode::kUnimplemented;
    case grpc::StatusCode::INTERNAL:
      return absl::StatusCode::kInternal;
    case grpc::StatusCode::UNAVAILABLE:
      return absl::StatusCode::kUnavailable;
    case grpc::StatusCode::DATA_LOSS:
      return absl::StatusCode::kDataLoss;
    case grpc::StatusCode::UNAUTHENTICATED:
      return absl::StatusCode::kUnauthenticated;
    default:
      LOG(ERROR) << "Unknown grpc::StatusCode: " << code
                 << ". Falling back to kUnknown.";
      return absl::StatusCode::kUnknown;
  }
}

static grpc::StatusCode FromRpcCode(int32_t code) {
  switch (code) {
    case google::rpc::Code::OK:
      return grpc::StatusCode::OK;
    case google::rpc::Code::CANCELLED:
      return grpc::StatusCode::CANCELLED;
    case google::rpc::Code::UNKNOWN:
      return grpc::StatusCode::UNKNOWN;
    case google::rpc::Code::INVALID_ARGUMENT:
      return grpc::StatusCode::INVALID_ARGUMENT;
    case google::rpc::Code::DEADLINE_EXCEEDED:
      return grpc::StatusCode::DEADLINE_EXCEEDED;
    case google::rpc::Code::NOT_FOUND:
      return grpc::StatusCode::NOT_FOUND;
    case google::rpc::Code::ALREADY_EXISTS:
      return grpc::StatusCode::ALREADY_EXISTS;
    case google::rpc::Code::PERMISSION_DENIED:
      return grpc::StatusCode::PERMISSION_DENIED;
    case google::rpc::Code::RESOURCE_EXHAUSTED:
      return grpc::StatusCode::RESOURCE_EXHAUSTED;
    case google::rpc::Code::FAILED_PRECONDITION:
      return grpc::StatusCode::FAILED_PRECONDITION;
    case google::rpc::Code::ABORTED:
      return grpc::StatusCode::ABORTED;
    case google::rpc::Code::OUT_OF_RANGE:
      return grpc::StatusCode::OUT_OF_RANGE;
    case google::rpc::Code::UNIMPLEMENTED:
      return grpc::StatusCode::UNIMPLEMENTED;
    case google::rpc::Code::INTERNAL:
      return grpc::StatusCode::INTERNAL;
    case google::rpc::Code::UNAVAILABLE:
      return grpc::StatusCode::UNAVAILABLE;
    case google::rpc::Code::DATA_LOSS:
      return grpc::StatusCode::DATA_LOSS;
    case google::rpc::Code::UNAUTHENTICATED:
      return grpc::StatusCode::UNAUTHENTICATED;
    default:
      LOG(ERROR) << "Unknown google::rpc::Code: " << code
                 << ". Falling back to UNKNOWN.";
      return grpc::StatusCode::UNKNOWN;
  }
}
}  // end namespace

grpc::Status ToGrpcStatus(const google::rpc::Status& status) {
  if (status.code() == google::rpc::Code::OK) {
    return grpc::Status::OK;
  }

  return grpc::Status(FromRpcCode(status.code()), status.message(),
                      status.SerializeAsString());
}

grpc::Status ToGrpcStatus(const absl::Status& status) {
  if (status.ok()) {
    return grpc::Status::OK;
  }

  return grpc::Status(FromAbslStatusCode(status.code()),
                      std::string(status.message()),
                      ToGoogleRpcStatus(status).SerializeAsString());
}

absl::Status ToAbslStatus(const grpc::Status& status) {
  if (status.ok()) {
    return absl::OkStatus();
  }

  absl::Status status_with_payloads;

  if (!status.error_details().empty()) {
    google::rpc::Status status_proto;
    if (status_proto.ParseFromString(status.error_details())) {
      return ToAbslStatusWithPayloads(
          status_proto, /*copy_payloads_from=*/status_with_payloads);
    } else {
      LOG(ERROR) << "Failed to parse error_details to google::rpc::Status";
    }
  }

  return absl::Status(ToAbslStatusCode(status.error_code()),
                      status.error_message());
}

}  // namespace intrinsic
