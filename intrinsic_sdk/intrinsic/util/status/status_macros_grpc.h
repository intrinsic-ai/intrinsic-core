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

#ifndef INTRINSIC_UTIL_STATUS_STATUS_MACROS_GRPC_H_
#define INTRINSIC_UTIL_STATUS_STATUS_MACROS_GRPC_H_

#include <utility>

#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "grpcpp/support/status.h"  // IWYU pragma: export
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"  // IWYU pragma: export
#include "intrinsic/util/status/status_macros.h"

// This provides INTR_RETURN_IF_ERROR_GRPC and INTR_ASSIGN_OR_RETURN_GRPC.
// These macros are to be used in the same way as the versions without the _GRPC
// suffix. They are meant to be used in a context that needs to return
// grpc::Status instead of absl::Status or absl::StatusOr, so typically in
// implementations of gRPC service methods.

#define INTR_RETURN_IF_ERROR_GRPC(expr)                                        \
  INTR_STATUS_MACROS_IMPL_ELSE_BLOCKER_                                        \
  if (auto status_macro_internal_adaptor =                                     \
          ::intrinsic::status_macro_grpc_internal::StatusAdaptorForMacrosGrpc( \
              (expr), INTRINSIC_LOC)) {                                        \
  } else /* NOLINT */                                                          \
    return status_macro_internal_adaptor.Consume()

#define INTR_ASSIGN_OR_RETURN_GRPC(...)                               \
  INTR_STATUS_MACROS_IMPL_GET_VARIADIC_(                              \
      (__VA_ARGS__, INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_GRPC_3_, \
       INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_GRPC_2_))             \
  (__VA_ARGS__)

#define INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_GRPC_2_(lhs, rexpr)           \
  INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_GRPC_(                              \
      INTR_STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), lhs, rexpr, \
      static_cast<grpc::Status>(_))

#define INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_GRPC_3_(lhs, rexpr,           \
                                                         error_expression)     \
  INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_GRPC_(                              \
      INTR_STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), lhs, rexpr, \
      error_expression)

#define INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_GRPC_(statusor, lhs, rexpr, \
                                                       error_expression)     \
  auto statusor = (rexpr);                                                   \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) {                                  \
    ::intrinsic::StatusBuilderGrpc _(::intrinsic::StatusBuilder(             \
        std::move(statusor).status(), INTRINSIC_LOC));                       \
    (void)_; /* error_expression is allowed to not use this variable */      \
    return (error_expression);                                               \
  }                                                                          \
  {                                                                          \
    static_assert(                                                           \
        #lhs[0] != '(' || #lhs[sizeof(#lhs) - 2] != ')' ||                   \
            !::intrinsic::status_macro_internal::                            \
                HasPotentialConditionalOperator(#lhs, sizeof(#lhs) - 2),     \
        "Identified potential conditional operator, consider not "           \
        "using INTR_ASSIGN_OR_RETURN_GRPC");                                 \
  }                                                                          \
  INTR_STATUS_MACROS_IMPL_UNPARENTHESIZE_IF_PARENTHESIZED(lhs) =             \
      (*std::move(statusor))

namespace intrinsic {
namespace status_macro_grpc_internal {

// Provides a conversion to bool so that it can be used inside an if
// statement that declares a variable.
class StatusAdaptorForMacrosGrpc {
 public:
  StatusAdaptorForMacrosGrpc(const absl::Status& status,
                             intrinsic::SourceLocation loc)
      : builder_(StatusBuilder(status, loc)) {}

  StatusAdaptorForMacrosGrpc(absl::Status&& status,
                             intrinsic::SourceLocation loc)
      : builder_(StatusBuilder(std::move(status), loc)) {}

  StatusAdaptorForMacrosGrpc(const grpc::Status& status,
                             intrinsic::SourceLocation loc)
      : builder_(StatusBuilder(ToAbslStatus(status), loc)) {}

  StatusAdaptorForMacrosGrpc(const intrinsic::StatusBuilder& builder,
                             intrinsic::SourceLocation loc)
      : builder_(StatusBuilder(builder)) {}

  StatusAdaptorForMacrosGrpc(intrinsic::StatusBuilder&& builder,
                             intrinsic::SourceLocation loc)
      : builder_(std::move(builder)) {}

  StatusAdaptorForMacrosGrpc(const intrinsic::StatusBuilderGrpc& builder,
                             intrinsic::SourceLocation loc)
      : builder_(builder) {}

  StatusAdaptorForMacrosGrpc(intrinsic::StatusBuilderGrpc&& builder,
                             intrinsic::SourceLocation loc)
      : builder_(std::move(builder)) {}

  StatusAdaptorForMacrosGrpc(const StatusAdaptorForMacrosGrpc&) = delete;
  StatusAdaptorForMacrosGrpc& operator=(const StatusAdaptorForMacrosGrpc&) =
      delete;

  explicit operator bool() const { return ABSL_PREDICT_TRUE(builder_.ok()); }

  StatusBuilderGrpc&& Consume() { return std::move(builder_); }

 private:
  StatusBuilderGrpc builder_;
};

}  // namespace status_macro_grpc_internal
}  // namespace intrinsic
#endif  // INTRINSIC_UTIL_STATUS_STATUS_MACROS_GRPC_H_
