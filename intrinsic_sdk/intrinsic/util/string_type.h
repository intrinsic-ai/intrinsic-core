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

// Typed strings.
//
// Often, we wish to distinguish between different kinds of strings, treat them
// as types in their own right and disallow assignment between them. This file
// uses some macro and template metaprogramming to define strongly typed
// strings.
//
// Example usage:
//
//  INTRINSIC_DEFINE_STRING_TYPE(Foo);
//  INTRINSIC_DEFINE_STRING_TYPE(Bar);
//  Foo foo("foo_value");
//  Bar bar("bar_value");
//
// The following two statements will not compile.
//
//  foo = bar;
//
//  if (foo == bar) { } else { }; ...
//
// The strongly-typed types are hashable with the Abseil hashing framework,
// so they work out of the box with any modern hashing system. For non-Abseil
// uses, the explicit functors absl::Hash<Foo>, absl::Hash<Bar> may be used:
//
//  std::unordered_set<Foo, absl::Hash<Foo>>
//  __gnu_cxx::hash_map<Bar, int, absl::Hash<Bar>>
//
// (But absl::flat_hash_set<Foo> is much better!)

#ifndef INTRINSIC_UTIL_STRING_TYPE_H_
#define INTRINSIC_UTIL_STRING_TYPE_H_

#include <memory>
#include <ostream>  // NOLINT
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/flags/marshalling.h"
#include "absl/hash/hash.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"

// Defines the StringType using StringTypeRepresentation and provides a type
// alias to string_type_name.  The struct string_type_name ## _tag_ trickery is
// needed to ensure that a new type is created per string_type_name.
//
// StringTypeRepresentation classes, as a rule, should either *be* a string-like
// object, or should provide a "value()" method that returns a string-like
// object.  If they can provide more optimal implementations of relational
// operators, they should define operator== and operator<; all other relational
// operators are defined in terms of those.  If they can provide more optimal
// implementations of AbslHashValue or operator<<, they should provide those as
// well.
#define INTRINSIC_DEFINE_STRING_TYPE_AS(string_type_name,             \
                                        StringTypeRepresentation)     \
  struct string_type_name##_tag_ {                                    \
    static absl::string_view TypeName() { return #string_type_name; } \
    using Representation = StringTypeRepresentation;                  \
  };                                                                  \
  using string_type_name = ::intrinsic::StringType<string_type_name##_tag_>;

#define INTRINSIC_DEFINE_STRING_TYPE(string_type_name) \
  INTRINSIC_DEFINE_STRING_TYPE_AS(string_type_name, std::string);

namespace intrinsic {

// StringType provides these operations:
//   * relational operators (==, !=, <, <=, >, >=)
//   * compare (future <=> operator)
//   * AbslHashValue
//   * streaming with operator<<
//   * value(), which should return a string-like object (const string&,
//     absl::string_view, ShortString<N>, etc.)
//
// It is parameterized here by a "TagAndRepresentation" struct.  It's a unique
// struct defined in each INTRINSIC_DEFINE_STRING_TYPE_AS macro invocation, so
// every StringType is its own unique, strong type.  In addition, it provides a
// static TypeName() method that returns the name of the type, and it provides a
// "Representation" type alias which is held as a member by each StringType
// instance.
//
// When a StringType instance method is called, it will first try to dispatch
// the call directly to the Representation instance.  If the Representation
// class doesn't have this method, StringType will call value() on its
// representation instance, and then call the method on the returned value.
// We do this dispatch at compile-time using go/sfinae.
template <typename TagAndRepresentation>
class StringType {
 public:
  // Shorthand.
  using Rep = typename TagAndRepresentation::Representation;

 private:
  // Using SFINAE, you can "rank" implementations using this mechanism.  Given
  // two acceptable implementations that substitute correctly, the compiler will
  // choose the one that requires fewer type conversions, so a method given a
  // Rank0 object will prefer an implementation that accepts a Rank0 type to an
  // implementation that accepts a Rank1 type, since a Rank1 argument requires a
  // derived->base conversion.
  struct Rank1 {};
  struct Rank0 : Rank1 {};

  // This would normally be declared at the bottom of the class, but we need to
  // move it up here to satisfy the compiler.
  //
  // Since the return type of value(), below, is either the same as the return
  // type of t_.value(), if such a method exists, or t_ itself, if t_.value()
  // doesn't exist, we need to declare our t_ up here so the compiler knows
  // what it is and whether it has a value() method.
  Rep t_;

  // These must be defined before value() is defined below, since value()'s
  // return type is determined by what these methods return.
  template <typename T>
  static auto DispatchValue(const T& t, Rank0) -> decltype(t.value()) {
    return t.value();
  }
  static const Rep& DispatchValue(const Rep& t, Rank1) { return t; }

 public:
  StringType() = default;
  template <typename T, typename = absl::enable_if_t<
                            std::is_constructible<Rep, T&&>::value>>
  explicit StringType(T&& value) : t_(std::forward<T>(value)) {}

  // Returns the name of this StringType, as used in code.
  static absl::string_view TypeName() {
    return TagAndRepresentation::TypeName();
  }

  // Returns the result of the representation's value() method, or the
  // representation itself if the representation has no value() method.
  auto value() const -> decltype(DispatchValue(t_, Rank0{})) {
    return DispatchValue(t_, Rank0{});
  }

  bool empty() const { return DispatchEmpty(t_, Rank0{}); }

