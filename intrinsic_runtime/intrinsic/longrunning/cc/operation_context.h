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

#ifndef INTRINSIC_LONGRUNNING_OPERATION_CONTEXT_H_
#define INTRINSIC_LONGRUNNING_OPERATION_CONTEXT_H_

#include <memory>
#include <optional>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::longrunning {

// Context during long running operations to handle cancellations and to report
// progress.
class OperationContext {
 public:
  OperationContext();

  const StopToken& GetStopToken() const;

  StopSource& GetStopSource();

  // If specified by the operation, returns progress value within [0,1].
  std::optional<double> Progress() const;

  // Pushes an optional progress multiplier onto the stack. If the stack is not
  // empty, the last progress multiplier will be multiplied to the new
  // value. This is intended for tracking progress on multiple hierarchy levels.
  // Example:
  //
  // OperationContext context;
  // context.PushProgressMultiplier(1.0 / num_objects);
  // for (int i = 0; i < num_objects; ++i) {
  //   // Phase 1 consumes 70% of training time.
  //   context.PushProgressMultiplier(0.7);
  //   int num_things = ...;
  //   for (int j = 0; j < num_things; ++j) {
  //     context.AddProgress(1.0 / num_things);
  //     // compute things
  //   }
  //   context.PopProgressMultiplier();
  //   // Phase 1 done
  //
  //   // Phase 2 consumes 30% of training time.
  //   context.PushProgressMultiplier(0.3);
  //   for (int j = 0; j < num_things; ++j) {
  //     context.AddProgress(1.0 / num_things);
  //     // compute things phase 2
  //   }
  //   context.PopProgressMultiplier();
  // }
  // context.PopProgressMultiplier();
  void PushProgressMultiplier(double multiplier);

  // Pops last progress multiplier from the stack. Noop if no progress
  // multipliers are available.
  void PopProgressMultiplier();

  // Adds progress amount to the current progress value. If the progress value
  // wasn't defined yet this call also initializes the progress value.
  void AddProgress(double amount);

 private:
  struct ProgressState {
   public:
    std::optional<double> Progress() const ABSL_LOCKS_EXCLUDED(mutex);
    void PushProgressMultiplier(double multiplier) ABSL_LOCKS_EXCLUDED(mutex);
    void PopProgressMultiplier() ABSL_LOCKS_EXCLUDED(mutex);
    void AddProgress(double amount) ABSL_LOCKS_EXCLUDED(mutex);

   private:
    mutable absl::Mutex mutex;
    std::optional<double> progress ABSL_GUARDED_BY(mutex);
    std::vector<double> progress_multiplier ABSL_GUARDED_BY(mutex);
  };

  StopSource stop_source_;
  StopToken stop_token_;

  // The shared_ptr here is used to enable copy semantics for the
  // OperationContext. It allows us to pass the OperationContext by value to
  // downstream computation functions. It also removes the need to store a
  // pointer to the OperationContext in the OperationScheduler which simplifies
  // its logic and implementation.
  std::shared_ptr<ProgressState> progress_state_;
};
}  // namespace intrinsic::longrunning

#endif  // INTRINSIC_LONGRUNNING_OPERATION_CONTEXT_H_
