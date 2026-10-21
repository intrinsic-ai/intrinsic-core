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

#ifndef INTRINSIC_ICON_UTILS_REALTIME_STACK_TRACE_H_
#define INTRINSIC_ICON_UTILS_REALTIME_STACK_TRACE_H_

#include "absl/types/span.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/fixed_string.h"

namespace intrinsic::icon {
// The maximum size of the generated stack trace string. Longer traces will be
// truncated.
constexpr int kRtErrorStacktraceStringSize = 2048;

// Initializes the stack trace internally. Needs to be called once and in a
// non-realtime section.
void InitRtStackTrace() INTRINSIC_NON_REALTIME_ONLY;

// Prints a simple stack trace using GIZ_RT_LOG(ERROR).
// This function does not allocate and is real-time compatible.
// `skip_count` is the number of stack frames to skip.
// The advantage of this function over `GenerateRtErrorStackTrace()` is that
// this function does not return a string and therefore is not limited by the
// string size.
void LogRtErrorStackTrace(int skip_count = 2);

// Generates a stack trace that was created by absl::GetStackTrace(). This takes
// usually a double digit usec duration to create. Function names will only be
// shown if the binary is linked shared. `skip_count` is the number of stack
// frames to skip.
FixedString<kRtErrorStacktraceStringSize> GenerateRtErrorStackTrace(
    int skip_count = 2);

// Symbolizes the given stack trace frames and generates a stack trace string
// out of it. The frames can be generated with absl::GetStackTrace().
//
// Will return empty string, if `InitRtStackTrace()` was not called beforehand.
//
// Example string:
//
// ```
// Stacktrace:
// #01: '_ZN7testing4Test3RunEv' (0x7f38021bee63).
// #02: '_ZN7testing8TestInfo3RunEv' (0x7f38021bfefa).
// #03: '_ZN7testing9TestSuite3RunEv' (0x7f38021c1749).
// #04: '_ZN7testing8internal12UnitTestImpl11RunAllTestsEv' (0x7f38021d3850).
// #05: '_ZN7testing8UnitTest3RunEv' (0x7f38021d2fe7).
// #06: 'main' (0x55d39402a680).
// #07: '__libc_start_main' (0x7f37b99413d4).
// #08: '_start' (0x55d39402a4ea).
// ```
FixedString<kRtErrorStacktraceStringSize> GenerateRtErrorStackTrace(
    absl::Span<const void* const> frames);
// Non-const overload for convenience. See other overload for more information.
FixedString<kRtErrorStacktraceStringSize> GenerateRtErrorStackTrace(
    absl::Span<void*> frames);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_UTILS_REALTIME_STACK_TRACE_H_
