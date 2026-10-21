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

#ifndef INTRINSIC_UTIL_LOG_LINES_H_
#define INTRINSIC_UTIL_LOG_LINES_H_

#include <string_view>

#include "absl/base/log_severity.h"

namespace intrinsic {
// Splits up text into multiple lines and logs it with the given severity.
// The file name and line number provided are used in the logging message.
void LogLines(int severity, std::string_view text, const char* file_name,
              int line_number);

inline void LogLines(absl::LogSeverity severity, std::string_view text,
                     const char* file_name, int line_number) {
  LogLines(static_cast<int>(severity), text, file_name, line_number);
}
}  // end namespace intrinsic

// Canonical macro for LOG_LINES.
// This can be used to write strings longer than the log buffer to the log,
// if they can be split by newline into multiple strings. This is useful for
// example to dump a proto to the log (make sure not to include PII). Do not
// use this to generally log large amounts of text.
//
// Usage:
// std::string my_very_long_string = "foo\nbar\neat\nnatural";;
// LOG_LINES(INFO, my_very_long_string);
//
// Note the difference that this does not use the << operator for concatenation.
// Log severity is the usual absl::LogSeverity values, e.g., INFO, WARNING etc.
#define LOG_LINES(SEVERITY, STRING) \
  ::intrinsic::internal::LogLines_##SEVERITY(STRING, __FILE__, __LINE__)

// Like LOG_LINES, but for VLOG.
// Example:
//   VLOG_LINES(3, some_proto->DebugString());
#define VLOG_LINES(LEVEL, STRING)                        \
  do {                                                   \
    if (ABSL_VLOG_IS_ON(LEVEL)) LOG_LINES(INFO, STRING); \
  } while (false)

namespace intrinsic::internal {
#define _LOG_LINES_FUNC_FOR_LEVEL(LEVEL, SEVERITY)                           \
  inline void LogLines_##LEVEL(std::string_view text, const char* file_name, \
                               int line_number) {                            \
    LogLines(SEVERITY, text, file_name, line_number);                        \
  }

_LOG_LINES_FUNC_FOR_LEVEL(INFO, absl::LogSeverity::kInfo);
_LOG_LINES_FUNC_FOR_LEVEL(WARNING, absl::LogSeverity::kWarning);
_LOG_LINES_FUNC_FOR_LEVEL(ERROR, absl::LogSeverity::kError);
_LOG_LINES_FUNC_FOR_LEVEL(FATAL, absl::LogSeverity::kFatal);
}  // end namespace intrinsic::internal

#endif  // INTRINSIC_UTIL_LOG_LINES_H_
