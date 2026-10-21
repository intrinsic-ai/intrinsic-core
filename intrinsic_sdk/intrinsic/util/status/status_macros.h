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

#ifndef INTRINSIC_UTIL_STATUS_STATUS_MACROS_H_
#define INTRINSIC_UTIL_STATUS_STATUS_MACROS_H_

#include <type_traits>  // IWYU pragma: export
#include <utility>

#include "absl/base/optimization.h"
#include "intrinsic/icon/release/source_location.h"
#include "intrinsic/util/status/status_builder.h"  // IWYU pragma: export

namespace grpc {
// Forward declare to enable checking if gRPC status macros should be used.
class Status;
}  // namespace grpc

// Evaluates an expression that produces a `absl::Status`. If the status is not
// ok, returns it from the current function.
//
// For example:
//   absl::Status MultiStepFunction() {
//     INTR_RETURN_IF_ERROR(Function(args...));
//     INTR_RETURN_IF_ERROR(foo.Method(args...));
//     return absl::OkStatus();
//   }
//
// The macro ends with a `StatusBuilder` which allows the returned status
// to be extended with more details.  Any chained expressions after the macro
// will not be evaluated unless there is an error.
//
// For example:
//   absl::Status MultiStepFunction() {
//     INTR_RETURN_IF_ERROR(Function(args...)) << "in MultiStepFunction";
//     INTR_RETURN_IF_ERROR(foo.Method(args...))
//         << "while processing query: " << query.DebugString();
//     return absl::OkStatus();
//   }
//
// The `StatusBuilder` can be used to directly log the error:
// For example:
//   INTR_RETURN_IF_ERROR(Function(args...)).LogError();
// Check `StatusBuilder` for additional options.
//
// If using this macro inside a lambda, you need to annotate the return type
// to avoid confusion between a `StatusBuilder` and an `absl::Status` type.
// E.g.
//
//   []() -> absl::Status {
//     INTR_RETURN_IF_ERROR(Function(args...));
//     INTR_RETURN_IF_ERROR(foo.Method(args...));
//     return absl::OkStatus();
//   }
#define INTR_RETURN_IF_ERROR(expr)                                         \
  INTR_STATUS_MACROS_IMPL_ELSE_BLOCKER_                                    \
  if (auto status_macro_internal_adaptor =                                 \
          ::intrinsic::status_macro_internal::StatusAdaptorForMacros(      \
              (expr), INTRINSIC_LOC)) {                                    \
    static_assert(                                                         \
        !std::is_same<typename std::remove_cvref<decltype(expr)>::type,    \
                      ::grpc::Status>::value,                              \
        "Use ToAbslStatus if you want to return absl::Status, or "         \
        "INTR_RETURN_IF_ERROR_GRPC if you want to return a grpc::Status"); \
  } else /* NOLINT */                                                      \
    return status_macro_internal_adaptor.Consume()

