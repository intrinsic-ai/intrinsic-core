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

#ifndef INTRINSIC_LONGRUNNING_WRAPPERS_H_
#define INTRINSIC_LONGRUNNING_WRAPPERS_H_

#include <memory>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/longrunning/cc/operation_scheduler_interface.h"

namespace intrinsic::longrunning::wrap_internal {

// Wraps the given OperationSchedulerInterface and adds a prefix asking about
// the underlying operations. For example if the inner scheduler has an
// operation with name "op1" the wrapped scheduler will have an operation with
// name "prefix/op1".
absl::StatusOr<std::shared_ptr<OperationSchedulerInterface>>
WrapSchedulerWithPrefix(absl::string_view prefix,
                        std::shared_ptr<OperationSchedulerInterface> scheduler);

}  // namespace intrinsic::longrunning::wrap_internal

#endif  // INTRINSIC_LONGRUNNING_WRAPPERS_H_
