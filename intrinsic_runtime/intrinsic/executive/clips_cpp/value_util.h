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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_VALUE_UTIL_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_VALUE_UTIL_H_

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/status/status_macros.h"

extern "C" {
// This is a C forward declaration and must match exactly the C header.
// NOLINTNEXTLINE(modernize-use-using)
typedef struct dataObject DATA_OBJECT;
}  // extern "C"

namespace intrinsic {
namespace executive {
namespace clips {
namespace internal {

// Converts a generic CLIPS data object to Values.
// If the dataobj references a single rather than a multi field, returns the
// value as first (and only) element.
Values DataObjectToValues(const DATA_OBJECT* dataobj);
// The same function, but pass return value by pointer
void DataObjectToValues(const DATA_OBJECT* dataobj, Values* values);

// Convert a data object to a list of strings, ignore non-string values.
std::vector<std::string> FilterDataObjectStrings(const DATA_OBJECT* dataobj);

// Convert Values to a CLIPS data object
absl::Status ValuesToDataObject(void* env, const Values& values,
                                DATA_OBJECT* dataobj);

// Convert single Value to a CLIPS data object.
// Must be CLIPS env pointer. Will crash if env is nullptr.
absl::Status ValueToDataObject(void* env, const Value& value,
                               DATA_OBJECT* dataobj);

}  // namespace internal
}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_VALUE_UTIL_H_
