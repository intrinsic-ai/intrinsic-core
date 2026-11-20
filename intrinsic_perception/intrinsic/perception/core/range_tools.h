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

#ifndef INTRINSIC_PERCEPTION_CORE_RANGE_TOOLS_H_
#define INTRINSIC_PERCEPTION_CORE_RANGE_TOOLS_H_

#include <algorithm>
#include <functional>
#include <iterator>
#include <optional>
#include <type_traits>
#include <vector>

#include "absl/container/flat_hash_set.h"

namespace intrinsic {
namespace perception {

// Transforms all elements in an input vector and returns a transformed
// elements.
//
// Examples:
// std::vector<int> values = {1, 2, 3, 4, 5};
// std::vector<double> sqrt_values = transformed(values, [](int i){
//   return std::sqrt(i);
// });
template <typename Range, typename Func>
auto Transformed(const Range& data, Func func) {
  using T = std::remove_reference_t<decltype(*std::begin(data))>;
  std::vector<std::invoke_result_t<Func, T>> transformed_data;
  transformed_data.reserve(data.size());
  for (const auto& element : data) {
    transformed_data.push_back(func(element));
  }
  return transformed_data;
}

// Returns a map with values transformed by the passed function.
template <template <typename, typename> class MapType, typename Key,
          typename Value, typename Func>
auto TransformedValues(const MapType<Key, Value>& map, Func func) {
  MapType<Key, std::invoke_result_t<Func, Value>> transformed_map;
  for (const auto& [key, value] : map) {
    transformed_map[key] = func(value);
  }
  return transformed_map;
}

// Returns a copy of the value associated with a key if it exists, otherwise
// returns std::nullopt.
template <typename MapType>
std::optional<typename MapType::mapped_type> OptionalCopyAt(
    const MapType& map, const typename MapType::key_type& key) {
  if (auto it = map.find(key); it != map.end()) {
    return it->second;
  } else {
    return std::nullopt;
  }
}

// Checks whether all elements (or their projected keys) in a range are unique.
template <typename Range, typename Proj = std::identity>
bool IsUnique(const Range& range, Proj proj = {}) {
  if (range.size() <= 1) {
    return true;
  }

  using Value = std::iter_value_t<decltype(std::begin(range))>;
  using Key = std::decay_t<std::invoke_result_t<Proj&, const Value&>>;
  absl::flat_hash_set<Key> seen;
  seen.reserve(range.size());
  for (const Value& element : range) {
    if (!seen.insert(std::invoke(proj, element)).second) {
      return false;
    }
  }
  return true;
}

// Checks whether all elements in a range are contained in a container.
template <typename Range>
bool Contains(const absl::flat_hash_set<typename Range::value_type>& container,
              const Range& range) {
  using T = typename Range::value_type;

  return std::all_of(range.begin(), range.end(), [&](const T& element) {
    return container.contains(element);
  });
}

}  // namespace perception
}  // namespace intrinsic

#endif  // INTRINSIC_PERCEPTION_CORE_RANGE_TOOLS_H_
