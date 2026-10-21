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

#include "intrinsic/util/status/status_builder_grpc.h"

#include "absl/status/status.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/status/status_builder.h"

namespace intrinsic {

StatusBuilderGrpc AbortedErrorBuilderGrpc(intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(StatusBuilder(absl::StatusCode::kAborted, location));
}

StatusBuilderGrpc AlreadyExistsErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kAlreadyExists, location));
}

StatusBuilderGrpc CancelledErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kCancelled, location));
}

StatusBuilderGrpc DataLossErrorBuilderGrpc(intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kDataLoss, location));
}

StatusBuilderGrpc DeadlineExceededErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kDeadlineExceeded, location));
}

StatusBuilderGrpc FailedPreconditionErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kFailedPrecondition, location));
}

StatusBuilderGrpc InternalErrorBuilderGrpc(intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kInternal, location));
}

StatusBuilderGrpc InvalidArgumentErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kInvalidArgument, location));
}

StatusBuilderGrpc NotFoundErrorBuilderGrpc(intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kNotFound, location));
}

StatusBuilderGrpc OutOfRangeErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kOutOfRange, location));
}

StatusBuilderGrpc PermissionDeniedErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kPermissionDenied, location));
}

StatusBuilderGrpc UnauthenticatedErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kUnauthenticated, location));
}

StatusBuilderGrpc ResourceExhaustedErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kResourceExhausted, location));
}

StatusBuilderGrpc UnavailableErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kUnavailable, location));
}

StatusBuilderGrpc UnimplementedErrorBuilderGrpc(
    intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(
      StatusBuilder(absl::StatusCode::kUnimplemented, location));
}

StatusBuilderGrpc UnknownErrorBuilderGrpc(intrinsic::SourceLocation location) {
  return StatusBuilderGrpc(StatusBuilder(absl::StatusCode::kUnknown, location));
}

}  // namespace intrinsic
