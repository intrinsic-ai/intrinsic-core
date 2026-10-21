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

#include "intrinsic/icon/utils/malloc_guard.h"

#include <asm/unistd_64.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <link.h>
#include <pthread.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_set>

#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/realtime_stack_trace.h"
#include "malloc_guard_plthook_elf.h"

extern "C" {
void* malloc_guard_intercept_malloc(std::size_t);
void* malloc_guard_intercept_calloc(std::size_t, std::size_t);
void* malloc_guard_intercept_realloc(void*, std::size_t);
void malloc_guard_intercept_free(void*);
int malloc_guard_intercept_posix_memalign(void**, std::size_t, std::size_t);
void* malloc_guard_intercept_dlopen(const char*, int);
void* malloc_guard_intercept_dlmopen(Lmid_t, const char*, int);
}

namespace intrinsic::icon {

static const HookTarget kTargets[] = {
    {
        .name = "malloc",
        .hook_func = (void*)malloc_guard_intercept_malloc,
    },
    {
        .name = "calloc",
        .hook_func = (void*)malloc_guard_intercept_calloc,
    },
    {
        .name = "realloc",
        .hook_func = (void*)malloc_guard_intercept_realloc,
    },
    {
        .name = "free",
        .hook_func = (void*)malloc_guard_intercept_free,
    },
    {
        .name = "posix_memalign",
        .hook_func = (void*)malloc_guard_intercept_posix_memalign,
    },
    {
        .name = "dlopen",
        .hook_func = (void*)malloc_guard_intercept_dlopen,
    },
    {
        .name = "dlmopen",
        .hook_func = (void*)malloc_guard_intercept_dlmopen,
    }};

static constexpr const char* kAllocatorSymbols[] = {
    "malloc", "calloc", "realloc", "free", "posix_memalign"};

namespace {

#if defined(__has_feature)
#define INTRINSIC_HAS_SANITIZER                                           \
  (__has_feature(address_sanitizer) || __has_feature(memory_sanitizer) || \
   __has_feature(thread_sanitizer))
#else
#define INTRINSIC_HAS_SANITIZER 0
#endif

// ASan, MSan and TSan replace the allocator, which conflicts with the malloc
// hooks, so `MallocGuard` is a no-op when used with them.
#if defined(ADDRESS_SANITIZER) || defined(__SANITIZE_ADDRESS__) || \
    defined(MEMORY_SANITIZER) || defined(__SANITIZE_MEMORY__) ||   \
    defined(THREAD_SANITIZER) || defined(__SANITIZE_THREAD__) ||   \
    INTRINSIC_HAS_SANITIZER
constexpr bool kMallocGuardDisabledByAllocatorSanitizer = true;
#else
constexpr bool kMallocGuardDisabledByAllocatorSanitizer = false;
#endif

#undef INTRINSIC_HAS_SANITIZER

// Count of how many times the malloc guard was enabled. Decreased when it was
// disabled again.
volatile std::atomic_int malloc_guard_install_counter = 0;
static_assert(decltype(malloc_guard_install_counter)::is_always_lock_free);
// Mutex to protect concurrent enabling/disabling of the malloc guard.
std::mutex malloc_guard_install_counter_mutex;

std::unordered_set<std::string> malloc_guard_denylist;
std::mutex malloc_guard_denylist_mutex;

// The global malloc guard reaction that is used when no thread local reaction
// is set.
volatile std::atomic<MallocGuardReaction> global_malloc_guard_reaction =
    MallocGuardReaction::kLog;
static_assert(decltype(global_malloc_guard_reaction)::is_always_lock_free);
// Thread local malloc guard reaction that takes precedence over the global
// reaction, but is optional.
thread_local std::optional<MallocGuardReaction>
    thread_local_malloc_guard_reaction = {};

// Custom callback storage
std::mutex global_malloc_guard_callback_mutex;
MallocGuardCallback global_malloc_guard_callback = nullptr;
thread_local MallocGuardCallback thread_local_malloc_guard_callback = nullptr;

// Keeps count how many MallocGuards were instantiated in a single thread.
// This variable needs to be an atomic. Otherwise, it can happen that the
// counter value is read before it is written in the malloc hook (maybe because
// the malloc function is exchanged at link time?). This issues occurred in one
// of the checks of the `CheckMallocHandlerInstalled()` function.
std::atomic_int64_t thread_local local_malloc_guard_counter = 0;
static_assert(decltype(local_malloc_guard_counter)::is_always_lock_free);
// Global variables to make sure that the compiler doesn't
// optimize the code used for testing that the malloc hook works.
void* volatile ensure_hooks_registered_malloc_ptr;
int* volatile ensure_hooks_registered_new_ptr;
void* ensure_hooks_registered_posix_memalign_ptr;

MallocGuardViolations& GetThreadLocalMallocViolationsPrivate() {
  static thread_local MallocGuardViolations malloc_guard_violations = {};
  return malloc_guard_violations;
}
constexpr size_t kMaxPosixThreadNameLen = 16;

std::array<char, kMaxPosixThreadNameLen> GetNameOfCurrentThread() {
  std::array<char, kMaxPosixThreadNameLen> buf{};
  int res =
      pthread_getname_np(pthread_self(), buf.data(), kMaxPosixThreadNameLen);
  if (res != 0) {
    buf[0] = '\0';
  }
  return buf;
}

thread_local int in_malloc_hook = 0;
struct ScopedInMallocHook {
  ScopedInMallocHook() { ++in_malloc_hook; }
  ~ScopedInMallocHook() { --in_malloc_hook; }
  ScopedInMallocHook(const ScopedInMallocHook&) = delete;
  ScopedInMallocHook& operator=(const ScopedInMallocHook&) = delete;
};
bool IsInMallocHook() { return in_malloc_hook > 0; }

}  // end anonymous namespace

void ReportAllocation(size_t size) {
  if constexpr (kMallocGuardDisabledByAllocatorSanitizer) {
    return;
  }
  // Prevent recursive loop due to printing in MallocGuardReaction::kAbort mode.
  if (IsInMallocHook()) [[unlikely]] {
    return;
  }
  ScopedInMallocHook in_malloc_hook;
  if (MallocGuard::IsMallocGuarded()) [[unlikely]] {
    const MallocGuardReaction reaction = GetCurrentMallocGuardReaction();

    switch (reaction) {
      case MallocGuardReaction::kNone:
        break;
      case MallocGuardReaction::kLog: {
        auto name = GetNameOfCurrentThread();
        RtSafeLog("ERROR: Encountered malloc in realtime thread '", name.data(),
                  "'. Allocated ", size, " bytes.");
        break;
      }
      case MallocGuardReaction::kLogWithTrace: {
        auto name = GetNameOfCurrentThread();
        auto stacktrace = intrinsic::icon::GenerateRtErrorStackTrace();
        RtSafeLog("ERROR: Encountered malloc in realtime thread '", name.data(),
                  "'. Allocated ", size, " bytes.\nStacktrace:\n",
                  std::string_view(stacktrace));
        break;
      }
      case MallocGuardReaction::kStoreViolationWithTrace: {
        MallocGuardViolations& violations =
            GetThreadLocalMallocViolationsPrivate();
        auto start = std::chrono::steady_clock::now();
        StackTrace stack_trace;
        stack_trace.num_frames =
            backtrace(stack_trace.stack_trace, kMaxMallocGuardStackFrames);
        violations.latest_violation_stack_trace = stack_trace;
        auto dur = std::chrono::steady_clock::now() - start;
        violations.stack_creation_duration = dur;
      }
        [[fallthrough]];
      case MallocGuardReaction::kStoreViolation: {
        MallocGuardViolations& violations =
            GetThreadLocalMallocViolationsPrivate();
        violations.num_violations++;
        violations.allocated_bytes += size;
      } break;
      case MallocGuardReaction::kAbort: {
        auto name = GetNameOfCurrentThread();
        RtSafeLog("FATAL: Encountered malloc in realtime thread '", name.data(),
                  "'. Bailing out.");
        std::exit(1);
        break;
      }
      case MallocGuardReaction::kCustomCallback: {
        MallocGuardCallback cb = thread_local_malloc_guard_callback;
        if (cb) {
          cb(size);
        } else {
          std::lock_guard<std::mutex> lock(global_malloc_guard_callback_mutex);
          if (global_malloc_guard_callback) {
            global_malloc_guard_callback(size);
          }
        }
        break;
      }
      default:
        break;
    }
  }
}

namespace internal {

void RtSafeLogImpl(const char* msg) {
  (void)::write(STDERR_FILENO, msg, std::strlen(msg));
}

void RtSafeLogImpl(std::string_view msg) {
  (void)::write(STDERR_FILENO, msg.data(), msg.size());
}

void RtSafeLogImpl(size_t val) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%zu", val);
  RtSafeLogImpl(std::string_view(buf));
}

void RtSafeLogImpl(int64_t val) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%ld", val);
  RtSafeLogImpl(std::string_view(buf));
}

