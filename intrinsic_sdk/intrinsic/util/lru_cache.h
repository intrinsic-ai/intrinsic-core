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

#ifndef INTRINSIC_UTIL_LRU_CACHE_H_
#define INTRINSIC_UTIL_LRU_CACHE_H_

#include <cstddef>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/log/log.h"
#include "third_party/simple_lru_cache/simple_lru_cache_inl.h"

namespace intrinsic {
namespace container_internal_details {

// Use absl::DefaultHashContainerHash instead when absl is updated in github.
template <class K, class V>
using HashContainerDefaults = absl::flat_hash_map<K, V>::hasher;

// Use absl::DefaultHashContainerEq instead when absl is updated in github.
template <class K, class V>
using EqualsContainerDefaults = absl::flat_hash_map<K, V>::key_equal;

}  // namespace container_internal_details

// We subclass the LRUCache so that we can add our own thrashing detection.
template <
    class KeyType, class ValueType,
    class HashFunc =
        container_internal_details::HashContainerDefaults<KeyType, ValueType>,
    class EqualsFunc =
        container_internal_details::EqualsContainerDefaults<KeyType, ValueType>>
class LruCache
    : public google::simple_lru_cache::SimpleLRUCache<KeyType, ValueType,
                                                      HashFunc, EqualsFunc> {
 public:
  explicit LruCache(size_t cache_size)
      : google::simple_lru_cache::SimpleLRUCache<KeyType, ValueType, HashFunc,
                                                 EqualsFunc>(cache_size) {}

  void MarkAdded(const KeyType& key) {
    auto& data = thrashing_data_[key];
    if (++data.first % 3 == 0) {
      LOG(WARNING) << "Thrashing[" << data.first << "][" << data.second
                   << "] detected for key: " << key;
    }
  }

  void MarkRemoved(const KeyType& key) {
    auto& data = thrashing_data_[key];
    if (++data.second % 3 == 0) {
      LOG(WARNING) << "Thrashing[" << data.first << "][" << data.second
                   << "] detected for key: " << key;
    }
  }

 private:
  // The map is keyed on the cache key and holds a pair<added, removed>. If we
  // see that the same key was added or removed multiple times, we treat as
  // cache thrashing and will log a warning. This is to help track down
  // potential slowdowns from cache misses.
  absl::flat_hash_map<KeyType, std::pair<int, int>, HashFunc, EqualsFunc>
      thrashing_data_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_LRU_CACHE_H_
