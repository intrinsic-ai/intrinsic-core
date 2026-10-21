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

#include "intrinsic/util/status/status_specs.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/assets/proto/status_spec.pb.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_builder.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

namespace intrinsic {

namespace {

struct StatusSpecsMetadata {
  std::string filename;
  std::string component;
  absl::flat_hash_map<uint32_t, intrinsic_proto::assets::StatusSpec> specs;
};

std::optional<StatusSpecsMetadata>& GetStatusSpecsMetadata() {
  static absl::NoDestructor<std::optional<StatusSpecsMetadata>>
      status_specs_metadata;
  return *status_specs_metadata;
}

constexpr std::string_view kErrorComponent = "ai.intrinsic.errors";
constexpr uint32_t kUndeclaredError = 604;
}  // namespace

absl::Status InitExtendedStatusSpecs(std::string_view component,
                                     std::string_view filename) {
  std::optional<StatusSpecsMetadata>& status_specs_metadata =
      GetStatusSpecsMetadata();
  if (status_specs_metadata.has_value()) {
    return absl::AlreadyExistsError(
        "ExtendedStatus specs have already been initialized. Call "
        "InitExtendedStatusSpecs only once in a process.");
  }

  StatusSpecsMetadata loaded_metadata;
  loaded_metadata.component = component;

  intrinsic_proto::assets::StatusSpecs message;
  QCHECK_OK(file::GetBinaryProto(filename, &message, file::Defaults()))
      << "Failed to load extended status specs from " << filename;

  for (const intrinsic_proto::assets::StatusSpec& st : message.status_info()) {
    uint32_t code = st.code();
    if (code == 0) {
      LOG(ERROR) << "Invalid status spec (code is zero): " << st;
      continue;
    }

    if (bool inserted{false}; std::tie(std::ignore, inserted) =
                                  loaded_metadata.specs.try_emplace(code, st),
                              !inserted) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Component %s declares error code %d twice (once with title '%s', "
          "once with '%s')",
          component, code, loaded_metadata.specs[code].title(), st.title()));
    }
  }

  status_specs_metadata = std::move(loaded_metadata);
  return absl::OkStatus();
}

absl::Status InitExtendedStatusSpecs(
    std::string_view component,
    const google::protobuf::RepeatedPtrField<
        intrinsic_proto::assets::StatusSpec>& specs) {
  std::optional<StatusSpecsMetadata>& status_specs_metadata =
      GetStatusSpecsMetadata();
  if (status_specs_metadata.has_value()) {
    return absl::AlreadyExistsError(
        "ExtendedStatus specs have already been initialized. Call "
        "InitExtendedStatusSpecs only once in a process.");
  }

  StatusSpecsMetadata loaded_metadata;
  loaded_metadata.component = component;

  for (const intrinsic_proto::assets::StatusSpec& spec : specs) {
    uint32_t code = spec.code();
    if (code == 0) {
      LOG(ERROR) << "Invalid status spec (code is zero): " << spec;
      continue;
    }
    if (bool inserted{false}; std::tie(std::ignore, inserted) =
                                  loaded_metadata.specs.try_emplace(code, spec),
                              !inserted) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Component %s declares error code %d twice (once with title '%s', "
          "once with '%s')",
          component, code, loaded_metadata.specs[code].title(), spec.title()));
    }
  }

  status_specs_metadata = std::move(loaded_metadata);
  return absl::OkStatus();
}

absl::Status InitExtendedStatusSpecs(
    std::string_view component,
    const std::vector<intrinsic_proto::assets::StatusSpec>& specs) {
  std::optional<StatusSpecsMetadata>& status_specs_metadata =
      GetStatusSpecsMetadata();
  if (status_specs_metadata.has_value()) {
    return absl::AlreadyExistsError(
        "ExtendedStatus specs have already been initialized. Call "
        "InitExtendedStatusSpecs only once in a process.");
  }

  StatusSpecsMetadata loaded_metadata;
  loaded_metadata.component = component;

  for (const intrinsic_proto::assets::StatusSpec& spec : specs) {
    uint32_t code = spec.code();
    if (code == 0) {
      LOG(ERROR) << "Invalid status spec (code is zero): " << spec;
      continue;
    }
    if (bool inserted{false}; std::tie(std::ignore, inserted) =
                                  loaded_metadata.specs.try_emplace(code, spec),
                              !inserted) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Component %s declares error code %d twice (once with title '%s', "
          "once with '%s')",
          component, code, loaded_metadata.specs[code].title(), spec.title()));
    }
  }

  status_specs_metadata = std::move(loaded_metadata);
  return absl::OkStatus();
}

bool IsExtendedStatusSpecsInitialized() {
  return GetStatusSpecsMetadata().has_value();
}

void ClearExtendedStatusSpecs() {
  std::optional<StatusSpecsMetadata>& status_specs_metadata =
      GetStatusSpecsMetadata();
  status_specs_metadata = std::nullopt;
}

