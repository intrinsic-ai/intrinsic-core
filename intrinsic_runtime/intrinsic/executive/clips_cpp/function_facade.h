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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_FUNCTION_FACADE_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_FUNCTION_FACADE_H_

#include <map>
#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace executive {
namespace clips {

// Facade class that (only) allows the registration of functions in CLIPS
// environment. This can be used to avoid having to pass a pointer to the real
// environment to ensure that other code cannot accidentally mess with the
// environment.
class EnvironmentFunctionFacade {
 public:
  explicit EnvironmentFunctionFacade(clips::Environment* environment)
      : environment_(environment) {}

  virtual ~EnvironmentFunctionFacade();  // Locks mutex().

  // Add a function which may then be called with the given name from CLIPS.
  // The function will be automatically removed upon destruction of the
  // registry. An error is returned if a function with that name has already
  // been registered.
  template <typename FunctionType>
  absl::Status AddFunction(const std::string& name,
                           const FunctionType& function)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_mutex()) {
    functions_[name] = std::make_unique<RegisteredFunction>(name, this);
    INTR_RETURN_IF_ERROR(environment_->AddFunction(name, function));
    return absl::OkStatus();
  }

  // Returns the mutex of the CLIPS environment (so that the caller can lock
  // it prior to calling RegisterFunction.
  absl::Mutex* clips_mutex() const ABSL_LOCK_RETURNED(environment_->mutex()) {
    return environment_->mutex();
  }

 protected:
  absl::Status ClipsRemoveFunction(const std::string& name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(environment_->mutex());

 private:
  // Wrapper class that encapsulated a CLIPS function.
  // Removes the function automatically upon destruction.
  class RegisteredFunction {
   public:
    RegisteredFunction(const std::string& name,
                       EnvironmentFunctionFacade* facade)
        : name_(name), facade_(facade) {}

    // Non-copyable.
    RegisteredFunction(const RegisteredFunction&) = delete;
    RegisteredFunction& operator=(const RegisteredFunction&) = delete;

    virtual ~RegisteredFunction();

   private:
    std::string name_;
    EnvironmentFunctionFacade* facade_;
  };

  clips::Environment* environment_;  // Owned externally.

  std::map<std::string, std::unique_ptr<RegisteredFunction>> functions_;
};

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_FUNCTION_FACADE_H_
