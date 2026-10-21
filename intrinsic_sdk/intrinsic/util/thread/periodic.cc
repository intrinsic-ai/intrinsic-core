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

#include "intrinsic/util/thread/periodic.h"

#include <optional>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/rt_thread.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"

namespace intrinsic {

PeriodicOperation::PeriodicOperation(absl::AnyInvocable<void()> f)
    : PeriodicOperation(std::move(f), absl::ZeroDuration()) {}

PeriodicOperation::PeriodicOperation(absl::AnyInvocable<void()> f,
                                     absl::Duration period)
    : executor_thread_options_(std::nullopt),
      operation_(std::move(f)),
      period_(period) {}

PeriodicOperation::PeriodicOperation(const ThreadOptions& options,
                                     absl::AnyInvocable<void()> f,
                                     absl::Duration period)
    : executor_thread_options_(options),
      operation_(std::move(f)),
      period_(period) {}

absl::Status PeriodicOperation::Start() {
  absl::MutexLock l(&mutex_);
  // Executor already started?
  if (operation_executor_.joinable()) {
    return absl::OkStatus();
  }
  run_executor_thread_ = true;
  run_now_request_ = true;  // Run at least once.

  // This branch ensures that we only spin up a RT thread if the user explicitly
  // requested it by providing a ThreadOptions.
  if (executor_thread_options_.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        operation_executor_,
        CreateRealtimeCapableThread(*executor_thread_options_,
                                    &PeriodicOperation::ExecutorLoop, this));
  } else {
    operation_executor_ = Thread(&PeriodicOperation::ExecutorLoop, this);
  }

  // Wait for the last start time to be something more recent than infinite
  // past, which would indicate the start of our first operation.
  absl::Condition wait_for_operation_start(
      +[](PeriodicOperation* op) ABSL_EXCLUSIVE_LOCKS_REQUIRED(op->mutex_) {
        return op->last_operation_start_time_ > absl::InfinitePast();
      },
      this);
  mutex_.Await(wait_for_operation_start);
  return absl::OkStatus();
}

absl::Status PeriodicOperation::Stop() {
  absl::MutexLock l(&mutex_);
  // Executor not running?
  if (!operation_executor_.joinable()) {
    return absl::OkStatus();
  }

  run_executor_thread_ = false;
  mutex_.Unlock();
  operation_executor_.request_stop();
  operation_executor_.join();
  mutex_.Lock();
  return absl::OkStatus();
}

void PeriodicOperation::RunNow() {
  absl::Time last_operation_time;
  {
    absl::MutexLock l(&mutex_);
    last_operation_time = last_operation_start_time_;

    // Notify that we're waiting
    run_now_request_ = true;
  }

  // Wait until we run
  struct WaitUntilRanProps {
    absl::Time last_op_time;
    PeriodicOperation* op;
  } props{
      .last_op_time = last_operation_time,
      .op = this,
  };

  absl::Condition wait_until_ran(
      +[](WaitUntilRanProps* props)
           ABSL_EXCLUSIVE_LOCKS_REQUIRED(props->op->mutex_) {
             return !props->op->operation_executor_.joinable() ||
                    props->op->last_operation_start_time_ > props->last_op_time;
           },
      &props);

  absl::MutexLock l(&mutex_);
  mutex_.Await(wait_until_ran);
}

void PeriodicOperation::RunNowNonBlocking() {
  absl::MutexLock l(&mutex_);
  run_now_request_ = true;
}

void PeriodicOperation::ExecutorLoop() {
  // The next execution time is based on the time since the last execution
  // started.
  absl::Time next_execute_time = absl::Now();
  absl::MutexLock l(&mutex_);
  while (run_executor_thread_ || run_now_request_) {
    const absl::Time now = absl::Now();
    run_now_request_ = false;
    last_operation_start_time_ = now;
    mutex_.Unlock();
    operation_();
    mutex_.Lock();

    // Catch up to the periodicity of this operation if the operation takes
    // longer than the period.
    if (period_ > absl::ZeroDuration()) {
      while (now > next_execute_time) {
        next_execute_time += period_;
      }
    }

    absl::Condition should_run_again(
        +[](PeriodicOperation* op) ABSL_EXCLUSIVE_LOCKS_REQUIRED(op->mutex_) {
          return op->run_now_request_ || !op->run_executor_thread_;
        },
        this);

    mutex_.AwaitWithDeadline(should_run_again, next_execute_time);
  }
}

}  // namespace intrinsic
