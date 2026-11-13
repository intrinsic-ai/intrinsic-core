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

#include "intrinsic/longrunning/cc/operation_context.h"

#include <memory>
#include <optional>
#include <vector>

#include "absl/synchronization/mutex.h"
#include "intrinsic/util/thread/stop_token.h"

namespace intrinsic::longrunning {

OperationContext::OperationContext()
    : stop_source_(),
      stop_token_(stop_source_.get_token()),
      progress_state_(std::make_shared<ProgressState>()) {}

const StopToken& OperationContext::GetStopToken() const { return stop_token_; }

StopSource& OperationContext::GetStopSource() { return stop_source_; }

std::optional<double> OperationContext::Progress() const {
  return progress_state_->Progress();
}

void OperationContext::PushProgressMultiplier(double multiplier) {
  return progress_state_->PushProgressMultiplier(multiplier);
}

void OperationContext::PopProgressMultiplier() {
  return progress_state_->PopProgressMultiplier();
}

void OperationContext::AddProgress(double amount) {
  return progress_state_->AddProgress(amount);
}

std::optional<double> OperationContext::ProgressState::Progress() const {
  absl::MutexLock lock(mutex);
  return progress;
}

void OperationContext::ProgressState::PushProgressMultiplier(
    double multiplier) {
  absl::MutexLock lock(mutex);
  if (!progress_multiplier.empty()) {
    multiplier *= progress_multiplier.back();
  }
  progress_multiplier.push_back(multiplier);
}

void OperationContext::ProgressState::PopProgressMultiplier() {
  absl::MutexLock lock(mutex);
  if (!progress_multiplier.empty()) {
    progress_multiplier.pop_back();
  }
}

void OperationContext::ProgressState::AddProgress(double amount) {
  absl::MutexLock lock(mutex);
  if (!progress_multiplier.empty()) {
    amount *= progress_multiplier.back();
  }
  if (!progress.has_value()) {
    progress = amount;
  } else {
    progress.value() += amount;
  }
}

}  // namespace intrinsic::longrunning