// Executes an expression `rexpr` that returns a `StatusOr<T>`. On OK, moves its
// value into the variable defined by `lhs`, otherwise returns from the current
// function. By default the error status is returned unchanged, but it may be
// modified by an `error_expression`. If there is an error, `lhs` is not
// evaluated; thus any side effects that `lhs` may have only occur in the
// success case.
//
// Interface:
//
//   INTR_ASSIGN_OR_RETURN(lhs, rexpr)
//   INTR_ASSIGN_OR_RETURN(lhs, rexpr, error_expression);
//
// WARNING: if lhs is parenthesized, the parentheses are removed. See examples
// for more details.
//
// WARNING: expands into multiple statements; it cannot be used in a single
// statement (e.g. as the body of an if statement without {})!
//
// Example: Declaring and initializing a new variable (ValueType can be anything
//          that can be initialized with assignment, including references):
//   INTR_ASSIGN_OR_RETURN(ValueType value, MaybeGetValue(arg));
//
// Example: Assigning to an existing variable:
//   ValueType value;
//   INTR_ASSIGN_OR_RETURN(value, MaybeGetValue(arg));
//
// Example: Assigning to an expression with side effects:
//   MyProto data;
//   INTR_ASSIGN_OR_RETURN(*data.mutable_str(), MaybeGetValue(arg));
//   // No field "str" is added on error.
//
// Example: Assigning to a std::unique_ptr.
//   INTR_ASSIGN_OR_RETURN(std::unique_ptr<T> ptr, MaybeGetPtr(arg));
//
// Example: Assigning to a map. Because of C preprocessor
// limitation, the type used in INTR_ASSIGN_OR_RETURN cannot contain comma, so
// wrap lhs in parentheses:
//   INTR_ASSIGN_OR_RETURN((absl::flat_hash_map<Foo, Bar> my_map), GetMap());
// Or use auto if the type is obvious enough:
//   INTR_ASSIGN_OR_RETURN(const auto& my_map, GetMapRef());
//
// Example: Assigning to structured bindings. The same situation with comma as
// in map, so wrap the statement in parentheses.
//   INTR_ASSIGN_OR_RETURN((const auto& [first, second]), GetPair());
//
// If passed, the `error_expression` is evaluated to produce the return
// value. The expression may reference any variable visible in scope, as
// well as a `StatusBuilder` object populated with the error and named by a
// single underscore `_`. The expression typically uses the builder to modify
// the status and is returned directly in manner similar to
// INTR_RETURN_IF_ERROR. The expression may, however, evaluate to any type
// returnable by the function, including (void). For example:
//
// Example: Adjusting the error message.
//   INTR_ASSIGN_OR_RETURN(ValueType value, MaybeGetValue(query),
//                        _ << "while processing " << query.DebugString());
//
// Example: Logging the error on failure.
//   INTR_ASSIGN_OR_RETURN(ValueType value, MaybeGetValue(query), _.LogError());
//
#define INTR_ASSIGN_OR_RETURN(...)                               \
  INTR_STATUS_MACROS_IMPL_GET_VARIADIC_(                         \
      (__VA_ARGS__, INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_3_, \
       INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_2_))             \
  (__VA_ARGS__)

// =================================================================
// == Implementation details, do not rely on anything below here. ==
// =================================================================

// MSVC incorrectly expands variadic macros, splice together a macro call to
// work around the bug.
#define INTR_STATUS_MACROS_IMPL_GET_VARIADIC_HELPER_(_1, _2, _3, NAME, ...) NAME
#define INTR_STATUS_MACROS_IMPL_GET_VARIADIC_(args) \
  INTR_STATUS_MACROS_IMPL_GET_VARIADIC_HELPER_ args

#define INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_2_(lhs, rexpr)                \
  INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_(                                   \
      INTR_STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), lhs, rexpr, \
      absl::Status(static_cast<absl::Status>(_)))

#define INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_3_(lhs, rexpr,                \
                                                    error_expression)          \
  INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_(                                   \
      INTR_STATUS_MACROS_IMPL_CONCAT_(_status_or_value, __LINE__), lhs, rexpr, \
      error_expression)

