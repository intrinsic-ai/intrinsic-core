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

#include "intrinsic/executive/clips_cpp/function_util.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/clips_cpp/value_util.h"
// Keep CLIPS at the bottom to prevent macro pollution.
#include "clips/clips.h"

namespace intrinsic {
namespace executive {
namespace clips {
namespace detail {

void GetArgument(void* env, const std::string& func_name, int argpos,
                 std::string* value) {
  DATA_OBJECT dataobj;
  if (!EnvArgTypeCheck(env, "", argpos, SYMBOL_OR_STRING, &dataobj)) {
    LOG(WARNING)
        << "Argument type mismatch: expected symbol or string for argument "
        << argpos << " of " << func_name;
    return;
  }
  *value = EnvRtnLexeme(env, argpos);
}

void GetArgument(void* env, const std::string& func_name, int argpos,
                 int* value) {
  DATA_OBJECT dataobj;
  if (!EnvArgTypeCheck(env, "", argpos, INTEGER, &dataobj)) {
    LOG(WARNING) << "Argument type mismatch: expected integer for argument "
                 << argpos << " of " << func_name;
    return;
  }
  *value = static_cast<int>(EnvRtnLong(env, argpos));
}

void GetArgument(void* env, const std::string& func_name, int argpos,
                 int64_t* value) {
  DATA_OBJECT dataobj;
  if (!EnvArgTypeCheck(env, "", argpos, INTEGER, &dataobj)) {
    LOG(WARNING) << "Argument type mismatch: expected integer for argument "
                 << argpos << " of " << func_name;
    return;
  }
  *value = EnvRtnLong(env, argpos);
}

void GetArgument(void* env, const std::string& func_name, int argpos,
                 double* value) {
  DATA_OBJECT dataobj;
  if (!EnvArgTypeCheck(env, "", argpos, FLOAT, &dataobj)) {
    LOG(WARNING) << "Argument type mismatch: expected float for argument "
                 << argpos << " of " << func_name;
    return;
  }
  *value = EnvRtnDouble(env, argpos);
}

void GetArgument(void* env, const std::string& func_name, int argpos,
                 float* value) {
  DATA_OBJECT dataobj;
  if (!EnvArgTypeCheck(env, "", argpos, FLOAT, &dataobj)) {
    LOG(WARNING) << "Argument type mismatch: expected float for argument "
                 << argpos << " of " << func_name;
    return;
  }
  *value = static_cast<float>(EnvRtnDouble(env, argpos));
}

void GetArgument(void* env, const std::string& func_name, int argpos,
                 void** value) {
  DATA_OBJECT obj;
  if (!EnvArgTypeCheck(env, "", argpos, EXTERNAL_ADDRESS, &obj)) {
    LOG(WARNING)
        << "Argument type mismatch: expected external address for argument "
        << argpos << " of " << func_name;
    return;
  }
  if (obj.type == EXTERNAL_ADDRESS) {
    *value = static_cast<struct externalAddressHashNode*>(obj.value)
                 ->externalAddress;
  }
}

void GetArgument(void* env, absl::string_view func_name, int argpos,
                 Value* value) {
  DATA_OBJECT obj;
  EnvRtnUnknown(env, argpos, &obj);
  auto values = internal::DataObjectToValues(&obj);
  if (!values.empty()) {
    *value = values[0];
  }
}

void GetArgument(void* env, const std::string& func_name, int argpos,
                 Values* values) {
  DATA_OBJECT obj;
  if (!EnvArgTypeCheck(env, "", argpos, MULTIFIELD, &obj)) {
    LOG(WARNING) << "Argument type mismatch: expected multifield for argument "
                 << argpos << " of " << func_name;
    return;
  }
  internal::DataObjectToValues(&obj, values);
}

void GetArgument(void* env, const std::string& func_name, int argpos,
                 Symbol* value) {
  DATA_OBJECT dataobj;
  if (!EnvArgTypeCheck(env, "", argpos, SYMBOL, &dataobj)) {
    LOG(WARNING) << "Argument type mismatch: expected symbol for argument "
                 << argpos << " of " << func_name;
    return;
  }
  value->Set(EnvRtnLexeme(env, argpos));
}

absl::Status CheckFunctionArgumentCount(void* env, const std::string& func_name,
                                        size_t arg_count) {
  if (EnvArgCountCheck(env, func_name.c_str(), EXACTLY, arg_count) == -1) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Invalid number of arguments to '%s', "
                        "expected %u, got %u",
                        func_name, arg_count, EnvRtnArgCount(env)));
  }
  return absl::OkStatus();
}

void SetGenericOrMultifieldReturnValue(void* env, const Value& value,
                                       void* rv) {
  if (!internal::ValueToDataObject(env, value, static_cast<DATA_OBJECT_PTR>(rv))
           .ok()) {
    Value error_sym("INVALID-CALL", Value::Type::kString);
    if (!internal::ValueToDataObject(env, error_sym,
                                     static_cast<DATA_OBJECT_PTR>(rv))
             .ok()) {
      LOG(FATAL) << "Internal error, cannot return INVALID-CALL symbol";
    }
  }
}

void SetGenericOrMultifieldReturnValue(void* env, const Values& values,
                                       void* rv) {
  if (!internal::ValuesToDataObject(env, values,
                                    static_cast<DATA_OBJECT_PTR>(rv))
           .ok()) {
    Values error_values = {Value("INVALID-CALL-MF", Value::Type::kString)};
    if (!internal::ValuesToDataObject(env, error_values,
                                      static_cast<DATA_OBJECT_PTR>(rv))
             .ok()) {
      LOG(FATAL) << "Internal error, cannot return INVALID-CALL-MF symbol";
    }
  }
}

}  // namespace detail
}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
