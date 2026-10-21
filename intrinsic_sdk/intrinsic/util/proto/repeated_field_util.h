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

#ifndef INTRINSIC_UTIL_PROTO_REPEATED_FIELD_UTIL_H_
#define INTRINSIC_UTIL_PROTO_REPEATED_FIELD_UTIL_H_

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "google/protobuf/repeated_field.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
// Remove all elements from the input array for which the input predicate
// pred is true. Returns number of erased elements.
template <typename T, typename Predicate>
int RemoveIf(google::protobuf::RepeatedPtrField<T>* array,
             const Predicate& pred) {
  int i = 0, end = array->size();
  while (i < end && !pred(&array->Get(i))) ++i;
  if (i == end) return 0;
  // 'i' is positioned at first element to be removed.
  for (int j = i + 1; j < end; ++j) {
    if (!pred(&array->Get(j))) array->SwapElements(j, i++);
  }
  array->DeleteSubrange(i, end - i);
  return end - i;
}
template <typename T, typename LessThan>
inline void Sort(google::protobuf::RepeatedPtrField<T>* array,
                 const LessThan& lt) {
  std::sort(array->pointer_begin(), array->pointer_end(), lt);
}

inline void Sort(google::protobuf::RepeatedPtrField<std::string>* array) {
  Sort(array,
       [](const std::string* x, const std::string* y) { return *x < *y; });
}

// Returns the repeated field as a vector.
template <typename T>
std::vector<T> AsVector(const google::protobuf::RepeatedField<T>& data) {
  return {data.begin(), data.end()};
}

// Returns the repeated pointer field as a vector.
template <typename T>
std::vector<T> AsVector(const google::protobuf::RepeatedPtrField<T>& data) {
  return {data.begin(), data.end()};
}

// Returns a vector as a 'RepeatedField<T>'.
template <typename T>
google::protobuf::RepeatedField<T> AsRepeatedField(const std::vector<T>& data) {
  return {data.begin(), data.end()};
}

// Returns a vector as a 'RepeatedPtrField<T>'.
template <typename T>
google::protobuf::RepeatedPtrField<T> AsRepeatedPtrField(
    const std::vector<T>& data) {
  return {data.begin(), data.end()};
}

namespace internal {
template <class ElementDestType, class RepeatedFieldVariant>
struct FromRepeatedFieldProto;
}  // namespace internal

// Converts from repeated fields to std::vector. Recursively calls FromProto
// on the elements.
template <class RepeatedFieldVariant>
auto FromProto(const RepeatedFieldVariant& data)
  requires(std::is_same_v<RepeatedFieldVariant,
                          google::protobuf::RepeatedField<
                              typename RepeatedFieldVariant::value_type>> ||
           std::is_same_v<RepeatedFieldVariant,
                          google::protobuf::RepeatedPtrField<
                              typename RepeatedFieldVariant::value_type>>)
{
  return internal::FromRepeatedFieldProto<decltype(FromProto(*data.begin())),
                                          RepeatedFieldVariant>::call(data);
}

// Template definitions.

namespace internal {

template <class ElementDestType, class RepeatedFieldVariant>
struct FromRepeatedFieldProto {
  static auto call(const RepeatedFieldVariant& data) {
    std::vector<ElementDestType> result;
    result.reserve(data.size());
    for (const auto& element : data) {
      result.push_back(FromProto(element));
    }
    return result;
  }
};

template <class ElementDestType, class RepeatedFieldVariant>
struct FromRepeatedFieldProto<absl::StatusOr<ElementDestType>,
                              RepeatedFieldVariant> {
  static absl::StatusOr<std::vector<ElementDestType>> call(
      const RepeatedFieldVariant& data) {
    std::vector<ElementDestType> result;
    result.reserve(data.size());
    for (const auto& element : data) {
      INTR_ASSIGN_OR_RETURN(auto value, FromProto(element));
      result.push_back(std::move(value));
    }
    return result;
  }
};

}  // namespace internal

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_REPEATED_FIELD_UTIL_H_
