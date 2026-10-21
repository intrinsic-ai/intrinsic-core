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

#ifndef INTRINSIC_UTIL_STATUS_STATUS_SPECS_H_
#define INTRINSIC_UTIL_STATUS_STATUS_SPECS_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/assets/proto/status_spec.pb.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/status_builder.h"

namespace intrinsic {

// Extended Status Specs API.

// Extended Status are a way to provide error information in the system and to a
// user eventually. The API in this file enables to read declaration and static
// information of errors from a binary proto file. The proto file must contain a
// message of type intrinsic_proto.assets.StatusSpecs (alternatively, this can
// also be provided as a static list once during initialization).
//
// General use is to call InitExtendedStatusSpecs() once per process in its
// main() function (for example, with a linked memfile). After that,
// CreateExtendedStatus() and AttachExtendedStatus() can be used to create
// errors easily. Referencing the respective error code, static information such
// as title and recovery instructions are read from the file. Errors which are
// not declared will raise a warning. The proto file can also be used to
// generate documentation.
//
// This API is intended to be used for a specific component which is run as a
// process, it is not to be used in API libraries. Call this in a process's
// main() function, not in a library. This must finish before calling any other
// function, in particular before any call to CreateExtendedStatus!

// Initialize the extended status functionality from a file for a specified
// component.
absl::Status InitExtendedStatusSpecs(std::string_view component,
                                     std::string_view filename);

// Initializes the extended status functionality from a list for a specified
// component.
absl::Status InitExtendedStatusSpecs(
    std::string_view component,
    const std::vector<intrinsic_proto::assets::StatusSpec>& specs);
absl::Status InitExtendedStatusSpecs(
    std::string_view component,
    const google::protobuf::RepeatedPtrField<
        intrinsic_proto::assets::StatusSpec>& specs);

// Checks whether InitExtendedStatusSpecs has already been called.
bool IsExtendedStatusSpecsInitialized();

// Erases status specs loaded prior. This is useful for tests.
void ClearExtendedStatusSpecs();

struct ExtendedStatusOptions {
  std::optional<absl::Time> timestamp;
  std::optional<std::string_view> debug_message;
  std::optional<intrinsic_proto::status::ExtendedStatus::Relations> related_to;
  std::optional<intrinsic_proto::data_logger::Context> log_context;
  std::optional<intrinsic_proto::status::ExtendedStatus::Severity> severity;
  std::vector<intrinsic_proto::status::ExtendedStatus> context;
};

// Creates an extended status for the given code. This will retrieve information
// from the status specs when available. Provide a user report message with as
// much detail as useful to a user, e.g., add specific values (example: "value
// 34 is too large, must be smaller than 20", where 34 and 20 would be values
// that occurred during execution). Typical would be a format string using
// absl::StrFormat(). You can provide additional options with additional
// information, in particular context (upstream errors in the form of
// ExtendedStatus protos).  If no information is available in the specs file,
// this will still emit the extended status with the given information, but also
// add a context extended status with a warning that the declaration is missing.
intrinsic_proto::status::ExtendedStatus CreateExtendedStatus(
    uint32_t code, std::string_view user_message,
    const ExtendedStatusOptions& options = {});

// Creates an absl::Status which has the given 'generic_code' and which has
// an ExtendedStatus with the given extended status 'code' and 'user_message'.
//
// For the 'user_message' and extended status 'options', also see the
// description of CreateExtendedStatus() above. If the given 'options' do not
// have an explicit severity value set, the created ExtendedStatus defaults to
// severity ERROR.
absl::Status CreateStatus(
    uint32_t code, std::string_view user_message,
    absl::StatusCode generic_code = absl::StatusCode::kUnknown,
    const ExtendedStatusOptions& options = {});

// Creates a pure policy function suitable to be used for StatusBuilder::With().
//
// Example invocation:
//
// Once at process start in main():
// INTR_RETURN_IF_ERROR(
//   InitExtendedStatusSpec("ai.intrinsic.my_component",
//                          "/path/to/status_specs.binarypb"));
//
// Then where an error may occur:
// INTR_RETURN_IF_ERROR(MyFunction())
//   .With(AttachExtendedStatus(23488, "While ... MyFunction failed",
//                              {.debug_message="Some debug info"}));
//
// Note that for INTR_ASSIGN_OR_RETURN you must move the status builder to
// properly apply the policy:
//
// INTR_ASSIGN_OR_RETURN(std::string s, ComputeValue(),
//                       std::move(_).With(AttacheExtendedStatus(
//                           23488, "While ... ComputeValue failed",
//                           {.debug_message="Some debug info"})));
//
// This invokes CreateExtendedStatus() and then attaches that status to the
// given StatusBuilder. This is a pure policy for the StatusBuilder that may
// be chained (see StatusBuilder documentation).
//
// If the given 'options' do not have an explicit severity value set, the
// created ExtendedStatus defaults to severity ERROR.
std::function<StatusBuilder && (StatusBuilder&&)> AttachExtendedStatus(
    uint32_t code, std::string_view user_message,
    const ExtendedStatusOptions& options = {});

// Policy function for use with StatusBuilder::With() that allows wrapping
// legacy status according to wrap_mode. If the wrapped Status does not have an
// ExtendedStatus it is included in the created ExtendedStatus either as a debug
// report or as a generic ExtendedStatus in the context. For details see
// StatusBuilder::WrapExtendedStatus.
//
// If the given 'options' do not have an explicit severity value set, the
// created ExtendedStatus defaults to severity ERROR.
//
// For invocation examples, see AttachExtendedStatus docs above.
std::function<StatusBuilder && (StatusBuilder&&)> WrapExtendedStatus(
    uint32_t code, std::string_view user_message,
    StatusBuilder::WrapExtendedStatusMode wrap_mode,
    const ExtendedStatusOptions& options = {});

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STATUS_STATUS_SPECS_H_
