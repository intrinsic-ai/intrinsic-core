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

#ifndef INTRINSIC_ICON_UTILS_MALLOC_GUARD_H_
#define INTRINSIC_ICON_UTILS_MALLOC_GUARD_H_

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define MALLOC_GUARD_HAS_RTSAN 1
#endif
#endif

#if MALLOC_GUARD_HAS_RTSAN
#include <sanitizer/rtsan_interface.h>
#endif

#if defined(__clang__) && defined(__has_attribute)
#if __has_attribute(clang_nonblocking)
#define MALLOC_GUARD_NONBLOCKING [[clang::nonblocking]]
#define MALLOC_GUARD_BLOCKING [[clang::blocking]]
#endif
#endif

#ifndef MALLOC_GUARD_NONBLOCKING
#define MALLOC_GUARD_NONBLOCKING
#define MALLOC_GUARD_BLOCKING
#endif

#include <chrono>
#include <string_view>

#include "intrinsic/icon/testing/realtime_annotations.h"

// Logs a message or value safely from a real-time context without allocating
// memory. Supported types are restricted to avoid allocations and formatting
// overhead.
namespace intrinsic::icon {
namespace internal {
void RtSafeLogImpl(const char* msg);
void RtSafeLogImpl(std::string_view msg);
void RtSafeLogImpl(size_t val);
void RtSafeLogImpl(int64_t val);
void RtSafeLogImpl(const void* ptr);
}  // namespace internal

template <class From, class To>
concept nothrow_convertible_to =
    std::is_nothrow_convertible_v<From, To> &&
    requires { static_cast<To>(std::declval<From>()); };

template <class T>
concept Loggable =
    nothrow_convertible_to<T, const char*> ||
    nothrow_convertible_to<T, std::string_view> ||
    nothrow_convertible_to<T, size_t> || nothrow_convertible_to<T, int64_t> ||
    nothrow_convertible_to<T, const void*>;

template <Loggable... Args>
void RtSafeLog(Args&&... args) {
  (internal::RtSafeLogImpl(std::forward<Args>(args)), ...);
  internal::RtSafeLogImpl("\n");
}

// This file provides functionality to monitor memory allocations. A common use
// case for this are real-time applications, in which it is usually forbidden to
// allocate memory on the heap. Without a malloc hook, it is difficult to
// determine if an algorithm actually calls malloc. Such a hook and convenience
// functions around it are provided in this file, see
// `InstallMallocGuardHooks()` and `ScopedMallocGuardHook`.
//
// Most use cases will link their allocators dynamically. If you link your
// allocator statically, you must *also* statically link a library that
// implements these two functions:
//
// * `MallocGuardCustomSetup()`
//   Registers hooks (that must call `ReportAllocation()`) with the custom
//   allocator and returns true on success.
// * `MallocGuardCustomTeardown()`
//   Undoes what `MallocGuardCustomSetup()` did, and returns true on success.
//
// NOTE: These two functions all run in non-realtime contexts, but the actual
// hook function should assume it runs in a realtime context and must not block.
//
// Several different reaction types on malloc allocations are possible, see
// `MallocGuardReaction`, `SetGlobalMallocGuardReaction()`,
// `SetThreadLocalMallocGuardReaction()`. It is also possible and necessary to
// control in which thread the memory allocations are actively monitored, see
// `MallocGuard`.
//
// Example usage:
//
// ```
// ScopedMallocGuardHook guard; // Somewhere in the process in a non-rt section.
//                              // Needs to stay alive for the whole duration
//                              // where the malloc guard should be active.
// {
//   MallocGuard malloc_guard; // Malloc guard is now active in this thread.
//                             // Can be instantiated in an rt section.
//   SetThreadLocalMallocGuardReaction(MallocGuardReaction::kStoreViolation);
//   GetThreadLocalMallocViolations().num_violations; // Contains number of
//                                  // violations of the current thread since
//                                  // kStoreViolation is set.
//   ResetThreadLocalMallocViolations(); // Resets violations in current thread.
// }
// ```
//
// Note: There are a few unsupported setups. The library does its best to detect
// them and error out with a helpful message, but there are some things it
// cannot detect. These are the unsupported setups:
//
// * The hook mechanism relies on statically caching pointers to the original
//   allocator functions (e.g., malloc, free). It assumes that the underlying
//   allocator does not change after the first allocation is intercepted. If a
//   custom allocator is loaded via dlopen() after MallocGuard is initialized,
//   MallocGuard raises a fatal error.
// * Loading a library into a new namespace using dlmopen() with LM_ID_NEWLM is
//   not supported and will result in a fatal error because the newly loaded
//   library will resolve to a different allocator symbol.
// * Denylisting and `MallocGuardCustomSetup/Teardown` are mutually exclusive.
//   Attempting to set a denylist if custom setup/teardown functions are present
//   causes a fatal error.
// * Loading libraries with RTLD_DEEPBIND is not allowed. Doing so after
//   `InstallMallocGuardHooks()` causes a fatal error.
// * Sanitizers: When compiled with allocator-replacing sanitizers
//   (AddressSanitizer, MemorySanitizer, ThreadSanitizer), MallocGuard is
//   automatically disabled and acts as a no-op, allowing code using
//   `MallocGuard` to be debugged with sanitizers. Note that RealtimeSanitizer
//   (RTSan) is supported and remains enabled.

// Sets up an allocator hook that reports any allocations to `MallocGuard` via
// `ReportAllocation()`. `MallocGuard` always pairs calls to this with calls to
// `MallocGuardCustomTeardown()`.
//
// As a function at namespace scope, this has external linkage, so another
// translation unit can provide a definition.
__attribute__((weak)) bool MallocGuardCustomSetup();

// Removes the allocator hook that `MallocGuardCustomSetup()` set up.
//
// As a function at namespace scope, this has external linkage, so another
// translation unit can provide a definition.
__attribute__((weak)) bool MallocGuardCustomTeardown();

enum class MallocGuardReaction {
  kNone,            // Do nothing on violations.
  kAbort,           // Abort the process and print the backtrace (default).
  kLog,             // Print all violations in the RT log (throttled).
  kLogWithTrace,    // Print all violations in the RT log (throttled) and add a
                    // stack trace. Creating the stack trace has a significant
                    // overhead (ballpark of double digit usec for 10 stack
                    // frames).
  kStoreViolation,  // Store the violations per thread. The violations are only
                    // accessible from the same thread. See
                    // `GetThreadLocalMallocViolations()`. For example, use this
                    // for a controlled reaction at the end of a cycle instead
                    // of an instant reaction when the violation happened.
  kStoreViolationWithTrace,  // Additionally stores a stack trace. The
                             // violations are only accessible from the same
                             // thread. See `GetThreadLocalMallocViolations()`.
                             // The stack trace string can be generated with
                             // `GenerateRtErrorStackTrace()`.
  kCustomCallback,  // Execute a custom callback when a violation occurs.
};

using MallocGuardCallback = void (*)(size_t allocated_bytes);

// Sets the global custom callback (default callback).
// This callback is executed when all of the following conditions are met:
// - A violation occurs
// - The active reaction resolves to kCustomCallback
// - The offending thread has NOT set its own thread-local callback
void SetGlobalMallocGuardCallback(MallocGuardCallback callback);

// Sets the thread-specific custom callback. If set, this overrides the global
// callback for the current thread that calls this function.
void SetThreadLocalMallocGuardCallback(MallocGuardCallback callback);

constexpr size_t kMaxMallocGuardStackFrames = 16;
struct StackTrace {
  StackTrace& operator=(const StackTrace& other) {
    std::copy(std::begin(other.stack_trace), std::end(other.stack_trace),
              std::begin(stack_trace));
    num_frames = other.num_frames;
    return *this;
  }
  void* stack_trace[kMaxMallocGuardStackFrames];
  size_t num_frames = 0;
};

struct MallocGuardViolations {
  // Counts how many times the malloc hook fires. Can be reset with
  // `ResetThreadLocalMallocViolations()`.
  std::atomic_size_t num_violations = 0;
  // Counts how many times the guarded thread has allocated. This should be zero
  // for a real-time safe section of code.
  std::atomic_size_t allocated_bytes = 0;
  // Contains the stack trace of the latest violation. Can be printed with
  // `LogRtErrorStacktrace()`. Might be empty if the stack trace creation was
  // not requested.
  std::optional<StackTrace> latest_violation_stack_trace;
  // Contains how long it took to create the latest stack trace.
  std::optional<std::chrono::nanoseconds> stack_creation_duration;
  MallocGuardViolations& operator=(const MallocGuardViolations& other) {
    num_violations = other.num_violations.load();
    allocated_bytes = other.allocated_bytes.load();
    latest_violation_stack_trace = other.latest_violation_stack_trace;
    stack_creation_duration = other.stack_creation_duration;
    return *this;
  }
};

// Enables the malloc hook for all threads (thread-safe). Do not call in
// real-time sections. Subsequent calls will just increase a counter and not
// install the hook multiple times. This does not enable the guard itself: Use
// icon::MallocGuard in threads where this hook should be actively evaluated.
// Prefer to use ScopedMallocGuardHook, which calls this function, if possible.
//
// Returns false when enabling the hook fails.
bool InstallMallocGuardHooks() INTRINSIC_NON_REALTIME_ONLY;

// Decreases the internal enable-counter and disables the hook if the counter
// reaches 0 (thread-safe).
//
// Returns false when disabling the hook fails or when this function
// was called more often than the corresponding `InstallMallocGuardHooks()`.
bool UninstallMallocGuardHooks() INTRINSIC_NON_REALTIME_ONLY;

// Checks if the malloc hook was enabled using `InstallMallocGuardHooks()`.
bool AreMallocGuardHooksInstalled() INTRINSIC_CHECK_REALTIME_SAFE;

bool CheckMallocHandlerInstalled(bool silent = false)
    INTRINSIC_NON_REALTIME_ONLY;

// Reports an allocation of `size` bytes to MallocGuard.
// Call this from custom allocator hooks (e.g., TCMalloc NewHook).
//
// While this function is not realtime safe (after all, we're already in a code
// path that allocates memory), it does its best to avoid unnecessary blocking.
void ReportAllocation(size_t size);

// Convenience class to enable/disable the malloc hook based on the lifetime of
// the instance.
//
// Example:
//   {
//     ScopedMallocHook malloc_hook; // Could be in any thread, but needs to
//                                   // stay alive for the duration of
//                                   // the realtime thread.
//     icon::MallocGuard malloc_guard; // Must be *in* the realtime thread.
//     while(true){
//       // do work
//     }
//   }
class ScopedMallocGuardHook {
 public:
  // Set `ignore` to true to make this instance do nothing. This is useful for
  // conditional activation of the ScopedMallocGuardHook. Otherwise you would
  // need to duplicate the code where the malloc hook should be
  // active/not-active. For example pass a flag from a config file to this
  // constructor.
  explicit ScopedMallocGuardHook(bool ignore = false)
      INTRINSIC_NON_REALTIME_ONLY : ignore_(ignore) {
    if (!ignore) {
      if (!InstallMallocGuardHooks()) [[unlikely]] {
        RtSafeLog("FATAL: Failed to install malloc guard hooks.");
        std::abort();
      }
    }
  }
  ~ScopedMallocGuardHook() INTRINSIC_NON_REALTIME_ONLY {
    if (!ignore_) {
      if (!UninstallMallocGuardHooks()) [[unlikely]] {
        RtSafeLog("FATAL: Failed to uninstall malloc guard hooks.");
        std::abort();
      }
    }
  }