  // If you want to optimize your relational methods, you need only implement
  // these three: compare, operator==, and operator<.
  int compare(const StringType& other) const {
    return DispatchCompare(*this, other, Rank0{});
  }
  friend bool operator==(const StringType& left, const StringType& right) {
    return DispatchEquals(left, right, Rank0{});
  }
  friend bool operator<(const StringType& left, const StringType& right) {
    return DispatchLessThan(left, right, Rank0{});
  }

  // These methods are defined in terms of the above.
  friend bool operator!=(const StringType& left, const StringType& right) {
    return !(left == right);
  }
  friend bool operator>(const StringType& left, const StringType& right) {
    return right < left;
  }
  friend bool operator<=(const StringType& left, const StringType& right) {
    return !(left > right);
  }
  friend bool operator>=(const StringType& left, const StringType& right) {
    return !(left < right);
  }

  template <typename H>
  friend H AbslHashValue(H h, const StringType& s) {
    return s.DispatchHash(std::move(h), Rank0{});
  }

  friend std::ostream& operator<<(std::ostream& os, const StringType& s) {
    return DispatchOstream(os, s.t_, Rank0{});
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const StringType& s) {
    std::stringstream ss;
    ss << s;
    sink.Append(ss.str());
  }

 private:
  template <typename T>
  static auto DispatchEmpty(const T& t, Rank0) -> decltype(t.empty()) {
    return t.empty();
  }
  static bool DispatchEmpty(const Rep& t, Rank1) {
    return DispatchValue(t, Rank0{}).empty();
  }

  // This overload exists specifically for absl::Cord, which spells 'compare' as
  // 'Compare'.
  template <typename T>
  static auto DispatchCompare(const T& left, const T& right, Rank0)
      -> decltype(left.value().Compare(right.value())) {
    return left.value().Compare(right.value());
  }
  template <typename T>
  static auto DispatchCompare(const T& left, const T& right, Rank0)
      -> decltype(left.t_.compare(right.t_)) {
    return left.t_.compare(right.t_);
  }
  template <typename T>
  static auto DispatchCompare(const T& left, const T& right, Rank1)
      -> decltype(left.value().compare(right.value())) {
    return left.value().compare(right.value());
  }

  template <typename T>
  static auto DispatchEquals(const T& left, const T& right, Rank0)
      -> decltype(left.t_ == right.t_) {
    return left.t_ == right.t_;
  }
  static bool DispatchEquals(const StringType& left, const StringType& right,
                             Rank1) {
    return left.value() == right.value();
  }

  template <typename T>
  static auto DispatchLessThan(const T& left, const T& right, Rank0)
      -> decltype(left.t_ < right.t_) {
    return left.t_ < right.t_;
  }
  static bool DispatchLessThan(const StringType& left, const StringType& right,
                               Rank1) {
    return left.value() < right.value();
  }

  template <
      typename H, typename T = Rep,
      typename Hashable = decltype(absl::Hash<T>()(std::declval<const T&>()))>
  H DispatchHash(H h, Rank0) const {
    return H::combine(std::move(h), t_);
  }
  template <typename H>
  H DispatchHash(H h, Rank1) const {
    return H::combine(std::move(h), value());
  }

  template <typename T>
  static auto DispatchOstream(std::ostream& os, const T& t, Rank0)
      -> decltype(os << t) {
    return os << t;
  }
  static std::ostream& DispatchOstream(std::ostream& os, const Rep& t, Rank1) {
    return os << DispatchValue(t, Rank0{});
  }
};

// Allows typed strings to be used as ABSL_FLAG values.
//
// This is equivalent in behavior to just using a raw std::string.
template <typename TagAndRepresentation>
bool AbslParseFlag(absl::string_view text,
                   StringType<TagAndRepresentation>* out, std::string* error) {
  *out = StringType<TagAndRepresentation>(text);
  return true;
}

template <typename TagAndRepresentation>
std::string AbslUnparseFlag(const StringType<TagAndRepresentation>& val) {
  return absl::UnparseFlag(std::string(val.value()));
}

// Below here are a couple representations that have been found useful.
namespace internal {
inline const std::string& GlobalEmptyString() {
  static std::string* kEmpty = new std::string{};
  return *kEmpty;
}
inline bool IsNullOrEmpty(const char* p) { return !p || *p == '\0'; }
inline bool IsNullOrEmpty(absl::string_view sv) { return sv.empty(); }
}  // namespace internal

// SharedPtrStringRepresentation uses a std::shared_ptr<const string>, which is
// shared between all copied instances of a given StringType.  This makes it
// half the size of the above ValueStringRepresentation, can reduce memory usage
// when the strings are copied frequently, and sometimes improves the
// performance of copying in general, if the strings are relatively long.
class SharedPtrStringRepresentation {
 public:
  SharedPtrStringRepresentation() = default;

  template <typename T, typename = absl::enable_if_t<
                            std::is_constructible<std::string, T&&>::value>>
  explicit SharedPtrStringRepresentation(T&& value)
      : value_(internal::IsNullOrEmpty(value)
                   ? nullptr
                   : std::make_shared<std::string>(std::forward<T>(value))) {}

  const std::string& value() const {
    if (!value_) return internal::GlobalEmptyString();
    return *value_;
  }

  bool empty() const { return value_ == nullptr; }

 private:
  std::shared_ptr<const std::string> value_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STRING_TYPE_H_
