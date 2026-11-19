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

#ifndef INTRINSIC_WORLD_HASHING_HASHING_H_
#define INTRINSIC_WORLD_HASHING_HASHING_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "intrinsic/util/string_type.h"
#include "intrinsic/world/entity_id.h"

namespace intrinsic {

namespace internal {

// Scrambles all bits of an integer (the finalizer of MurmurHash3, fmix64).
// Ref:
// https://github.com/aappleby/smhasher/blob/07bb4de10a63e8cc2e1724865454eba635742383/src/MurmurHash3.cpp#L81-L90
//
// Why this is needed for absl::flat_hash_{set,map}:
//
// absl's Swiss tables use the hash value of a key directly, without remixing
// it, unless the hasher is absl::Hash. A custom hasher's value `h` is only
// XORed with a per-table seed, which is a 16 bit random value sign extended to
// 64 bits: its upper 48 bits are all ones. The table then splits `h ^ seed`
// into two parts:
//  - H1, the lowest log2(capacity + 1) bits, selects the group where probing
//    starts.
//  - H2, the 7 most significant bits, is stored in the control byte of the
//    slot. A lookup compares H2 against the 16 control bytes of a group in one
//    instruction and only compares keys for the slots that match.
//
// std::hash of an integer is the identity function in libc++. Entity ids are
// 32 bit values made of a 16 bit prefix followed by a small 16 bit counter, so
// their identity hashes:
//  - have the 32 most significant bits equal to zero. The H2 of every key is
//    the same, the SIMD filter matches every full slot and each lookup does a
//    key comparison for every full slot it visits.
//  - differ in the lowest bits only by their counter, so that ids with
//    different prefixes but the same counter start probing in the same group.
//    Probe sequences grow long and lookups, misses in particular, scan large
//    parts of the table.
//
// More details here:
// https://github.com/abseil/abseil-cpp/blob/master/absl/container/internal/raw_hash_set.h
//
// fmix64 is a bijection on 64 bit values in which every output bit depends on
// every input bit (avalanche). Both H1 and H2 are thus close to uniformly
// distributed even for keys that differ only in a few low bits, which is what
// Swiss tables assume. Being a bijection, distinct keys never collide on the
// full 64 bit hash. It is stateless and constexpr, so that every library and
// process computes the same hash for the same value, which b/380032603
// requires (absl::Hash is not used for that reason).
constexpr std::size_t MixIntegerHash(std::uint64_t x) noexcept {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return static_cast<std::size_t>(x);
}

// Hashes a value of a type that has a hash function in the standard library.
// Integers, enums, and pointers are scrambled, see MixIntegerHash().
template <typename T>
std::size_t HashValue(const T& value) noexcept {
  if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
    static_assert(sizeof(T) <= sizeof(std::uint64_t));
    return MixIntegerHash(static_cast<std::uint64_t>(value));
  } else if constexpr (std::is_pointer_v<T>) {
    static_assert(sizeof(std::uintptr_t) <= sizeof(std::uint64_t));
    return MixIntegerHash(
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(value)));
  } else {
    return std::hash<T>{}(value);
  }
}

template <typename T>
std::size_t hash_world_object(const T& s) noexcept {
  return HashValue<std::decay_t<decltype(s.value())>>(s.value());
}

// A simple hash combine function, used to combine multiple hashes into one.
inline std::size_t hash_combine(std::size_t a, std::size_t b) {
  return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
}
}  // namespace internal

// This file contains a collection of hash functions for the various types
// used in the world library.
// The goal is to provide a workaround for b/380032603.

template <typename T>
struct WorldHasher {
  std::size_t operator()(const T& s) const noexcept {
    return internal::HashValue(s);
  }
};

template <>
struct WorldHasher<std::string> {
  using is_transparent = void;  // Enables heterogeneous lookup

  std::size_t operator()(const std::string& s) const noexcept {
    return std::hash<std::string>{}(s);
  }
  std::size_t operator()(std::string_view s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
  size_t operator()(const char* cstr) const {
    return std::hash<std::string_view>{}(cstr);
  }
};

template <typename T>
struct WorldHasher<StringType<T>> {
  std::size_t operator()(const StringType<T>& s) const noexcept {
    return internal::hash_world_object(s);
  }
};

template <>
struct WorldHasher<TypedEntityId<>> {
  std::size_t operator()(const TypedEntityId<>& s) const noexcept {
    return internal::hash_world_object(s);
  }
};

template <typename... Types>
struct WorldHasher<TypedEntityId<Types...>> {
  std::size_t operator()(const TypedEntityId<Types...>& s) const noexcept {
    return internal::hash_world_object(s);
  }
};

template <typename T, typename U>
struct WorldHasher<std::pair<T, U>> {
  std::size_t operator()(const std::pair<T, U>& s) const noexcept {
    return internal::hash_combine(WorldHasher<T>{}(s.first),
                                  WorldHasher<U>{}(s.second));
  }
};

template <typename X, typename Y>
struct WorldHasher<std::tuple<X, Y>> {
  std::size_t operator()(const std::tuple<X, Y>& s) const noexcept {
    return internal::hash_combine(WorldHasher<X>{}(std::get<0>(s)),
                                  WorldHasher<Y>{}(std::get<1>(s)));
  }
};

template <>
struct WorldHasher<EntityId> {
  std::size_t operator()(const EntityId& s) const noexcept {
    return internal::hash_world_object(s);
  }
};

// Hash function for absl::flat_hash_set. This function sorts the elements of
// the set before hashing them. This is done to ensure that the hash of a set is
// independent of the order of the elements.
template <typename T, typename H>
struct WorldHasher<absl::flat_hash_set<T, H>> {
  std::size_t operator()(const absl::flat_hash_set<T, H>& s) const noexcept {
    if (s.empty()) {
      return 0;
    }
    std::vector<size_t> hashes;
    for (const auto& t : s) {
      hashes.push_back(H{}(t));
    }
    std::sort(hashes.begin(), hashes.end());
    auto hash = hashes[0];
    for (size_t i = 1; i < hashes.size(); ++i) {
      hash = internal::hash_combine(hash, hashes[i]);
    }
    return hash;
  }
};

// Forward declaration of the WorldHasher template to prevent circular
// dependencies.
template <typename T>
struct WorldHasher;

template <typename T>
using WorldHashSet = absl::flat_hash_set<T, WorldHasher<T>>;

template <typename K, typename V>
using WorldHashMap = absl::flat_hash_map<K, V, WorldHasher<K>>;

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_HASHING_HASHING_H_
