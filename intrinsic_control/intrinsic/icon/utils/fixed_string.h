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

#ifndef INTRINSIC_ICON_UTILS_FIXED_STRING_H_
#define INTRINSIC_ICON_UTILS_FIXED_STRING_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

#include "absl/base/macros.h"
#include "absl/strings/string_view.h"

namespace intrinsic::icon {

// A string type with a fixed maximum capacity of 'MaxSize' characters.
// It is safe to use in realtime contexts because it allocates on the stack.
// If input exceeds 'MaxSize', it truncates and does not throw errors.
// So, callers should ensure that capacity is sufficient.
//
// The interface is intentionally minimal. If more features are desired, we
// should instead customize basic_string or import prior art like
// https://github.com/electronicarts/EASTL/blob/master/include/EASTL/fixed_string.h
//
// You should not pass this type by constant reference, because this would limit
// functions to a fixed maximum length 'MaxSize'. In almost all cases, it is
// better to pass a 'absl::string_view' instead, this will accept any maximum
// size.
//
//   // BAD: Library function takes const reference of FixedString.
//   void Print(const FixedString<10>& s);
//
//   // GOOD: Library function takes absl::string_view.
//   void Print(absl::string_view s);
template <std::size_t MaxSize>
class FixedString {
 public:
  static constexpr std::size_t kMaxSize = MaxSize;

  FixedString() = default;

  // FixedString is copy-able.s
  FixedString(const FixedString& other) = default;
  FixedString& operator=(const FixedString& other) = default;

  // FixedString is move-able.
  FixedString(FixedString&& other) noexcept = default;
  FixedString& operator=(FixedString&& other) noexcept = default;

  // Implicit conversion from absl::string_view allowed for symmetry with
  // implicit conversion to absl::string_view.
  FixedString(  // NOLINT(google-explicit-constructor)
      const absl::string_view s) {
    size_ = std::min(MaxSize, s.size());
    std::copy(s.begin(), s.begin() + size_, data_.begin());
  }

  // Truncates. No operation if full (`size() == MaxSize`).
  void append(const absl::string_view s) {
    std::size_t new_size = std::min(MaxSize, size_ + s.size());
    // Allowed because new_size >= size_.
    std::size_t chars_to_write = new_size - size_;
    std::copy(s.begin(), s.begin() + chars_to_write, data_.begin() + size_);
    size_ = new_size;
  }

  void resize(size_t count) {
    ABSL_HARDENING_ASSERT(count <= MaxSize);

    if (count > size_) {
      std::fill(data_.begin() + size_, data_.begin() + count, char());
    }
    size_ = count;
  }

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  static constexpr size_t max_size() { return MaxSize; }

  char& operator[](size_t idx) {
    ABSL_HARDENING_ASSERT(idx < size());
    return data_[idx];
  }

  friend bool operator==(const FixedString& lhs, const FixedString& rhs) {
    return std::equal(lhs.data_.begin(), lhs.data_.begin() + lhs.size_,
                      rhs.data_.begin(), rhs.data_.begin() + rhs.size_) &&
           lhs.size_ == rhs.size_;
  }

  friend bool operator!=(const FixedString& lhs, const FixedString& rhs) {
    return !(lhs == rhs);
  }

  template <typename H>
  friend H AbslHashValue(H h, const FixedString& c) {
    return H::combine(
        H::combine_contiguous(std::move(h), c.data_.data(), c.size_), c.size_);
  }

  operator absl::string_view() const {  // NOLINT(google-explicit-constructor)
    return absl::string_view(data_.data(), size_);
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const FixedString& s) {
    sink.Append(absl::string_view{s});
  }

 private:
  std::array<char, MaxSize> data_;
  std::size_t size_ = 0;
};

// Explicit function to convert a single character into a FixedString.
inline FixedString<1> SingleCharacterString(char ch) {
  FixedString<1> str;
  str.resize(1);
  str[0] = ch;
  return str;
}

// Trait to check whether `T` is a `FixedString<N>`.
template <typename T>
struct is_fixed_string : std::false_type {};

template <std::size_t N>
struct is_fixed_string<FixedString<N>> : std::true_type {};

template <typename T>
inline constexpr bool is_fixed_string_v = is_fixed_string<T>::value;

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_UTILS_FIXED_STRING_H_
