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

#include "intrinsic/executive/clips_cpp/value_util.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/status/status_macros.h"
// Keep CLIPS at the bottom to prevent macro pollution.
#include "clips/clips.h"

namespace intrinsic {
namespace executive {
namespace clips {
namespace internal {

void DataObjectToValues(const DATA_OBJECT* dataobj, Values* values) {
  switch (GetpType(dataobj)) {
    case FLOAT: {
      double d = DOPToDouble(dataobj);
      values->emplace_back(d);
      break;
    }
    case INTEGER: {
      int64_t i = DOPToLong(dataobj);
      values->emplace_back(i);
      break;
    }
    case SYMBOL: {
      std::string sym = DOPToString(dataobj);
      values->emplace_back(sym, Value::Type::kSymbol);
      break;
    }
    case STRING: {
      std::string str = DOPToString(dataobj);
      values->emplace_back(str, Value::Type::kString);
      break;
    }
    case MULTIFIELD: {
      int64_t begin = GetpDOBegin(dataobj);
      int64_t end = GetpDOEnd(dataobj);
      void* mf_ptr = GetpValue(dataobj);
      for (int64_t i = begin; i <= end; ++i) {
        switch (GetMFType(mf_ptr, i)) {
          case FLOAT: {
            double d = ValueToDouble(GetMFValue(mf_ptr, i));
            values->emplace_back(d);
            break;
          }
          case INTEGER: {
            int64_t int_val = ValueToLong(GetMFValue(mf_ptr, i));
            values->emplace_back(int_val);
            break;
          }
          case SYMBOL: {
            std::string sym = ValueToString(GetMFValue(mf_ptr, i));
            values->emplace_back(sym, Value::Type::kSymbol);
            break;
          }
          case STRING: {
            std::string str = ValueToString(GetMFValue(mf_ptr, i));
            values->emplace_back(str, Value::Type::kString);
            break;
          }
          case EXTERNAL_ADDRESS: {
            void* ptr = ValueToExternalAddress(GetMFValue(mf_ptr, i));
            values->emplace_back(ptr);
            break;
          }
          default:
            LOG(WARNING) << "Encountered unknown multifield field type";
            break;
        }
      }
      break;
    }
    case EXTERNAL_ADDRESS: {
      void* ptr = DOPToExternalAddress(dataobj);
      values->emplace_back(ptr);
      break;
    }
    case RVOID:
      break;  // nothing to do, returns void
    default:
      LOG(WARNING) << "Encountered unknown return type";
      break;
  }
}

Values DataObjectToValues(const DATA_OBJECT* dataobj) {
  Values values;
  DataObjectToValues(dataobj, &values);
  return values;
}

std::vector<std::string> FilterDataObjectStrings(const DATA_OBJECT* dataobj) {
  Values values;
  DataObjectToValues(dataobj, &values);
  values.erase(std::remove_if(values.begin(), values.end(),
                              [](const Value& v) {
                                return v.GetValueType() !=
                                           Value::Type::kString &&
                                       v.GetValueType() != Value::Type::kSymbol;
                              }),
               values.end());
  std::vector<std::string> rv;
  rv.reserve(values.size());
  std::transform(values.begin(), values.end(), std::back_inserter(rv),
                 [](const Value& v) { return v.GetStringOrSymbol().value(); });
  return rv;
}

absl::Status ValueToDataObject(void* env, const Value& value,
                               DATA_OBJECT* dataobj) {
  if (value.GetValueType() == Value::Type::kUnknown) {
    return absl::InvalidArgumentError(
        "Cannot set data object "
        "to value of unknown type");
  }

  SetpType(dataobj, static_cast<int>(value.GetValueType()));
  switch (value.GetValueType()) {
    case Value::Type::kFloat: {
      INTR_ASSIGN_OR_RETURN(double d, value.GetDouble());
      void* dp = EnvAddDouble(env, d);
      SetpValue(dataobj, dp);
      break;
    }
    case Value::Type::kInteger: {
      INTR_ASSIGN_OR_RETURN(int64_t i, value.GetInteger());
      void* ip = EnvAddLong(env, i);
      SetpValue(dataobj, ip);
      break;
    }
    case Value::Type::kString:
      [[fallthrough]];
    case Value::Type::kSymbol: {
      INTR_ASSIGN_OR_RETURN(std::string s, value.GetStringOrSymbol());
      void* sp = EnvAddSymbol(env, s.c_str());
      SetpValue(dataobj, sp);
      break;
    }
    case Value::Type::kExternalAddress: {
      INTR_ASSIGN_OR_RETURN(void* p, value.GetPointer());
      void* pp = EnvAddExternalAddress(env, p, EXTERNAL_ADDRESS);
      SetpValue(dataobj, pp);
      break;
    }
    default:
      return absl::InvalidArgumentError(
          "Cannot set data object "
          "to unsuported type");
  }
  return absl::OkStatus();
}

absl::Status ValuesToDataObject(void* env, const Values& values,
                                DATA_OBJECT* dataobj) {
  void* mfp = EnvCreateMultifield(env, values.size());

  for (size_t i = 0; i < values.size(); ++i) {
    // indexes in multifiled start at 1
    size_t mfi = i + 1;

    SetMFType(mfp, mfi, static_cast<int>(values[i].GetValueType()));

    switch (values[i].GetValueType()) {
      case Value::Type::kFloat: {
        INTR_ASSIGN_OR_RETURN(double d, values[i].GetDouble());
        void* dp = EnvAddDouble(env, d);
        SetMFValue(mfp, mfi, dp);
        break;
      }
      case Value::Type::kInteger: {
        INTR_ASSIGN_OR_RETURN(int64_t iv, values[i].GetInteger());
        void* ivp = EnvAddLong(env, iv);
        SetMFValue(mfp, mfi, ivp);
        break;
      }
      case Value::Type::kString:
        [[fallthrough]];
      case Value::Type::kSymbol: {
        INTR_ASSIGN_OR_RETURN(std::string s, values[i].GetStringOrSymbol());
        void* sp = EnvAddSymbol(env, s.c_str());
        SetMFValue(mfp, mfi, sp);
        break;
      }
      case Value::Type::kExternalAddress: {
        INTR_ASSIGN_OR_RETURN(void* p, values[i].GetPointer());
        void* pp = EnvAddExternalAddress(env, p, EXTERNAL_ADDRESS);
        SetMFValue(mfp, mfi, pp);
        break;
      }
      default:
        return absl::InvalidArgumentError(
            "Cannot set multifield value "
            "to unsuported type");
    }
  }
  SetpType(dataobj, MULTIFIELD);
  SetpValue(dataobj, mfp);
  SetpDOBegin(dataobj, 1);
  SetpDOEnd(dataobj, values.size());
  return absl::OkStatus();
}

}  // namespace internal
}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