void RtSafeLogImpl(const void* ptr) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%p", ptr);
  RtSafeLogImpl(std::string_view(buf));
}

}  // namespace internal

bool CheckMallocHandlerInstalled(bool silent) INTRINSIC_NON_REALTIME_ONLY {
  if constexpr (kMallocGuardDisabledByAllocatorSanitizer) {
    return true;
  }
  bool success = true;
  ResetThreadLocalMallocViolations();
  auto installed_reaction = thread_local_malloc_guard_reaction;
  thread_local_malloc_guard_reaction = MallocGuardReaction::kStoreViolation;

  MallocGuard guard;
  {  // do a malloc
    ensure_hooks_registered_malloc_ptr = malloc(32);

    // ensure we saw the malloc
    if (GetThreadLocalMallocViolationsPrivate().num_violations != 1) {
      if (!silent) {
        RtSafeLog(
            "malloc() handler not installed correctly: call to raw malloc did "
            "not increment counter.");
      }
      success = false;
    }
    if (GetThreadLocalMallocViolationsPrivate().allocated_bytes != 32) {
      if (!silent) {
        RtSafeLog(
            "malloc() handler not installed correctly: call to raw malloc did "
            "not count 32 bytes of requested memory.");
      }
      success = false;
    }
    // free the memory
    free(const_cast<void*>(ensure_hooks_registered_malloc_ptr));
  }

  {  // do a new
    ensure_hooks_registered_new_ptr = new int;

    // ensure we saw the new
    if (GetThreadLocalMallocViolationsPrivate().num_violations != 2) {
      if (!silent) {
        RtSafeLog(
            "malloc() handler not installed correctly: call to new did not "
            "increment counter.");
      }
      success = false;
    }
    if (GetThreadLocalMallocViolationsPrivate().allocated_bytes !=
        32 + sizeof(int)) {
      if (!silent) {
        RtSafeLog(
            "malloc() handler not installed correctly: call to new did not "
            "count correct new bytes for an int.");
      }
      success = false;
    }
    // free the memory
    delete ensure_hooks_registered_new_ptr;
  }

  {  // check posix aligned memory allocation
    constexpr size_t kAlignBytes = 16;
    constexpr size_t kSize = 32;
    if (posix_memalign(&ensure_hooks_registered_posix_memalign_ptr, kAlignBytes,
                       kSize) != 0) {
      if (!silent) {
        RtSafeLog("posix_memalign() failed.");
      }
    }

    // ensure we saw the malloc
    if (GetThreadLocalMallocViolationsPrivate().num_violations != 3) {
      if (!silent) {
        RtSafeLog(
            "posix_memalign() handler not installed correctly: direct call to "
            "posix_memalign did not increment counter.");
      }
      success = false;
    }
    // free the memory
    free(ensure_hooks_registered_posix_memalign_ptr);
  }

  {  // check realloc is caught
    ensure_hooks_registered_malloc_ptr = malloc(32);
    ensure_hooks_registered_malloc_ptr =
        realloc(const_cast<void*>(ensure_hooks_registered_malloc_ptr), 64);
    // ensure we saw the realloc and malloc
    if (GetThreadLocalMallocViolationsPrivate().num_violations != 5) {
      if (!silent) {
        RtSafeLog(
            "realloc() handler not installed correctly: call to raw realloc "
            "did not increment counter.");
      }
      success = false;
    }
    // free the memory
    free(const_cast<void*>(ensure_hooks_registered_malloc_ptr));
  }

  {  // do an array new
    const size_t bytes_before =
        GetThreadLocalMallocViolationsPrivate().allocated_bytes.load();
    ensure_hooks_registered_new_ptr = new int[10];

    // ensure we saw the new
    if (GetThreadLocalMallocViolationsPrivate().num_violations != 6) {
      if (!silent) {
        RtSafeLog(
            "malloc() handler not installed correctly: call to array new did "
            "not increment counter.");
      }
      success = false;
    }
    if (GetThreadLocalMallocViolationsPrivate().allocated_bytes !=
        (sizeof(int) * 10) + bytes_before) {
      if (!silent) {
        RtSafeLog(
            "malloc() handler not installed correctly: call to array new did "
            "not count correct new bytes for an array of int.");
      }
      success = false;
    }
    // free the memory
    delete[] ensure_hooks_registered_new_ptr;
  }

  if (success) {
    if (!silent) {
      RtSafeLog("Installed malloc hook for MallocGuard successfully.");
    }
  }
  ResetThreadLocalMallocViolations();
  thread_local_malloc_guard_reaction = installed_reaction;
  if (!success) {
    return false;
  }
  return true;
}

