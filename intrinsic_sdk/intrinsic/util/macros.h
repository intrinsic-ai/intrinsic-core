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

#ifndef INTRINSIC_UTIL_MACROS_H_
#define INTRINSIC_UTIL_MACROS_H_

#include "absl/base/optimization.h"
#include "absl/log/log.h"
#include "intrinsic/util/status/status_builder.h"  // IWYU pragma: export

// Executes an expression `rexpr` that returns an `absl::StatusOr<T>`. On OK,
// moves its value into the variable defined by `lhs`, otherwise dies logging
// the status.
//
// Creates a unique name for the local status variable.
#define ASSIGN_OR_DIE(lhs, rexpr)                                            \
  ASSIGN_OR_DIE_IMPL_(                                                       \
      INTRINSIC_STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), lhs, \
      rexpr)

#define ASSIGN_OR_DIE_IMPL_(statusor, lhs, rexpr) \
  auto statusor = (rexpr);                        \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) {       \
    LOG(FATAL) << statusor.status();              \
  }                                               \
  lhs = std::move(statusor).value()

// Executes an expression `expr` that returns an `absl::StatusOr<T>`. On OK,
// returns its value, otherwise dies logging the status.
//
// Creates a unique name for the local status variable.
//
// For example, instead of writing:
// string MyFunction() { return FunctionThatReturnStatusOr().value() }
//
// One can write:
// string MyFunction() { RETURN_OR_DIE(FunctionThatReturnStatusOr()): }
//
// The key benefit is that the error message will be pointing at MyFunction()
// line instead of somewhere in statusor.cc/
#define RETURN_OR_DIE(expr) \
  RETURN_OR_DIE_IMPL_(      \
      INTRINSIC_STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), expr)

#define RETURN_OR_DIE_IMPL_(statusor, expr) \
  auto statusor = (expr);                   \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) { \
    LOG(FATAL) << statusor.status();        \
  }                                         \
  return std::move(statusor).value()

// Internal helper for concatenating macro values.
#define INTRINSIC_STATUS_MACROS_IMPL_CONCAT_INNER_(x, y) x##y
#define INTRINSIC_STATUS_MACROS_IMPL_CONCAT_(x, y) \
  INTRINSIC_STATUS_MACROS_IMPL_CONCAT_INNER_(x, y)

#endif  // INTRINSIC_UTIL_MACROS_H_