 private:
  const bool ignore_;
};

// Convenience class to enable/disable a malloc guard based on the lifetime of
// the instance for the current thread. This class can be instantiated multiple
// times in the same callstack. `IsMallocGuarded()` will return true if at least
// one `MallocGuard` instance exists in the current callstack of the current
// thread.
class MallocGuard {
 public:
  // Set `ignore` to true to make this instance do nothing. This is useful for
  // conditional activation of the MallocGuard. Otherwise you would need to
  // duplicate the code where the malloc guard should be active/not-active. For
  // example pass a flag from a config file to this constructor.
  explicit MallocGuard(bool ignore = false) INTRINSIC_CHECK_REALTIME_SAFE;
  ~MallocGuard() INTRINSIC_CHECK_REALTIME_SAFE;
  // Returns true if at least one `MallocGuard` instance exists in the current
  // callstack of the current thread.
  static bool IsMallocGuarded() INTRINSIC_CHECK_REALTIME_SAFE;

 private:
  const bool ignore_;
};

// Convenience class to temporarily ignore allocations even if a MallocGuard
// is active in the current thread.
class ScopedMallocGuardIgnore {
 public:
  ScopedMallocGuardIgnore();
  ~ScopedMallocGuardIgnore();

 private:
  [[maybe_unused]] bool disabled_rtsan_ = false;
};

// Sets a denylist of library names that should NOT be intercepted via
// dynamic GOT/PLT hooking. The library name is matched against the dlpi_name
// provided by dl_iterate_phdr using exact matching.
// Note that the dlpi_name depends on how the library was loaded (e.g., an
// absolute path, a relative path, or just the filename). You must provide the
// exact string that dl_iterate_phdr sees to successfully exclude the library.
void INTRINSIC_NON_REALTIME_ONLY
SetMallocGuardDenylist(const std::unordered_set<std::string>& denylist);

// Gets the denylist of library names that should NOT be intercepted via
// dynamic GOT/PLT hooking.
std::unordered_set<std::string> INTRINSIC_NON_REALTIME_ONLY
GetMallocGuardDenylist();

// Sets the global malloc guard reaction. SetThreadLocalMallocGuardReaction()
// overrides this for the current thread.
void SetGlobalMallocGuardReaction(MallocGuardReaction reaction)
    INTRINSIC_CHECK_REALTIME_SAFE;

// Sets the thread local guard reaction. Has precedence over the global
// variable. Reset to use global variable again by passing in a std::nullopt.
void SetThreadLocalMallocGuardReaction(
    std::optional<MallocGuardReaction> reaction) INTRINSIC_CHECK_REALTIME_SAFE;
// Returns the thread local malloc guard reaction.
std::optional<MallocGuardReaction> GetThreadLocalMallocGuardReaction()
    INTRINSIC_CHECK_REALTIME_SAFE;

// Returns either the thread local reaction or, if that is not set, the global
// reaction.
MallocGuardReaction GetCurrentMallocGuardReaction()
    INTRINSIC_CHECK_REALTIME_SAFE;

// Stored violations from current thread in case
// `SetGlobalMallocGuardReaction()`/`SetThreadLocalMallocGuardReaction()` was
// set to `MallocGuardReaction::kStoreViolation`.
const MallocGuardViolations& GetThreadLocalMallocViolations()
    INTRINSIC_CHECK_REALTIME_SAFE;

// Reset stored violations in current thread.
void ResetThreadLocalMallocViolations() INTRINSIC_CHECK_REALTIME_SAFE;

// Convenience class to
// - set a thread local malloc reaction as long as this class instance is alive.
// - reset the thread local violation data.
// - restore the previous reaction and the reaction data when this class
//   instance goes out of scope.
//
// Note: Does *not* enable the malloc guard (hook) itself. So, this can be used,
// even if the malloc guard (hook) is not enabled. It will just not record any
// memory allocations in this case. Enable the malloc guard hook by using
// `ScopedMallocGuardHook` or `InstallMallocGuardHooks()` and enable the malloc
// guard by using `MallocGuard`.
class ScopedThreadLocalReaction {
 public:
  explicit ScopedThreadLocalReaction(MallocGuardReaction reaction)
      INTRINSIC_CHECK_REALTIME_SAFE;
  ~ScopedThreadLocalReaction() INTRINSIC_CHECK_REALTIME_SAFE;

 private:
  MallocGuardViolations previous_violations_;
  std::optional<MallocGuardReaction> previous_reaction_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_UTILS_MALLOC_GUARD_H_
