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

#include "intrinsic/icon/utils/log_sink.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"

namespace intrinsic::icon {

const char* LogPriorityName(LogPriority priority) {
  switch (priority) {
    case LogPriority::ERROR:
      return "ERROR";
    case LogPriority::WARNING:
      return "WARNING";
    case LogPriority::INFO:
      return "INFO";
  }
}

int LogEntryFormatToBuffer(char* buffer, int buffer_size,
                           const LogSinkInterface::LogEntry& entry,
                           absl::TimeZone timezone) {
  CHECK(buffer != nullptr);
  const char* priority = LogPriorityName(entry.priority);
  absl::Duration stamp = absl::Nanoseconds(entry.robot_timestamp_ns);
  int64_t sec = stamp / absl::Seconds(1);
  int usec = (stamp - absl::Seconds(sec)) / absl::Microseconds(1);

  // Format wall timestamp.
  // Want: "0102 15:04:05.000000"
  const absl::TimeZone::CivilInfo ci =
      timezone.At(absl::FromUnixNanos(entry.wall_timestamp_ns));

  // Get the base of the filename.
  const char* base = strrchr(entry.filename, '/');
  if (base == nullptr) {
    base = entry.filename;
  } else {
    base = base + 1;
  }

  static constexpr char kFormat[] =
      "%c%02d%02d %02d:%02d:%02d.%06d       0 %s:%d] %05d.%06d: %.*s\n";

  int entry_msg_size =
      std::min(entry.msglen, static_cast<int>(sizeof(entry.msg)));

  int num_chars =
      absl::SNPrintF(buffer, buffer_size, kFormat, priority[0], ci.cs.month(),
                     ci.cs.day(), ci.cs.hour(), ci.cs.minute(), ci.cs.second(),
                     absl::ToInt64Microseconds(ci.subsecond), base, entry.line,
                     sec, usec, entry_msg_size, entry.msg);
  return std::min(num_chars, buffer_size - 1);
}

void StderrLogSink::Log(const LogEntry& entry) {
  char buffer[kLogMessageMaxSize] = {0};
  LogEntryFormatToBuffer(buffer, sizeof(buffer), entry);
  fprintf(stderr, "%s", buffer);
  fflush(stderr);
}

}  // namespace intrinsic::icon
