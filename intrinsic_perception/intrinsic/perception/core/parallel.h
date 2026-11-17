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

#ifndef INTRINSIC_PERCEPTION_CORE_PARALLEL_H_
#define INTRINSIC_PERCEPTION_CORE_PARALLEL_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "tbb/global_control.h"  // IWYU pragma: export
#include "tbb/info.h"            // IWYU pragma: export
#include "tbb/parallel_for.h"    // IWYU pragma: export
#include "tbb/task_arena.h"      // IWYU pragma: export
#include "tbb/task_group.h"      // IWYU pragma: export

namespace intrinsic::perception {

// Mutex to handle parallel processing.
using ParallelMutex = absl::Mutex;

// Struct that allows groups of actions to be performed in parallel.
using ParallelTaskGroup = tbb::task_group;

// ParallelScopedLock takes a ParallelMutex to block parallel processing of
// succeeding scopes.
using ParallelScopedLock = absl::MutexLock;

// Function f is performed in parallel on all indices from first to last. The
// function f takes a index as input. The return type refers to void if func
// returns void and util::Status otherwise.
template <typename Index, typename Func>
auto ParallelFor(Index first, Index last, const Func& func) {
  if constexpr (std::is_void_v<std::invoke_result_t<Func, Index>>) {
    tbb::parallel_for(first, last, func);
  } else {
    ParallelMutex status_mutex;
    absl::Status status = absl::OkStatus();
    tbb::parallel_for(first, last,
                      [&status_mutex, &status, &func](const auto& element) {
                        const absl::Status func_status = func(element);
                        if (!func_status.ok()) {
                          ParallelScopedLock status_lock(status_mutex);
                          status = func_status;
                        }
                      });
    return status;
  }
}

// Function f is performed in parallel on all indices from first to last with a
// step size. The function f takes a index as input.The return type refers to
// void if func returns void and util::Status otherwise.
template <typename Index, typename Func>
auto ParallelFor(Index first, Index last, Index step, const Func& func) {
  if constexpr (std::is_same_v<std::invoke_result_t<Func, Index>, void>) {
    tbb::parallel_for(first, last, step, func);
  } else {
    ParallelMutex status_mutex;
    absl::Status status = absl::OkStatus();
    tbb::parallel_for(first, last, step,
                      [&status_mutex, &status, &func](const auto& element) {
                        const absl::Status func_status = func(element);
                        if (!func_status.ok()) {
                          ParallelScopedLock status_lock(status_mutex);
                          status = func_status;
                        }
                      });
    return status;
  }
}

// Function f is performed in parallel on all indices from first to last. The
// function f takes a index as input. Each parallel call is isolated. See:
// https://www.threadingbuildingblocks.org/docs/help/tbb_userguide/work_isolation.html
// We use this_task_arena::isolate instead of a task_arena to isolate
// the threads (according "ProTBB" p.350).
// The return type refers to void if func returns void and util::Status
// otherwise.
template <typename Index, typename Func>
auto IsolatedParallelFor(Index first, Index last, const Func& func) {
  // Nested tbb::parallel_for functions need to be executed within an arena
  // to avoid deadlocks by resource starving.
  if constexpr (std::is_same_v<std::invoke_result_t<Func, Index>, void>) {
    tbb::this_task_arena::isolate(
        [&] { tbb::parallel_for(first, last, func); });
  } else {
    ParallelMutex status_mutex;
    absl::Status status = absl::OkStatus();
    tbb::this_task_arena::isolate([&status_mutex, &status, &func, &first,
                                   &last] {
      tbb::parallel_for(first, last,
                        [&status_mutex, &status, &func](const auto& element) {
                          const absl::Status func_status = func(element);
                          if (!func_status.ok()) {
                            ParallelScopedLock status_lock(status_mutex);
                            status = func_status;
                          }
                        });
    });
    return status;
  }
}

// Function f is performed in parallel on all indices from first to last. The
// function f takes a index as input. Each parallel call is isolated. See:
// https://www.threadingbuildingblocks.org/docs/help/tbb_userguide/work_isolation.html
// We use this_task_arena::isolate instead of a task_arena to isolate
// the threads (according "ProTBB" p.350).
// The return type refers to void if func returns void and util::Status
// otherwise.
template <typename Index, typename Func>
auto IsolatedLockedParallelFor(Index first, Index last, const Func& func,
                               ParallelMutex& mutex) {
  // Nested tbb::parallel_for functions need to be executed within an arena
  // to avoid deadlocks by resource starving.
  if constexpr (std::is_same_v<std::invoke_result_t<Func, Index>, void>) {
    tbb::this_task_arena::isolate([&] {
      ParallelScopedLock lock(mutex);
      tbb::parallel_for(first, last, func);
    });
  } else {
    ParallelMutex status_mutex;
    absl::Status status = absl::OkStatus();
    tbb::this_task_arena::isolate([&status_mutex, &status, &func, &first, &last,
                                   &mutex] {
      ParallelScopedLock lock(mutex);
      tbb::parallel_for(first, last,
                        [&status_mutex, &status, &func](const auto& element) {
                          const absl::Status func_status = func(element);
                          if (!func_status.ok()) {
                            ParallelScopedLock status_lock(status_mutex);
                            status = func_status;
                          }
                        });
    });
    return status;
  }
}

// Function f is performed in parallel on all indices from first to last with a
// step size. The function f takes a index as input.  Each parallel call is
// isolated. See:
// https://www.threadingbuildingblocks.org/docs/help/tbb_userguide/work_isolation.html
// We use this_task_arena::isolate instead of a task_arena to isolate
// the threads (according "ProTBB" p.350).
// The return type refers to void if func returns void and util::Status
// otherwise.
template <typename Index, typename Func>
auto IsolatedParallelFor(Index first, Index last, Index step,
                         const Func& func) {
  // Nested tbb::parallel_for functions need to be executed within an arena
  // to avoid deadlocks by resource starving.
  if constexpr (std::is_same_v<std::invoke_result_t<Func, Index>, void>) {
    tbb::this_task_arena::isolate(
        [&] { tbb::parallel_for(first, last, step, func); });
  } else {
    ParallelMutex status_mutex;
    absl::Status status = absl::OkStatus();
    tbb::this_task_arena::isolate([&status_mutex, &status, &func, &first, &last,
                                   &step] {
      tbb::parallel_for(first, last, step,
                        [&status_mutex, &status, &func](const auto& element) {
                          const absl::Status func_status = func(element);
                          if (!func_status.ok()) {
                            ParallelScopedLock status_lock(status_mutex);
                            status = func_status;
                          }
                        });
    });
    return status;
  }
}

// Function f is performed in parallel on all indices from first to last with a
// step size. The function f takes a index as input.  Each parallel call is
// isolated. See:
// https://www.threadingbuildingblocks.org/docs/help/tbb_userguide/work_isolation.html
// We use this_task_arena::isolate instead of a task_arena to isolate
// the threads (according "ProTBB" p.350).
// The return type refers to void if func returns void and util::Status
// otherwise.
template <typename Index, typename Func>
auto IsolatedLockedParallelFor(Index first, Index last, Index step,
                               const Func& func, ParallelMutex& mutex) {
  // Nested tbb::parallel_for functions need to be executed within an arena
  // to avoid deadlocks by resource starving.
  if constexpr (std::is_same_v<std::invoke_result_t<Func, Index>, void>) {
    tbb::this_task_arena::isolate([&] {
      ParallelScopedLock lock(mutex);
      tbb::parallel_for(first, last, step, func);
    });
  } else {
    ParallelMutex status_mutex;
    absl::Status status = absl::OkStatus();
    tbb::this_task_arena::isolate([&status_mutex, &status, &func, &first, &last,
                                   &step, &mutex] {
      ParallelScopedLock lock(mutex);
      tbb::parallel_for(first, last, step,
                        [&status_mutex, &status, &func](const auto& element) {
                          const absl::Status func_status = func(element);
                          if (!func_status.ok()) {
                            ParallelScopedLock status_lock(status_mutex);
                            status = func_status;
                          }
                        });
    });
    return status;
  }
}

// Gives back the number of hyperthreads that TBB is using.
inline int32_t ParallelDefaultNumberOfThreads() {
  const std::size_t max_allowed_parallelism = tbb::global_control::active_value(
      tbb::global_control::max_allowed_parallelism);
  const int32_t default_concurrency = tbb::info::default_concurrency();
  if (default_concurrency <= 0 ||
      max_allowed_parallelism >=
          static_cast<std::size_t>(default_concurrency)) {
    return default_concurrency;
  }
  return static_cast<int32_t>(max_allowed_parallelism);
}

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_CORE_PARALLEL_H_
