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

#ifndef INTRINSIC_UTIL_AGGREGATE_TYPE_H_
#define INTRINSIC_UTIL_AGGREGATE_TYPE_H_

#include <optional>
#include <set>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace intrinsic {

namespace aggregate_type_details {
// check if types Ts... are the first types of ordered list Us...
// Note: we call recursively rather as a single pack expansion to allow
// Us... to be longer than Ts...
template <typename, typename>
struct IsFrontOf : std::false_type {};

template <typename... Us>
struct IsFrontOf<std::tuple<>, std::tuple<Us...>> : std::true_type {};

template <typename T, typename U, typename... Ts, typename... Us>
struct IsFrontOf<std::tuple<T, Ts...>, std::tuple<U, Us...>>
    : std::conjunction<std::is_same<T, U>,
                       IsFrontOf<std::tuple<Ts...>, std::tuple<Us...>>> {};

// check if types Ts... are an ordered subset Us... with no gaps
template <typename, typename>
struct IsSequentialSubsetOf : std::false_type {};

template <>
struct IsSequentialSubsetOf<std::tuple<>, std::tuple<>> : std::true_type {};

template <typename U, typename... Ts, typename... Us>
struct IsSequentialSubsetOf<std::tuple<Ts...>, std::tuple<U, Us...>>
    : std::disjunction<
          IsFrontOf<std::tuple<Ts...>, std::tuple<U, Us...>>,
          IsSequentialSubsetOf<std::tuple<Ts...>, std::tuple<Us...>>> {};

// check if types Ts... are a subset Us..., but allow gaps or different orders
template <typename, typename>
struct IsSubsetOf : std::false_type {};

template <typename... Us>
struct IsSubsetOf<std::tuple<>, std::tuple<Us...>> : std::true_type {};

template <typename T, typename... Ts, typename... Us>
struct IsSubsetOf<std::tuple<T, Ts...>, std::tuple<Us...>>
    : std::conjunction<std::disjunction<std::is_same<T, Us>...>,
                       IsSubsetOf<std::tuple<Ts...>, std::tuple<Us...>>> {};

// check if constructable with single value brace-init-list
template <typename, typename, typename = std::void_t<>>
struct IsBraceConstructible : std::false_type {};

template <typename T, typename U>
struct IsBraceConstructible<T, U, std::void_t<decltype(T{std::declval<U>()})>>
    : std::true_type {};

template <typename... Ts>
struct AggregateType : public Ts... {
  using TypesList = std::tuple<Ts...>;

  AggregateType() = default;

  // copies all the types from another AggregateType.
  // if the other type is not a strict superset of this,
  // the static_cast will fail
  template <typename... Us, typename = std::enable_if_t<IsSubsetOf<
                                TypesList, std::tuple<Us...>>::value>>
  AggregateType(const AggregateType<Us...>& other)  // NOLINT
      : Ts(static_cast<const Ts&>(other))... {}

  // forwards all arguments to initializer constructors of all Ts.
  // if all Ts are not initializeable from the 1-to-1 mapping of
  // args, this will fail
  template <typename... Us, typename = std::enable_if_t<std::conjunction_v<
                                IsBraceConstructible<Ts, Us>...>>>
  AggregateType(Us&&... vals) : Ts{std::forward<Us>(vals)}... {}  // NOLINT

  // copies all the types from another AggregateType via assignment.
  // if the other type is not a strict superset of this,
  // the static_cast will fail
  template <typename... Us, typename = std::enable_if_t<IsSubsetOf<
                                TypesList, std::tuple<Us...>>::value>>
  AggregateType<Ts...>& operator=(const AggregateType<Us...>& other) {
    *this = AggregateType<Ts...>(other);
    return *this;
  }

  // allows mutable reference casting to any aggregate type that is
  // is a sequential subset of this type
  template <typename R, typename = std::enable_if_t<IsSequentialSubsetOf<
                            typename R::TypesList, TypesList>::value>>
  operator R&() {  // NOLINT
    using CastTo = std::tuple_element_t<0, typename R::TypesList>;
    return reinterpret_cast<R&>(static_cast<CastTo&>(*this));
  }

  bool operator==(const AggregateType<Ts...>& other) const {
    return (... && (static_cast<Ts>(*this) == static_cast<Ts>(other)));
  }

  bool operator!=(const AggregateType<Ts...>& other) const {
    return !operator==(other);
  }
};

}  // namespace aggregate_type_details

template <typename... Ts>
using AggregateType = aggregate_type_details::AggregateType<Ts...>;

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_AGGREGATE_TYPE_H_
