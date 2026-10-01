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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CC_OPERATION_ERROR_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CC_OPERATION_ERROR_H_

#include <string>
#include <string_view>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/util/status/extended_status.pb.h"

namespace intrinsic::executive {

// Returns the messages of all error facts in the environment.
//
// These are errors that have not been migrated to ExtendedStatus, yet.
//
// Args:
//   message_only: Only return the message, instead of a combination of the
//     error's name, message and type.
//   fatal_only: Only consider errors of type FATAL.
//
// TODO(b/381078902): Remove this function once the remaining error facts have
// been migrated to ExtendedStatus or are reported through a dedicated channel.
std::vector<std::string> GetClipsLegacyErrorMessages(
    clips::Environment* absl_nonnull env, bool message_only = false,
    bool fatal_only = false) ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

// Adds the messages of all error facts as context to the given
// ExtendedStatus.
//
// These are errors that have not been migrated to ExtendedStatus, yet.
void AddExtendedStatusLegacyErrors(clips::Environment* absl_nonnull env,
                                   intrinsic_proto::status::ExtendedStatus& es)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

// Retrieves the ExtendedStatus of the operation-envelope of the given
// operation, as-is.
absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
GetOperationExtendedStatus(clips::Environment* absl_nonnull env,
                           clips::ProtobufManager* absl_nonnull proto_mgr,
                           std::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

// As GetOperationExtendedStatus, but with the errors facts added as context.
absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
BuildOperationExtendedStatusWithLegacyErrors(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    std::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

// Sets the error field of the given Operation proto from the ExtendedStatus of
// the given operation.
//
// The error message is taken from the extended status' title, or its user
// report message if there is no title. The full extended status is added as
// detail.
//
// An error is always set, even if no extended status can be retrieved, so that
// an Operation is never reported as done without a reason.
//
// Exposed in CLIPS as operation-proto-set-error.
void SetOperationProtoError(clips::Environment* absl_nonnull env,
                            clips::ProtobufManager* absl_nonnull proto_mgr,
                            clips::ProtoMessageId operation_proto_id,
                            std::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

// Adds the operation error functions to the CLIPS environment.
// The ProtobufManager must be available as long as the functions are registered
// in the environment.
absl::Status AddClipsOperationErrorFunctions(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex());

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CC_OPERATION_ERROR_H_
