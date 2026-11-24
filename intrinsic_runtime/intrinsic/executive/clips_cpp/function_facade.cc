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

#include "intrinsic/executive/clips_cpp/function_facade.h"

#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"

namespace intrinsic {
namespace executive {
namespace clips {

EnvironmentFunctionFacade::RegisteredFunction::~RegisteredFunction() {
  facade_->clips_mutex()->AssertHeld();
  absl::Status result = facade_->ClipsRemoveFunction(name_);
  if (!result.ok()) {
    // If we cannot remove the function, there must be a programming error
    // (e.g., CLIPS got reset in the mean time). We print a message here but
    // it's ok to proceed as the function anyway doesn't exist anymore.
    LOG(ERROR) << "Could not remove function " << name_ << ": " << result;
  }
}

EnvironmentFunctionFacade::~EnvironmentFunctionFacade() {
  absl::MutexLock lock(*clips_mutex());
  functions_.clear();
}

absl::Status EnvironmentFunctionFacade::ClipsRemoveFunction(
    const std::string& name) {
  return environment_->RemoveFunction(name);
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