bool InstallMallocGuardHooks() {
  if constexpr (kMallocGuardDisabledByAllocatorSanitizer) {
    static std::once_flag log_once;
    std::call_once(log_once, []() {
      RtSafeLog(
          "INFO: MallocGuard is disabled because an allocator-replacing "
          "sanitizer (ASan/MSan/TSan) is active.\n");
    });
    return true;
  }
  // We intentionally lock the mutex for the entire duration of this function
  // to prevent race conditions during initialization, not just to protect the
  // counter increment.
  std::lock_guard<std::mutex> lock(malloc_guard_install_counter_mutex);
  int new_value = ++malloc_guard_install_counter;
  if (new_value != 1) {
    // MallocGuard is either disabled, or already enabled, nothing to do.
    return true;
  }

  static std::once_flag init_once;
  std::call_once(init_once, []() { intrinsic::icon::InitRtStackTrace(); });

  const bool have_custom_setup = MallocGuardCustomSetup != nullptr;
  const bool have_custom_teardown = MallocGuardCustomTeardown != nullptr;
  if (have_custom_setup != have_custom_teardown) {
    RtSafeLog(
        "There is a definition for only one of `MallocGuardCustomSetup()` and "
        "`MallocGuardCustomTeardown()`. You must provide both or neither!");
    std::exit(1);
  }
  if (MallocGuardCustomSetup != nullptr && !MallocGuardCustomSetup()) {
    RtSafeLog("Failed to set up static allocator hook.");
    --malloc_guard_install_counter;
    return false;
  }

  if (!intrinsic::icon::InstallDynamicGotHooks(kTargets)) {
    if (MallocGuardCustomTeardown != nullptr && !MallocGuardCustomTeardown()) {
      RtSafeLog(
          "Failed to tear down static allocator hook after an error in "
          "InstallMallocGuardHooks().");
    }

    --malloc_guard_install_counter;
    return false;
  }

  if (auto s = CheckMallocHandlerInstalled(false); !s) {
    RtSafeLog("Dynamic GOT hooks failed to intercept malloc.");
    --malloc_guard_install_counter;
    intrinsic::icon::UninstallDynamicGotHooks(kTargets);
    if (MallocGuardCustomTeardown != nullptr && !MallocGuardCustomTeardown()) {
      RtSafeLog(
          "Failed to tear down static allocator hook after an error after "
          "CheckMallocHandlerInstalled().");
    }
    return s;
  }

  return true;
}