#define INTR_STATUS_MACROS_IMPL_ASSIGN_OR_RETURN_(statusor, lhs, rexpr,        \
                                                  error_expression)            \
  auto statusor = (rexpr);                                                     \
  static_assert(                                                               \
      !std::is_same<                                                           \
          typename std::remove_cvref<decltype(statusor.status())>::type,       \
          ::grpc::Status>::value,                                              \
      "Use ToAbslStatus if you want to return absl::Status, or "               \
      "INTR_ASSIGN_OR_RETURN_GRPC if you want to return a grpc::Status");      \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) {                                    \
    ::intrinsic::StatusBuilder _(std::move(statusor).status(), INTRINSIC_LOC); \
    (void)_; /* error_expression is allowed to not use this variable */        \
    return (error_expression);                                                 \
  }                                                                            \
  {                                                                            \
    static_assert(                                                             \
        #lhs[0] != '(' || #lhs[sizeof(#lhs) - 2] != ')' ||                     \
            !::intrinsic::status_macro_internal::                              \
                HasPotentialConditionalOperator(#lhs, sizeof(#lhs) - 2),       \
        "Identified potential conditional operator, consider not "             \
        "using INTR_ASSIGN_OR_RETURN");                                        \
  }                                                                            \
  INTR_STATUS_MACROS_IMPL_UNPARENTHESIZE_IF_PARENTHESIZED(lhs) =               \
      (*std::move(statusor))

// Internal helpers for macro expansion.
#define INTR_STATUS_MACROS_IMPL_EAT(...)
#define INTR_STATUS_MACROS_IMPL_REM(...) __VA_ARGS__
#define INTR_STATUS_MACROS_IMPL_EMPTY()

// Internal helpers for emptyness arguments check.
#define INTR_STATUS_MACROS_IMPL_IS_EMPTY_INNER(...) \
  INTR_STATUS_MACROS_IMPL_IS_EMPTY_INNER_I(__VA_ARGS__, 0, 1)
#define INTR_STATUS_MACROS_IMPL_IS_EMPTY_INNER_I(e0, e1, is_empty, ...) is_empty

#define INTR_STATUS_MACROS_IMPL_IS_EMPTY(...) \
  INTR_STATUS_MACROS_IMPL_IS_EMPTY_I(__VA_ARGS__)
#define INTR_STATUS_MACROS_IMPL_IS_EMPTY_I(...) \
  INTR_STATUS_MACROS_IMPL_IS_EMPTY_INNER(_, ##__VA_ARGS__)

// Internal helpers for if statement.
#define INTR_STATUS_MACROS_IMPL_IF_1(_Then, _Else) _Then
#define INTR_STATUS_MACROS_IMPL_IF_0(_Then, _Else) _Else
#define INTR_STATUS_MACROS_IMPL_IF(_Cond, _Then, _Else)               \
  INTR_STATUS_MACROS_IMPL_CONCAT_(INTR_STATUS_MACROS_IMPL_IF_, _Cond) \
  (_Then, _Else)

// Expands to 1 if the input is parenthesized. Otherwise expands to 0.
#define INTR_STATUS_MACROS_IMPL_IS_PARENTHESIZED(...) \
  INTR_STATUS_MACROS_IMPL_IS_EMPTY(INTR_STATUS_MACROS_IMPL_EAT __VA_ARGS__)

// If the input is parenthesized, removes the parentheses. Otherwise
// expands to the input unchanged.
#define INTR_STATUS_MACROS_IMPL_UNPARENTHESIZE_IF_PARENTHESIZED(...) \
  INTR_STATUS_MACROS_IMPL_IF(                                        \
      INTR_STATUS_MACROS_IMPL_IS_PARENTHESIZED(__VA_ARGS__),         \
      INTR_STATUS_MACROS_IMPL_REM, INTR_STATUS_MACROS_IMPL_EMPTY())  \
  __VA_ARGS__

// Internal helper for concatenating macro values.
#define INTR_STATUS_MACROS_IMPL_CONCAT_INNER_(x, y) x##y
#define INTR_STATUS_MACROS_IMPL_CONCAT_(x, y) \
  INTR_STATUS_MACROS_IMPL_CONCAT_INNER_(x, y)

// The GNU compiler emits a warning for code like:
//
//   if (foo)
//     if (bar) { } else baz;
//
// because it thinks you might want the else to bind to the first if. This
// leads to problems with code like:
//
//   if (do_expr) INTR_RETURN_IF_ERROR(expr) << "Some message";
//
// The "switch (0) case 0:" idiom is used to suppress this.
#define INTR_STATUS_MACROS_IMPL_ELSE_BLOCKER_ \
  switch (0)                                  \
  case 0:                                     \
  default:  // NOLINT

namespace intrinsic {
namespace status_macro_internal {

// Some builds do not support C++14 fully yet, using C++11 constexpr
// technique.
constexpr bool HasPotentialConditionalOperator(const char* lhs, int index) {
  return (index == -1 ? false
                      : (lhs[index] == '?' ? true
                                           : HasPotentialConditionalOperator(
                                                 lhs, index - 1)));
}

// Provides a conversion to bool so that it can be used inside an if
// statement that declares a variable.
class StatusAdaptorForMacros {
 public:
  StatusAdaptorForMacros(const absl::Status& status,
                         intrinsic::SourceLocation loc)
      : builder_(status, loc) {}

  StatusAdaptorForMacros(absl::Status&& status, intrinsic::SourceLocation loc)
      : builder_(std::move(status), loc) {}

  StatusAdaptorForMacros(const intrinsic::StatusBuilder& builder,
                         intrinsic::SourceLocation loc)
      : builder_(builder) {}

  StatusAdaptorForMacros(intrinsic::StatusBuilder&& builder,
                         intrinsic::SourceLocation loc)
      : builder_(std::move(builder)) {}

  StatusAdaptorForMacros(const StatusAdaptorForMacros&) = delete;
  StatusAdaptorForMacros& operator=(const StatusAdaptorForMacros&) = delete;

  explicit operator bool() const { return ABSL_PREDICT_TRUE(builder_.ok()); }

  intrinsic::StatusBuilder&& Consume() { return std::move(builder_); }

 private:
  intrinsic::StatusBuilder builder_;
};

}  // namespace status_macro_internal
}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STATUS_STATUS_MACROS_H_