namespace {

// Creates an ExtendedStatus with the specified options. 'default_severity' is
// used if no severity is set in 'options'.
intrinsic_proto::status::ExtendedStatus CreateExtendedStatusImpl(
    uint32_t code, std::string_view user_message,
    const ExtendedStatusOptions& options,
    intrinsic_proto::status::ExtendedStatus::Severity default_severity) {
  const std::optional<StatusSpecsMetadata>& metadata = GetStatusSpecsMetadata();
  QCHECK(metadata.has_value())
      << "Call InitExtendedStatusSpecs before invoking CreateExtendedStatus.";

  intrinsic_proto::status::ExtendedStatus es;
  es.mutable_status_code()->set_component(metadata->component);
  es.mutable_status_code()->set_code(code);

  if (!user_message.empty()) {
    es.mutable_user_report()->set_message(user_message);
  }

  if (const auto it = metadata->specs.find(code); it != metadata->specs.end()) {
    const intrinsic_proto::assets::StatusSpec& spec = it->second;
    es.set_title(spec.title());
    if (!spec.recovery_instructions().empty() &&
        (!es.has_user_report() || es.user_report().instructions().empty())) {
      es.mutable_user_report()->set_instructions(spec.recovery_instructions());
    }
  } else {
    LOG(WARNING) << "No extended status spec for error " << metadata->component
                 << ":" << code;
    intrinsic_proto::status::ExtendedStatus not_found_es;
    not_found_es.mutable_status_code()->set_component(kErrorComponent);
    not_found_es.mutable_status_code()->set_code(kUndeclaredError);
    not_found_es.set_severity(intrinsic_proto::status::ExtendedStatus::WARNING);
    not_found_es.set_title("Error code not declared");
    not_found_es.mutable_user_report()->set_message(absl::StrFormat(
        "The code %s:%u has not been declared by the component.",
        metadata->component, code));
    not_found_es.mutable_user_report()->set_instructions(absl::StrFormat(
        "Inform the owner of %s to add error %u to the status specs file.",
        metadata->component, code));
    *es.add_context() = std::move(not_found_es);
  }

  if (options.timestamp.has_value()) {
    FromAbslTime(*options.timestamp, es.mutable_timestamp()).IgnoreError();
  } else {
    *es.mutable_timestamp() = GetCurrentTimeProto();
  }
  if (options.debug_message.has_value()) {
    es.mutable_debug_report()->set_message(*options.debug_message);
  }
  if (options.related_to.has_value()) {
    *es.mutable_related_to() = *options.related_to;
  }
  if (options.log_context.has_value()) {
    *es.mutable_related_to()->mutable_log_context() = *options.log_context;
  }
  es.set_severity(options.severity.value_or(default_severity));
  if (!options.context.empty()) {
    es.mutable_context()->Assign(options.context.begin(),
                                 options.context.end());
  }
  return es;
}

}  // namespace

intrinsic_proto::status::ExtendedStatus CreateExtendedStatus(
    uint32_t code, std::string_view user_message,
    const ExtendedStatusOptions& options) {
  return CreateExtendedStatusImpl(
      code, user_message, options,
      /*default_severity=*/intrinsic_proto::status::ExtendedStatus::DEFAULT);
}

absl::Status CreateStatus(uint32_t code, std::string_view user_message,
                          absl::StatusCode generic_code,
                          const ExtendedStatusOptions& options) {
  return StatusBuilder(absl::Status(generic_code, user_message))
      .With(AttachExtendedStatus(code, user_message, options));
}

std::function<StatusBuilder && (StatusBuilder&&)> AttachExtendedStatus(
    uint32_t code, std::string_view user_message,
    const ExtendedStatusOptions& options) {
  // Create an ExtendedStatus moved into the lambda closure to avoid having to
  // copy all parameters, in particular options. We also ensure that the
  // timestamp, if not set in options, is as close to the actual event as
  // possible.
  intrinsic_proto::status::ExtendedStatus es = CreateExtendedStatusImpl(
      code, user_message, options,
      // We are in the context of creating an absl::Status (i.e., an error).
      /*default_severity=*/intrinsic_proto::status::ExtendedStatus::ERROR);
  return
      [es = std::move(es)](StatusBuilder&& status_builder) -> StatusBuilder&& {
        return std::move(status_builder).AttachExtendedStatus(std::move(es));
      };
}

std::function<StatusBuilder && (StatusBuilder&&)> WrapExtendedStatus(
    uint32_t code, std::string_view user_message,
    StatusBuilder::WrapExtendedStatusMode wrap_mode,
    const ExtendedStatusOptions& options) {
  intrinsic_proto::status::ExtendedStatus es = CreateExtendedStatusImpl(
      code, user_message, options,
      // We are in the context of creating an absl::Status (i.e., an error).
      /*default_severity=*/intrinsic_proto::status::ExtendedStatus::ERROR);
  return [es = std::move(es), wrap_mode = wrap_mode](
             StatusBuilder&& status_builder) -> StatusBuilder&& {
    return std::move(status_builder)
        .WrapExtendedStatus(std::move(es), wrap_mode);
  };
}

}  // namespace intrinsic