bool UninstallMallocGuardHooks() {
  if constexpr (kMallocGuardDisabledByAllocatorSanitizer) {
    return true;
  }
  std::lock_guard<std::mutex> lock(malloc_guard_install_counter_mutex);
  int new_value = --malloc_guard_install_counter;
  if (new_value == 0) {
    RtSafeLog("Removing malloc_guard hook.");
    bool got_ok = intrinsic::icon::UninstallDynamicGotHooks(kTargets);
    bool custom_teardown_ok =
        (MallocGuardCustomTeardown == nullptr || MallocGuardCustomTeardown());
    if (!custom_teardown_ok) {
      RtSafeLog(
          "Failed to tear down static allocator hook in "
          "UninstallMallocGuardHooks().");
      custom_teardown_ok = false;
    }

    if (!got_ok || !custom_teardown_ok) {
      return false;
    }
  }

  if (new_value < 0) {
    // Restore the counter to a valid state and then report the error. This also
    // works when this function is called in a parallel thread that decreases
    // the value below zero.
    malloc_guard_install_counter = 0;
    return false;
  }

  return true;
}

namespace {

[[gnu::noinline]] INTRINSIC_SUPPRESS_REALTIME_CHECK void
AssertHooksAndInitializeLog() {
  if (!AreMallocGuardHooksInstalled()) {
    RtSafeLog(
        "Malloc hook is not enabled. Call InstallMallocGuardHooks() or use "
        "ScopedMallocGuardHook.");
    std::exit(1);
  }

  thread_local bool log_initialized = false;
  if (!log_initialized) {
    // Trigger any one-time log allocations before entering guarded code
    intrinsic::RtLogInitForThisThread();
    log_initialized = true;
  }
}

}  // namespace

MallocGuard::MallocGuard(bool ignore)
    : ignore_(ignore || kMallocGuardDisabledByAllocatorSanitizer) {
  if (!ignore_) {
    AssertHooksAndInitializeLog();

#if __has_feature(realtime_sanitizer)
    if (local_malloc_guard_counter == 0) {
      __rtsan_realtime_enter();
    }
#endif
    local_malloc_guard_counter++;
  }
}

MallocGuard::~MallocGuard() {
  if (!ignore_) {
    local_malloc_guard_counter--;
#if __has_feature(realtime_sanitizer)
    if (local_malloc_guard_counter == 0) {
      __rtsan_realtime_exit();
    }
#endif
    if (local_malloc_guard_counter < 0) {
      RtSafeLog(
          "~MallocGuard() was called more often than MallocGuard(). This is a "
          "bug!");
      std::exit(1);
    }
  }
}

thread_local std::atomic_int64_t local_ignore_counter = 0;

bool MallocGuard::IsMallocGuarded() {
  return local_malloc_guard_counter > 0 && local_ignore_counter == 0;
}

ScopedMallocGuardIgnore::ScopedMallocGuardIgnore() {
  if constexpr (kMallocGuardDisabledByAllocatorSanitizer) {
    return;
  }
  local_ignore_counter++;
#if __has_feature(realtime_sanitizer)
  if (local_malloc_guard_counter > 0 && local_ignore_counter == 1) {
    disabled_rtsan_ = true;
    __rtsan_disable();
  }
#endif
}

ScopedMallocGuardIgnore::~ScopedMallocGuardIgnore() {
  if constexpr (kMallocGuardDisabledByAllocatorSanitizer) {
    return;
  }
  local_ignore_counter--;
#if __has_feature(realtime_sanitizer)
  if (disabled_rtsan_ && local_ignore_counter == 0) {
    __rtsan_enable();
    disabled_rtsan_ = false;
  }
#endif
  if (local_ignore_counter < 0) {
    RtSafeLog(
        "~ScopedMallocGuardIgnore() called more often than "
        "ScopedMallocGuardIgnore()!");
    std::exit(1);
  }
}

