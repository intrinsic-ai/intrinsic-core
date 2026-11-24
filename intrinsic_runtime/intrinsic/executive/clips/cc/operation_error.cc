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

#include "intrinsic/executive/clips/cc/operation_error.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "google/longrunning/operations.pb.h"
#include "google/rpc/status.pb.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"

namespace intrinsic::executive {
namespace {

// Status code of the ExtendedStatus that carries a legacy (error) fact.
constexpr int kLegacyErrorStatusCode = 13020;
constexpr char kOperationProtoSetErrorFunction[] = "operation-proto-set-error";

}  // namespace

std::vector<std::string> GetClipsLegacyErrorMessages(
    clips::Environment* absl_nonnull env, bool message_only, bool fatal_only) {
  std::vector<std::string> error_msgs;
  std::vector<clips::SlotValue> type_constraint;
  if (fatal_only) {
    type_constraint.emplace_back(
        clips::SlotValue("type", clips::Symbol("FATAL")));
  }
  std::vector<clips::Fact> facts = env->QueryFacts("error", type_constraint);
  for (const clips::Fact& fact : facts) {
    absl::StatusOr<clips::Value> status_or_name_val = fact.GetSlotValue("name");
    absl::StatusOr<clips::Value> status_or_type_val = fact.GetSlotValue("type");
    absl::StatusOr<clips::Value> status_or_message_val =
        fact.GetSlotValue("message");
    if (!status_or_type_val.ok() || !status_or_message_val.ok() ||
        !status_or_name_val.ok()) {
      LOG(WARNING) << "Failed to get values from error fact: "
                   << fact.DebugString();
      continue;
    }
    absl::StatusOr<std::string> status_or_name_str =
        status_or_name_val.value().GetSymbolAsString();
    absl::StatusOr<std::string> status_or_type_str =
        status_or_type_val.value().GetSymbolAsString();
    absl::StatusOr<std::string> status_or_message_str =
        status_or_message_val.value().GetString();
    if (!status_or_type_str.ok() || !status_or_message_str.ok() ||
        !status_or_name_str.ok()) {
      LOG(WARNING) << "Failed to get strings from error fact: "
                   << fact.DebugString();
      continue;
    }
    if (message_only) {
      error_msgs.push_back(status_or_message_str.value());
    } else {
      error_msgs.push_back(absl::StrFormat(
          "%s|%s (%s)", status_or_name_str.value(),
          status_or_message_str.value(), status_or_type_str.value()));
    }
  }
  return error_msgs;
}

void AddExtendedStatusLegacyErrors(
    clips::Environment* absl_nonnull env,
    intrinsic_proto::status::ExtendedStatus& es) {
  for (const std::string& error_msg :
       GetClipsLegacyErrorMessages(env, /*message_only=*/true)) {
    *es.add_context() = CreateExtendedStatus(
        kLegacyErrorStatusCode,
        absl::StrFormat("Additional error info: %s", error_msg));
  }
}

absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
GetOperationExtendedStatus(clips::Environment* absl_nonnull env,
                           clips::ProtobufManager* absl_nonnull proto_mgr,
                           std::string_view operation_name) {
  env->mutex()->AssertHeld();
  INTR_ASSIGN_OR_RETURN(
      clips::Fact op_fact,
      env->GetUniqueFact("operation-envelope", {{"name", operation_name}}));
  INTR_ASSIGN_OR_RETURN(clips::Value extended_status_proto_id_val,
                        op_fact.GetSlotValue("extended-status-proto-id"));
  INTR_ASSIGN_OR_RETURN(int64_t extended_status_proto_id_int,
                        extended_status_proto_id_val.GetInteger());
  clips::ProtoMessageId extended_status_proto_id(extended_status_proto_id_int);
  if (extended_status_proto_id == clips::ProtobufManager::kInvalidId) {
    return absl::InternalError(absl::StrFormat(
        "Failed to retrieve extended status from operation '%s'",
        operation_name));
  }
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic_proto::status::ExtendedStatus>
          extended_status_ptr,
      proto_mgr->GetProtoAs<intrinsic_proto::status::ExtendedStatus>(
          extended_status_proto_id));
  return *extended_status_ptr;
}

absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
BuildOperationExtendedStatusWithLegacyErrors(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr,
    std::string_view operation_name) {
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::status::ExtendedStatus es,
      GetOperationExtendedStatus(env, proto_mgr, operation_name));
  AddExtendedStatusLegacyErrors(env, es);
  return es;
}

void SetOperationProtoError(clips::Environment* absl_nonnull env,
                            clips::ProtobufManager* absl_nonnull proto_mgr,
                            clips::ProtoMessageId operation_proto_id,
                            std::string_view operation_name) {
  env->mutex()->AssertHeld();
  absl::StatusOr<google::longrunning::Operation*> operation =
      proto_mgr->GetMutableProtoAs<google::longrunning::Operation>(
          operation_proto_id);
  if (!operation.ok()) {
    LOG(ERROR) << "Failed to retrieve the Operation proto of operation '"
               << operation_name << "': " << operation.status();
    return;
  }

  absl::StatusOr<intrinsic_proto::status::ExtendedStatus> extended_status =
      BuildOperationExtendedStatusWithLegacyErrors(env, proto_mgr,
                                                   operation_name);

  // Check if the operation extended status contains a title or user report
  // message and use that as the operation error message, otherwise fall back to
  // a generic message.
  google::rpc::Status& error = *(*operation)->mutable_error();
  if (extended_status.ok() && !extended_status->title().empty()) {
    error = SaveStatusAsRpcStatus(absl::AbortedError(extended_status->title()));
  } else if (extended_status.ok() &&
             !extended_status->user_report().message().empty()) {
    error = SaveStatusAsRpcStatus(
        absl::AbortedError(extended_status->user_report().message()));
  } else if (extended_status.ok()) {
    error = SaveStatusAsRpcStatus(
        absl::AbortedError("Operation failed, please check details."));
  } else {
    LOG(ERROR) << "Failed to retrieve the extended status of operation '"
               << operation_name << "': " << extended_status.status();
    error = SaveStatusAsRpcStatus(
        absl::InternalError("Operation failed with unknown error."));
  }

  if (extended_status.ok()) {
    error.add_details()->PackFrom(*extended_status);
  }
}

absl::Status AddClipsOperationErrorFunctions(
    clips::Environment* absl_nonnull env,
    clips::ProtobufManager* absl_nonnull proto_mgr) {
  env->mutex()->AssertHeld();
  return env->AddFunction(
      kOperationProtoSetErrorFunction,
      std::function([env, proto_mgr](int64_t operation_proto_id,
                                     const std::string& operation_name) {
        env->mutex()->AssertHeld();
        SetOperationProtoError(env, proto_mgr,
                               clips::ProtoMessageId(operation_proto_id),
                               operation_name);
      }));
}

}  // namespace intrinsic::executive
