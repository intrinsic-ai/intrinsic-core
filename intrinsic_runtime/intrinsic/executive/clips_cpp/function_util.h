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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_FUNCTION_UTIL_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_FUNCTION_UTIL_H_

#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/log_severity.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace executive {
namespace clips {
namespace detail {

// Struct to store callback information.
// Must persist for as long as the function is registered, i.e., as long as
// the associated environment exists and the function could be called.
struct CallbackInfo {
  // Function can be arbitrary and depends on the callback. Use std::any
  // and during calls any_cast to expected function signature.
  std::any function;
  // Argument restriction is used to verify arguments.
  std::string argument_string;
};

// This is used by the callback lambda to retrieve typed arguments.
// We move it into the detail namespace to avoid internal linkage, so
// that we can put it into the cc file to avoid a larger number of
// CLIPS C API forward declarations.
void GetArgument(void* env, const std::string& func_name, int argpos,
                 std::string* value);
void GetArgument(void* env, const std::string& func_name, int argpos,
                 int* value);
void GetArgument(void* env, const std::string& func_name, int argpos,
                 int64_t* value);
void GetArgument(void* env, const std::string& func_name, int argpos,
                 double* value);
void GetArgument(void* env, const std::string& func_name, int argpos,
                 float* value);
void GetArgument(void* env, const std::string& func_name, int argpos,
                 void** value);
void GetArgument(void* env, absl::string_view func_name, int argpos,
                 Value* value);
void GetArgument(void* env, const std::string& func_name, int argpos,
                 Values* value);
void GetArgument(void* env, const std::string& func_name, int argpos,
                 Symbol* value);

absl::Status CheckFunctionArgumentCount(void* env, const std::string& func_name,
                                        size_t arg_count);

// This is a helper function to recursively append to the type string based on
// actual template arguments.
template <typename ArgType, typename... Args>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive() {
  INTR_ASSIGN_OR_RETURN(std::string s1,
                        GetArgumentRestrictionRecursive<ArgType>());
  INTR_ASSIGN_OR_RETURN(std::string s2,
                        GetArgumentRestrictionRecursive<Args...>());
  return absl::StrCat(s1, s2);
}

template <typename... Args, typename = std::enable_if_t<(sizeof...(Args) == 0)>>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive() {
  return "";
}

template <>
inline absl::StatusOr<std::string>
GetArgumentRestrictionRecursive<std::string>() {
  return "k";
}
template <>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive<void*>() {
  return "a";
}
template <>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive<double>() {
  return "d";
}
template <>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive<float>() {
  return "f";
}
template <>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive<int>() {
  return "i";
}
template <>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive<int64_t>() {
  return "l";
}
template <>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive<Value>() {
  return "u";
}
template <>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive<Values>() {
  return "m";
}
template <>
inline absl::StatusOr<std::string> GetArgumentRestrictionRecursive<Symbol>() {
  return "w";
}

// Forward declaration to avoid having to include clips.h and pollute callers
// namespace with C functions.
extern "C" {
void* GetEnvironmentFunctionContext(void*);
void* EnvAddSymbol(void*, const char*);
}

// The actual low-level callback invoked by CLIPS. This is guarded
// by return value, since the handling depends on the type.
template <typename ReturnType, typename... Args,
          typename = std::enable_if_t<std::is_integral_v<ReturnType> ||
                                      std::is_floating_point_v<ReturnType> ||
                                      std::is_same_v<ReturnType, void*> ||
                                      std::is_void_v<ReturnType>>>
ReturnType ClipsCallbackImpl(void* env) {
  void* context = GetEnvironmentFunctionContext(env);
  CallbackInfo* cb_info = static_cast<CallbackInfo*>(context);
  auto func =
      std::any_cast<std::function<ReturnType(void*)>>(cb_info->function);
  return func(env);
}

template <typename ReturnType, typename... Args,
          typename = std::enable_if_t<std::is_same_v<std::string, ReturnType> ||
                                      std::is_same_v<Symbol, ReturnType>>>
void* ClipsCallbackImpl(void* env) {
  void* context = GetEnvironmentFunctionContext(env);
  CallbackInfo* cb_info = static_cast<CallbackInfo*>(context);
  auto func =
      std::any_cast<std::function<ReturnType(void*)>>(cb_info->function);
  // we need to cast since the return value might be Symbol
  std::string rv = static_cast<std::string>(func(env));
  return EnvAddSymbol(env, rv.c_str());
}

// Set the return value, split into function to avoid spilling CLIPS internals.
void SetGenericOrMultifieldReturnValue(void* env, const Value& value, void* rv);
void SetGenericOrMultifieldReturnValue(void* env, const Values& values,
                                       void* rv);

// Returning a generic/type-unknown or multifield value requires different
// signature. The procedure is described in APG 6.31 Sec 3.3.5.
template <typename ReturnType, typename... Args,
          typename = std::enable_if_t<std::is_same_v<Value, ReturnType> ||
                                      std::is_same_v<Values, ReturnType>>>
void ClipsCallbackImpl(void* env, void* rv) {
  void* context = GetEnvironmentFunctionContext(env);
  CallbackInfo* cb_info = static_cast<CallbackInfo*>(context);
  auto func =
      std::any_cast<std::function<ReturnType(void*)>>(cb_info->function);
  ReturnType return_value = func(env);
  SetGenericOrMultifieldReturnValue(env, return_value, rv);
  // The actual return type is ignored, hence no value is returned, cf. APG
}

// This creates a lambda with closure to invoke the user-supplied callback
// function with appropriately typed arguments which have been retrieved
// from the CLIPS environment.
// This must be separate from GetCallbackLambda for std::index_sequence.
template <typename FuncType, typename TupleType, size_t... I>
auto GetCallbackLambdaImpl(const std::string& func_name, FuncType func,
                           std::index_sequence<I...> /*unused*/) {
  return [func_name, func](void* env) {
    TupleType args;
    absl::Status arg_count_status = detail::CheckFunctionArgumentCount(
        env, func_name, std::tuple_size<TupleType>());
    if (!arg_count_status.ok()) {
      LOG(FATAL) << arg_count_status.message();
    }
    ((detail::GetArgument(env, func_name, I + 1, &std::get<I>(args))), ...);
    return std::apply(std::move(func), std::move(args));
  };
}

}  // namespace detail

// Return types according to DefineFunction3 in clips/exntfunc.c:
template <typename ReturnType>
inline absl::StatusOr<char> GetReturnCode() {
  return absl::InvalidArgumentError("Unknown return type");
}
template <>
inline absl::StatusOr<char> GetReturnCode<void*>() {
  return 'a';
}
template <>
inline absl::StatusOr<char> GetReturnCode<bool>() {
  return 'b';
}
template <>
inline absl::StatusOr<char> GetReturnCode<char>() {
  return 'c';
}
template <>
inline absl::StatusOr<char> GetReturnCode<double>() {
  return 'd';
}
template <>
inline absl::StatusOr<char> GetReturnCode<float>() {
  return 'f';
}
template <>
inline absl::StatusOr<char> GetReturnCode<int>() {
  return 'i';
}
template <>
inline absl::StatusOr<char> GetReturnCode<int64_t>() {
  return 'l';
}
template <>
inline absl::StatusOr<char> GetReturnCode<void>() {
  return 'v';
}
template <>
inline absl::StatusOr<char> GetReturnCode<std::string>() {
  return 's';
}
template <>
inline absl::StatusOr<char> GetReturnCode<Value>() {
  return 'u';
}
template <>
inline absl::StatusOr<char> GetReturnCode<Symbol>() {
  return 'w';
}
template <>
inline absl::StatusOr<char> GetReturnCode<Values>() {
  return 'm';
}

// Formulates the argument restriction string
// (cf. CLIPS Advanced Programming Manual, Sec 3.1).
template <typename... Args>
inline absl::StatusOr<std::string> GetArgumentRestriction() {
  INTR_ASSIGN_OR_RETURN(
      std::string restriction,
      detail::GetArgumentRestrictionRecursive<std::decay_t<Args>...>());
  // CLIPS restriction strings use single character digits for min/max argument
  // counts (positions 0 and 1). For functions with >= 10 arguments, '*' (no
  // min/max count constraint) must be used in the header so the string format
  // is not corrupted; argument count is still validated at runtime by
  // CheckFunctionArgumentCount.
  if (restriction.length() > 9) {
    return absl::StrFormat("**u%s", restriction);
  }
  return absl::StrFormat("%i%iu%s", restriction.length(), restriction.length(),
                         restriction);
}

// Create the lambda wrapping the user-supplied callback.
template <typename ReturnType, typename... Args>
std::function<ReturnType(void*)> GetCallbackLambda(
    const std::string& func_name,
    const std::function<ReturnType(Args...)>& func) {
  return detail::GetCallbackLambdaImpl<decltype(func),
                                       std::tuple<std::decay_t<Args>...>>(
      func_name, func, std::index_sequence_for<std::decay_t<Args>...>{});
}

// Get the oddly-typed callback that the CLIPS C API requires.
// Returns a matching ClipsCallbackImpl deduced from the template args.
template <typename ReturnType, typename... Args>
int (*ClipsCallback())(void*) {
  return reinterpret_cast<int (*)(void*)>(
      detail::ClipsCallbackImpl<ReturnType, Args...>);
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_FUNCTION_UTIL_H_