void INTRINSIC_NON_REALTIME_ONLY
SetMallocGuardDenylist(const std::unordered_set<std::string>& denylist) {
  if (MallocGuardCustomSetup != nullptr &&
      MallocGuardCustomTeardown != nullptr) {
    RtSafeLog(
        "ERROR: This binary defines MallocGuardCustomSetup() and "
        "MallocGuardCustomTeardown(), so MallocGuard does not support library "
        "denylisting.");
    std::exit(1);
  }
  ScopedMallocGuardIgnore ignore;
  std::lock_guard<std::mutex> lock(malloc_guard_denylist_mutex);
  malloc_guard_denylist = denylist;
}

std::unordered_set<std::string> INTRINSIC_NON_REALTIME_ONLY
GetMallocGuardDenylist() {
  std::lock_guard<std::mutex> lock(malloc_guard_denylist_mutex);
  return malloc_guard_denylist;
}

void SetGlobalMallocGuardReaction(MallocGuardReaction reaction) {
  global_malloc_guard_reaction.store(reaction, std::memory_order_relaxed);
}

void SetThreadLocalMallocGuardReaction(
    std::optional<MallocGuardReaction> reaction) {
  thread_local_malloc_guard_reaction = reaction;
}

std::optional<MallocGuardReaction> GetThreadLocalMallocGuardReaction() {
  return thread_local_malloc_guard_reaction;
}

void SetGlobalMallocGuardCallback(MallocGuardCallback callback) {
  std::lock_guard<std::mutex> lock(global_malloc_guard_callback_mutex);
  global_malloc_guard_callback = callback;
}

void SetThreadLocalMallocGuardCallback(MallocGuardCallback callback) {
  thread_local_malloc_guard_callback = callback;
}

MallocGuardReaction GetCurrentMallocGuardReaction() {
  return GetThreadLocalMallocGuardReaction().value_or(
      global_malloc_guard_reaction.load(std::memory_order_relaxed));
}

const MallocGuardViolations& GetThreadLocalMallocViolations() {
  return GetThreadLocalMallocViolationsPrivate();
}

void ResetThreadLocalMallocViolations() {
  GetThreadLocalMallocViolationsPrivate().num_violations = 0;
  GetThreadLocalMallocViolationsPrivate().allocated_bytes = 0;
  GetThreadLocalMallocViolationsPrivate().latest_violation_stack_trace.reset();
  GetThreadLocalMallocViolationsPrivate().stack_creation_duration.reset();
}

bool AreMallocGuardHooksInstalled() { return malloc_guard_install_counter > 0; }

ScopedThreadLocalReaction::ScopedThreadLocalReaction(
    MallocGuardReaction reaction)
    : previous_reaction_(GetThreadLocalMallocGuardReaction()) {
  previous_violations_ = GetThreadLocalMallocViolations();
  ResetThreadLocalMallocViolations();
  SetThreadLocalMallocGuardReaction(reaction);
}

ScopedThreadLocalReaction::~ScopedThreadLocalReaction() {
  GetThreadLocalMallocViolationsPrivate() = previous_violations_;
  SetThreadLocalMallocGuardReaction(previous_reaction_);
}

}  // namespace intrinsic::icon

