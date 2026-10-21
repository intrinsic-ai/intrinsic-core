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

#include "intrinsic/skills/internal/error_utils.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "google/rpc/status.pb.h"
#include "intrinsic/assets/proto/status_spec.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/skills/internal/runtime_data.h"
#include "intrinsic/skills/proto/error.pb.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_conversion_rpc.h"

namespace intrinsic {
namespace skills {

namespace {
// Maximum length to which an external report message is shortened when used
// as a title (when title neither set in status specs nor provided by user).
constexpr int kExtendedStatusTitleShortToLength = 75;

// Returns an action description for a skill error status.
std::string ErrorToSkillAction(absl::Status status) {
  // LINT.IfChange(skill_error_actions)
  switch (status.code()) {
    case absl::StatusCode::kCancelled:
      return "was cancelled during";
    case absl::StatusCode::kInvalidArgument:
      return "was passed invalid parameters during";
    case absl::StatusCode::kUnimplemented:
      return "has not implemented";
    case absl::StatusCode::kDeadlineExceeded:
      return "timed out during";
    default:
      return "returned an error during";
  }
  // LINT.ThenChange(//intrinsic_sdk/intrinsic/skills/internal/skill_service_impl.py:skill_error_actions)
}

std::string EllipsizeString(absl::string_view s) {
  if (s.length() < kExtendedStatusTitleShortToLength) {
    return std::string(s);
  }

  constexpr absl::string_view kEllipsis = "...";

  absl::string_view::size_type space_pos =
      s.rfind(' ', kExtendedStatusTitleShortToLength - kEllipsis.length());
  if (space_pos == absl::string_view::npos) {
    return absl::StrCat(
        s.substr(0, kExtendedStatusTitleShortToLength - kEllipsis.length()),
        kEllipsis);
  }
  return absl::StrCat(s.substr(0, space_pos), kEllipsis);
}

// Updates the extended status error in the following ways:
// - if component not set, sets skill id
// - if component set that doesn't match skill ID wraps ES
// - if timestamp not set, sets it to "now"
// - if log_context provided and non set already set it
absl::Status UpdateExtendedStatusOnError(
    absl::Status status, absl::string_view skill_id, absl::string_view op_name,
    const internal::StatusSpecs& status_specs,
    std::optional<intrinsic_proto::data_logger::Context> log_context) {
  intrinsic_proto::status::ExtendedStatus es;
  std::optional<absl::Cord> extended_status_payload =
      status.GetPayload(AddTypeUrlPrefix(es));
  if (extended_status_payload) {
    // This should not happen, but since we cannot control the data a skill
    // developer could put anything. Hence, if we fail to interpret the data
    // just return as-is.
    if (!es.ParseFromString(*extended_status_payload)) {
      LOG(WARNING) << "Skill " << skill_id
                   << " issued an invalid extended status payload";
      return status;
    }
  } else {
    // Fallback conversion to extended status if none given
    es.mutable_status_code()->set_code(static_cast<uint32_t>(status.code()));
    es.set_title(absl::StrFormat("Skill %s %s (generic code %s)",
                                 ErrorToSkillAction(status), op_name,
                                 absl::StatusCodeToString(status.code())));
    es.mutable_user_report()->set_message(status.message());
  }

  if (!es.has_status_code() || es.status_code().component().empty()) {
    // Set component since it wasn't set before
    es.mutable_status_code()->set_component(skill_id);
  } else if (es.status_code().component() != skill_id) {
    // this may be a case of a skill author simply returning an error
    // previously received from, e.g., a service. This should not be the
    // skill's status, but we also cannot just overwrite it. Wrap this
    // into a new extended status.
    intrinsic_proto::status::ExtendedStatus new_es;
    new_es.mutable_status_code()->set_component(skill_id);
    *new_es.add_context() = std::move(es);
    es = std::move(new_es);
  }  // else: it's the expected component ID, don't do anything

  // Set timestamp to now if none set
  if (!es.has_timestamp()) {
    *es.mutable_timestamp() = GetCurrentTimeProto();
  }

  if (es.title().empty()) {
    absl::StatusOr<intrinsic_proto::assets::StatusSpec> status_spec =
        status_specs.GetSpecForCode(es.status_code().code());
    if (status_spec.ok()) {
      es.set_title(status_spec->title());
    } else if (es.has_user_report() && !es.user_report().message().empty()) {
      es.set_title(EllipsizeString(es.user_report().message()));
    } else if (es.has_status_code()) {
      es.set_title(absl::StrFormat("%s:%d", es.status_code().component(),
                                   es.status_code().code()));
    }
  }

  // If no log context has been set explicitly, update it
  if (log_context &&
      (!es.has_related_to() || !es.related_to().has_log_context())) {
    *es.mutable_related_to()->mutable_log_context() = *log_context;
  }

  // Update the extended status with the modified version
  status.SetPayload(AddTypeUrlPrefix(es), es.SerializeAsCord());

  return status;
}

}  // end namespace

// Handles an error return a skill.
::google::rpc::Status CreateSkillError(
    absl::Status status, absl::string_view skill_id, absl::string_view op_name,
    const internal::StatusSpecs& status_specs,
    std::optional<intrinsic_proto::data_logger::Context> log_context) {
  std::string message = absl::StrFormat(
      "Skill %s %s %s (code: %s). Message: %s", skill_id,
      ErrorToSkillAction(status), op_name,
      absl::StatusCodeToString(status.code()), status.message());

  intrinsic_proto::skills::SkillErrorInfo error_info;
  error_info.set_error_type(
      intrinsic_proto::skills::SkillErrorInfo::ERROR_TYPE_SKILL);

  ::google::rpc::Status rpc_status =
      ToGoogleRpcStatus(UpdateExtendedStatusOnError(status, skill_id, op_name,
                                                    status_specs, log_context));
  rpc_status.set_message(message);
  static_cast<void>(rpc_status.add_details()->PackFrom(
      error_info));  // TODO: Handle return value

  return rpc_status;
}

intrinsic_proto::skills::SkillErrorInfo GetErrorInfo(
    const absl::Status& status) {
  intrinsic_proto::skills::SkillErrorInfo error_info;
  std::optional<absl::Cord> error_info_cord =
      status.GetPayload(AddTypeUrlPrefix(
          intrinsic_proto::skills::SkillErrorInfo::descriptor()->full_name()));
  if (error_info_cord) {

    // Similar to SerializeAsCord, ParseFromString is not available.

    static_cast<void>(error_info.ParseFromString(std::string(
        error_info_cord.value())));  // NOLINT  // TODO: Handle return value
  }
  return error_info;
}

}  // namespace skills
}  // namespace intrinsic
