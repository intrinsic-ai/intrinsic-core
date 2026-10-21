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

#include "intrinsic/icon/utils/realtime_stack_trace.h"

#include <dlfcn.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/debugging/stacktrace.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/fixed_string.h"
#include "intrinsic/icon/utils/log.h"

namespace intrinsic::icon {

// This flag is used to ensure that InitRtStackTrace() is called before
// GenerateRtErrorStackTrace().
std::atomic_bool rt_stacktrace_initialized = false;
// Mutex to protect concurrent initialization of the stack trace.
ABSL_CONST_INIT absl::Mutex rt_stacktrace_initialization_mutex(
    absl::kConstInit);

void InitRtStackTrace() {
  absl::MutexLock lock(&rt_stacktrace_initialization_mutex);
  if (!rt_stacktrace_initialized) {
    rt_stacktrace_initialized = true;
  }
}

void LogRtErrorStackTrace(int skip_count) {
  if (!rt_stacktrace_initialized) {
    INTRINSIC_RT_LOG(WARNING)
        << "Call to GenerateRtErrorStackTrace() without prior "
           "call to InitRtStackTrace.";
    return;
  }
  INTRINSIC_RT_LOG(ERROR) << "Stacktrace:";
  constexpr size_t kMaxFrames = 16;
  // Get the stack trace using absl instead of linux stacktrace, as that
  // sometimes doesn't show the full trace.
  void* buffer[kMaxFrames];
  const int num_frames = absl::GetStackTrace(buffer, kMaxFrames, skip_count);
  auto frames = absl::MakeSpan(buffer, num_frames);

  for (int index = 0; index < frames.size(); index++) {
    const uint64_t pc = reinterpret_cast<uintptr_t>(frames[index]);
    const char* name = "<no symbol available>";
    INTRINSIC_RT_LOG(ERROR) << "#" << absl::Dec(index, absl::kZeroPad2) << ": '"
                            << name << "' (0x" << absl::Hex(pc) << ").";
  }
}

FixedString<kRtErrorStacktraceStringSize> GenerateRtErrorStackTrace(
    int skip_count) {
  // This function needs to be real-time compatible
  // when stack is printed as a non-fatal error.
  constexpr size_t kMaxFrames = 16;
  // Get the stack trace using absl instead of linux stacktrace, as that
  // sometimes doesn't show the full trace.
  void* buffer[kMaxFrames];
  const int num_frames = absl::GetStackTrace(buffer, kMaxFrames, skip_count);

  return GenerateRtErrorStackTrace(absl::MakeSpan(buffer, num_frames));
}

FixedString<kRtErrorStacktraceStringSize> GenerateRtErrorStackTrace(
    absl::Span<const void* const> frames) {
  if (!rt_stacktrace_initialized) {
    INTRINSIC_RT_LOG(WARNING)
        << "Call to GenerateRtErrorStackTrace() without prior "
           "call to InitRtStackTrace.";
    return {};
  }
  FixedString<kRtErrorStacktraceStringSize> full_string;
  for (int index = 0; index < frames.size(); index++) {
    const uint64_t pc = reinterpret_cast<uintptr_t>(frames[index]);
    const char* name = "<no symbol available>";
    full_string.append(FixedStrCat<kRtErrorStacktraceStringSize>(
        "#", absl::Dec(index, absl::kZeroPad2), ": '", name, "' (0x",
        absl::Hex(pc), ").\n"));
  }
  return full_string;
}

FixedString<kRtErrorStacktraceStringSize> GenerateRtErrorStackTrace(
    absl::Span<void*> frames) {
  // Needs some explicit casting to make it const.
  const void* const* buf = static_cast<const void* const*>(frames.data());
  absl::Span<const void* const> span = absl::MakeConstSpan(buf, frames.size());
  return GenerateRtErrorStackTrace(span);
}

}  // namespace intrinsic::icon