extern "C" {

static char calloc_fallback_buffer[1024];
static std::atomic<size_t> calloc_fallback_pos{0};

// malloc handler
void* malloc_guard_intercept_malloc(std::size_t size) {
  typedef void*(MallocFunction)(std::size_t);
  static std::atomic<MallocFunction*> real_malloc{nullptr};
  static thread_local bool malloc_in_init = false;

  MallocFunction* func = real_malloc.load(std::memory_order_acquire);
  if (!func) {
    // `dlsym` internally calls `calloc` and `malloc` to allocate memory.
    // To prevent infinite recursion when we call `dlsym` to find the real
    // allocator functions, we use a static fallback buffer to serve any
    // allocations requested during this initialization phase.
    // We don't need this handling for `realloc` or `posix_memalign` because
    // `dlsym` does not use them internally.
    if (malloc_in_init) {
      size_t current_pos =
          calloc_fallback_pos.fetch_add(size, std::memory_order_relaxed);
      if (current_pos + size > sizeof(calloc_fallback_buffer)) {
        intrinsic::icon::RtSafeLog("Malloc fallback buffer exhausted.");
        std::exit(1);
      }
      return calloc_fallback_buffer + current_pos;
    }
    malloc_in_init = true;
    // We first use RTLD_DEFAULT to find the globally active definition of
    // malloc (e.g., TCMalloc when linked into the main executable, or glibc's
    // malloc). If RTLD_DEFAULT returns a pointer to this very interceptor
    // function (which can happen if our symbol was interposed first), we fall
    // back to RTLD_NEXT to find the next definition in the dynamic linker's
    // search order and avoid infinite recursion.
    func = (MallocFunction*)::dlsym(RTLD_DEFAULT, "malloc");
    if (func == &malloc_guard_intercept_malloc) {
      func = (MallocFunction*)::dlsym(RTLD_NEXT, "malloc");
    }
    malloc_in_init = false;
    if (!func) {
      intrinsic::icon::RtSafeLog("Could not find symbol for malloc.");
      std::exit(1);
    }
    real_malloc.store(func, std::memory_order_release);
  }

  if (intrinsic::icon::AreMallocGuardHooksInstalled()) [[unlikely]] {
    intrinsic::icon::ReportAllocation(size);
  }
  intrinsic::icon::ScopedMallocGuardIgnore ignore;
  return func(size);
}

// realloc handler
void* malloc_guard_intercept_realloc(void* ptr, std::size_t size) {
  typedef void*(ReallocFunction)(void* ptr, std::size_t);
  static std::atomic<ReallocFunction*> real_realloc{nullptr};

  ReallocFunction* func = real_realloc.load(std::memory_order_acquire);
  if (!func) {
    // We first use RTLD_DEFAULT to find the globally active definition of
    // realloc (e.g., TCMalloc when linked into the main executable, or glibc's
    // realloc). If RTLD_DEFAULT returns a pointer to this very interceptor
    // function (which can happen if our symbol was interposed first), we fall
    // back to RTLD_NEXT to find the next definition in the dynamic linker's
    // search order and avoid infinite recursion.
    func = (ReallocFunction*)::dlsym(RTLD_DEFAULT, "realloc");
    if (func == &malloc_guard_intercept_realloc) {
      func = (ReallocFunction*)::dlsym(RTLD_NEXT, "realloc");
    }
    if (!func) {
      intrinsic::icon::RtSafeLog("Could not find symbol for realloc.");
      std::exit(1);
    }
    real_realloc.store(func, std::memory_order_release);
  }

  if (intrinsic::icon::AreMallocGuardHooksInstalled()) [[unlikely]] {
    intrinsic::icon::ReportAllocation(size);
  }
  intrinsic::icon::ScopedMallocGuardIgnore ignore;
  return func(ptr, size);
}

// posix_memalign handler
int malloc_guard_intercept_posix_memalign(void** memptr, size_t alignment,
                                          size_t size) {
  typedef int(PosixMemalignFunction)(void**, size_t, size_t);
  static std::atomic<PosixMemalignFunction*> real_posix_memalign{nullptr};

  PosixMemalignFunction* func =
      real_posix_memalign.load(std::memory_order_acquire);
  if (!func) {
    // We first use RTLD_DEFAULT to find the globally active definition of
    // posix_memalign (e.g., TCMalloc when linked into the main executable, or
    // glibc's posix_memalign). If RTLD_DEFAULT returns a pointer to this very
    // interceptor function (which can happen if our symbol was interposed
    // first), we fall back to RTLD_NEXT to find the next definition in the
    // dynamic linker's search order and avoid infinite recursion.
    func = (PosixMemalignFunction*)::dlsym(RTLD_DEFAULT, "posix_memalign");
    if (func == &malloc_guard_intercept_posix_memalign) {
      func = (PosixMemalignFunction*)::dlsym(RTLD_NEXT, "posix_memalign");
    }
    if (!func) {
      intrinsic::icon::RtSafeLog("Could not find symbol for posix_memalign.");
      std::exit(1);
    }
    real_posix_memalign.store(func, std::memory_order_release);
  }

  if (intrinsic::icon::AreMallocGuardHooksInstalled()) [[unlikely]] {
    intrinsic::icon::ReportAllocation(size);
  }
  intrinsic::icon::ScopedMallocGuardIgnore ignore;
  return func(memptr, alignment, size);
}

// calloc handler
void* malloc_guard_intercept_calloc(std::size_t nmemb, std::size_t size) {
  typedef void*(CallocFunction)(std::size_t, std::size_t);
  static std::atomic<CallocFunction*> real_calloc{nullptr};
  static thread_local bool calloc_in_init = false;

  CallocFunction* func = real_calloc.load(std::memory_order_acquire);
  if (!func) {
    if (calloc_in_init) {
      size_t alloc_size = nmemb * size;
      size_t current_pos =
          calloc_fallback_pos.fetch_add(alloc_size, std::memory_order_relaxed);
      if (current_pos + alloc_size > sizeof(calloc_fallback_buffer)) {
        intrinsic::icon::RtSafeLog("Calloc fallback buffer exhausted.");
        std::exit(1);
      }
      return calloc_fallback_buffer + current_pos;
    }
    calloc_in_init = true;
    // We first use RTLD_DEFAULT to find the globally active definition of
    // calloc (e.g., TCMalloc when linked into the main executable, or glibc's
    // calloc). If RTLD_DEFAULT returns a pointer to this very interceptor
    // function (which can happen if our symbol was interposed first), we fall
    // back to RTLD_NEXT to find the next definition in the dynamic linker's
    // search order and avoid infinite recursion.
    func = (CallocFunction*)::dlsym(RTLD_DEFAULT, "calloc");
    if (func == &malloc_guard_intercept_calloc) {
      func = (CallocFunction*)::dlsym(RTLD_NEXT, "calloc");
    }
    calloc_in_init = false;
    if (!func) {
      intrinsic::icon::RtSafeLog("Could not find symbol for calloc.");
      std::exit(1);
    }
    real_calloc.store(func, std::memory_order_release);
  }

  if (intrinsic::icon::AreMallocGuardHooksInstalled()) [[unlikely]] {
    intrinsic::icon::ReportAllocation(nmemb * size);
  }
  intrinsic::icon::ScopedMallocGuardIgnore ignore;
  return func(nmemb, size);
}

// free handler
void malloc_guard_intercept_free(void* ptr) {
  if (ptr >= calloc_fallback_buffer &&
      ptr < calloc_fallback_buffer + sizeof(calloc_fallback_buffer)) {
    return;  // Ignore fallback buffer memory
  }
  typedef void(FreeFunction)(void*);
  static std::atomic<FreeFunction*> real_free{nullptr};
  static thread_local bool free_in_init = false;

  FreeFunction* func = real_free.load(std::memory_order_acquire);
  if (!func) {
    if (free_in_init) {
      return;
    }
    free_in_init = true;
    // We first use RTLD_DEFAULT to find the globally active definition of
    // free (e.g., TCMalloc when linked into the main executable, or glibc's
    // free). If RTLD_DEFAULT returns a pointer to this very interceptor
    // function (which can happen if our symbol was interposed first), we fall
    // back to RTLD_NEXT to find the next definition in the dynamic linker's
    // search order and avoid infinite recursion.
    func = (FreeFunction*)::dlsym(RTLD_DEFAULT, "free");
    if (func == &malloc_guard_intercept_free) {
      func = (FreeFunction*)::dlsym(RTLD_NEXT, "free");
    }
    free_in_init = false;
    if (!func) {
      return;
    }
    real_free.store(func, std::memory_order_release);
  }
  func(ptr);
}

static thread_local int dl_intercept_depth = 0;

struct ScopedDlopenIntercept {
  ScopedDlopenIntercept() { ++dl_intercept_depth; }
  ~ScopedDlopenIntercept() { --dl_intercept_depth; }
  ScopedDlopenIntercept(const ScopedDlopenIntercept&) = delete;
  ScopedDlopenIntercept& operator=(const ScopedDlopenIntercept&) = delete;
};

// dlopen handler
void* malloc_guard_intercept_dlopen(const char* filename, int flags) {
  typedef void*(DlopenFunction)(const char*, int);
  static std::atomic<DlopenFunction*> real_dlopen{nullptr};

  DlopenFunction* func = real_dlopen.load(std::memory_order_acquire);
  if (!func) {
    // We first use RTLD_DEFAULT to find the globally active definition of
    // dlopen. If RTLD_DEFAULT returns a pointer to this very interceptor
    // function (which can happen if our symbol was interposed first), we fall
    // back to RTLD_NEXT to find the next definition in the dynamic linker's
    // search order and avoid infinite recursion.
    func = (DlopenFunction*)::dlsym(RTLD_DEFAULT, "dlopen");
    if (func == &malloc_guard_intercept_dlopen) {
      func = (DlopenFunction*)::dlsym(RTLD_NEXT, "dlopen");
    }
    if (!func) {
      intrinsic::icon::RtSafeLog("Could not find symbol for dlopen.");
      std::exit(1);
    }
    real_dlopen.store(func, std::memory_order_release);
  }

  // Bypass the interceptor if we are currently inside a plthook operation.
  // `plthook_open()` internally calls `dlopen(..., RTLD_NOLOAD)` while
  // iterating over loaded libraries, which must not trigger custom-allocator
  // checks or recursive GOT re-hooking.
  if (intrinsic::icon::IsInPlthookOperation()) {
    return func(filename, flags);
  }

  ScopedDlopenIntercept dlopen_intercept;
  void* handle = func(filename, flags);

  if (handle && filename != nullptr &&
      intrinsic::icon::AreMallocGuardHooksInstalled()) [[unlikely]] {
    if (flags & RTLD_DEEPBIND) {
      intrinsic::icon::RtSafeLog(
          "FATAL ERROR: MallocGuard does not support loading libraries with "
          "RTLD_DEEPBIND.");
      std::exit(1);
    }
    intrinsic::icon::ScopedMallocGuardIgnore ignore;

    // Check if the newly loaded library defines its own custom allocator
    // functions. We compare the library's symbol address (`local_sym`) against
    // * `RTLD_DEFAULT`, the globally active allocator that our interceptor
    //   functions use. However, certain library configurations can cause this
    //   to be different, even though the function calls in that library still
    //   resolve to the global definitions. To cover those cases, we also
    //   check...
    // * `RTLD_NEXT`, which should *also* resolve to that same allocator.
    // If `local_sym` is different from both, the library uses a different
    // allocator and we raise an error.
    for (const char* sym : intrinsic::icon::kAllocatorSymbols) {
      void* local_sym = ::dlsym(handle, sym);
      if (local_sym == nullptr) {
        continue;
      }
      void* global_sym = ::dlsym(RTLD_DEFAULT, sym);
      void* next_sym = ::dlsym(RTLD_NEXT, sym);
      if (local_sym != global_sym && local_sym != next_sym) {
        intrinsic::icon::RtSafeLog(
            "FATAL ERROR: Library '", filename,
            "' uses a different allocator than the main binary. MallocGuard "
            "does not support this, consider excluding the library from checks "
            "using `SetMallocGuardDenylist()`.");
        std::exit(1);
      }
    }

    // Only re-apply GOT hooks when the outermost load finishes.
    if (dl_intercept_depth == 1) {
      intrinsic::icon::InstallDynamicGotHooks(intrinsic::icon::kTargets);
    }
  }

  return handle;
}

// dlmopen handler
void* malloc_guard_intercept_dlmopen(Lmid_t lmid, const char* filename,
                                     int flags) {
  typedef void*(DlmopenFunction)(Lmid_t, const char*, int);
  static std::atomic<DlmopenFunction*> real_dlmopen{nullptr};

  DlmopenFunction* func = real_dlmopen.load(std::memory_order_acquire);
  if (!func) {
    // We first use RTLD_DEFAULT to find the globally active definition of
    // dlmopen. If RTLD_DEFAULT returns a pointer to this very interceptor
    // function (which can happen if our symbol was interposed first), we fall
    // back to RTLD_NEXT to find the next definition in the dynamic linker's
    // search order and avoid infinite recursion.
    func = (DlmopenFunction*)::dlsym(RTLD_DEFAULT, "dlmopen");
    if (func == &malloc_guard_intercept_dlmopen) {
      func = (DlmopenFunction*)::dlsym(RTLD_NEXT, "dlmopen");
    }
    if (!func) {
      intrinsic::icon::RtSafeLog("Could not find symbol for dlmopen.");
      std::exit(1);
    }
    real_dlmopen.store(func, std::memory_order_release);
  }

  // Bypass the interceptor if we are currently inside a plthook operation.
  // `plthook_open()` internally calls `dlopen(..., RTLD_NOLOAD)` while
  // iterating over loaded libraries, which must not trigger custom-allocator
  // checks or recursive GOT re-hooking.
  if (intrinsic::icon::IsInPlthookOperation()) {
    return func(lmid, filename, flags);
  }

  ScopedDlopenIntercept dlopen_intercept;
  void* handle = func(lmid, filename, flags);

  if (handle && filename != nullptr &&
      intrinsic::icon::AreMallocGuardHooksInstalled()) [[unlikely]] {
    if (flags & RTLD_DEEPBIND) {
      intrinsic::icon::RtSafeLog(
          "FATAL ERROR: MallocGuard does not support loading libraries with "
          "RTLD_DEEPBIND.");
      std::exit(1);
    }
    intrinsic::icon::ScopedMallocGuardIgnore ignore;

    // Check if the newly loaded library defines its own custom allocator
    // functions. We compare the library's symbol address (`local_sym`) against
    // * `RTLD_DEFAULT`, the globally active allocator that our interceptor
    //   functions use. However, certain library configurations can cause this
    //   to be different, even though the function calls in that library still
    //   resolve to the global definitions. To cover those cases, we also
    //   check...
    // * `RTLD_NEXT`, which should *also* resolve to that same allocator.
    // If `local_sym` is different from both, the library uses a different
    // allocator and we raise an error.
    for (const char* sym : intrinsic::icon::kAllocatorSymbols) {
      void* local_sym = ::dlsym(handle, sym);
      if (local_sym != nullptr) {
        void* global_sym = ::dlsym(RTLD_DEFAULT, sym);
        void* next_sym = ::dlsym(RTLD_NEXT, sym);
        if (local_sym != global_sym && local_sym != next_sym) {
          intrinsic::icon::RtSafeLog(
              "FATAL ERROR: Library '", filename,
              "' uses a different allocator than the main binary. MallocGuard "
              "does not support this, consider excluding the library from "
              "checks "
              "using `SetMallocGuardDenylist()`.");
          std::exit(1);
        }
      }
    }

    // Only re-apply GOT hooks when the outermost load finishes.
    if (dl_intercept_depth == 1) {
      intrinsic::icon::InstallDynamicGotHooks(intrinsic::icon::kTargets);
    }
  }

  return handle;
}

}  // extern "C"
