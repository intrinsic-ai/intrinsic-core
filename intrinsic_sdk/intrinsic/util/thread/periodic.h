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

#ifndef INTRINSIC_UTIL_THREAD_PERIODIC_H_
#define INTRINSIC_UTIL_THREAD_PERIODIC_H_

#include <optional>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"

namespace intrinsic {

class PeriodicOperation {
 public:
  // Returns a new PeriodicOperation that executes continuously with no delay
  // in between.
  explicit PeriodicOperation(absl::AnyInvocable<void()> f);

  // Returns a new PeriodicOperation that executes with the given period.
  PeriodicOperation(absl::AnyInvocable<void()> f, absl::Duration period);

  // Returns a new PeriodicOperation that executes with the given period on a
  // thread with the provided thread options..
  PeriodicOperation(const ThreadOptions& options, absl::AnyInvocable<void()> f,
                    absl::Duration period);

  // Disallow copying
  PeriodicOperation(const PeriodicOperation&) = delete;
  PeriodicOperation& operator=(const PeriodicOperation&) = delete;

  // Starts the periodic execution of the operation passed to the constructor.
  // If this is the first time `Start()` is being called, then it is guaranteed
  // that the operation will execute at least once.
  absl::Status Start();

  // Stops the periodic execution started by `Start()`. This is a blocking call
  // that guarantees upon returning that the operation is not being run. This is
  // a no-op if no call to `Start` has been beforehand unless `Stop` was already
  // called after `Start`. Must be called before this object is destroyed if
  // `Start` was also called.
  absl::Status Stop();

  // Blocking call that waits for the current invocation of the operation to
  // finish and then schedules a subsequent incovation immediately when the
  // running thread is next scheduled.
  //
  // Multiple calls to this function from different threads will only wait for
  // a single invocation before returning.
  void RunNow();

  // Nonblocking call that schedules the operation to run directly after the
  // current one is finished.
  //
  // Multiple calls to this function from different threads will only schedule a
  // single invocation before returning.
  void RunNowNonBlocking();

  // Returns the current period for this operation.
  absl::Duration Period() const { return period_; }

  // Sets the period at which to execute the given operation. The next operation
  // is scheduled from the start time of the last executed operation.
  void SetPeriod(absl::Duration new_period) { period_ = std::move(new_period); }

 private:
  absl::Mutex mutex_;

  std::optional<ThreadOptions> executor_thread_options_;

  absl::AnyInvocable<void()> operation_;
  absl::Duration period_;

  bool run_now_request_ ABSL_GUARDED_BY(mutex_) = false;
  bool run_executor_thread_ ABSL_GUARDED_BY(mutex_) = false;

  void ExecutorLoop();

  absl::Time last_operation_start_time_ ABSL_GUARDED_BY(mutex_) =
      absl::InfinitePast();
  Thread operation_executor_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_THREAD_PERIODIC_H_
