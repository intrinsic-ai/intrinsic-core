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

#ifndef INTRINSIC_UTIL_STATUS_LOG_IF_ERROR_H_
#define INTRINSIC_UTIL_STATUS_LOG_IF_ERROR_H_

#include <utility>

#include "absl/status/status.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"

// Expects expr to yield an absl::Status or intrinsic::StatusBuilder. If the
// status is not ok, logs the status with the given severity. Note that severity
// is from the absl::LogSeverity enum.
//
// Example:
// INTR_LOG_IF_ERROR(absl::LogSeverity::kError, FuncThatYieldsStatus());
#define INTR_LOG_IF_ERROR(severity, expr)                                      \
  INTR_STATUS_MACROS_IMPL_ELSE_BLOCKER_                                        \
  if (intrinsic::status_macro_internal::StatusAdaptorForMacros status_adapter{ \
          (expr), INTRINSIC_LOC}) {                                            \
    /* Status of expr is OK, nothing to do */                                  \
  } else /* NOLINT */                                                          \
    intrinsic::status_macro_internal::StatusBuilderConvertOnDestroy(           \
        status_adapter.Consume().Log(severity))

// Anything below are implementation details
namespace intrinsic {
namespace status_macro_internal {

class StatusBuilderConvertOnDestroy {
 public:
  explicit StatusBuilderConvertOnDestroy(StatusBuilder&& status_builder)
      : status_builder_(std::move(status_builder)) {}

  ~StatusBuilderConvertOnDestroy() {
    absl::Status(std::move(status_builder_)).IgnoreError();
  }

 private:
  StatusBuilder status_builder_;
};

}  // namespace status_macro_internal
}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STATUS_LOG_IF_ERROR_H_
