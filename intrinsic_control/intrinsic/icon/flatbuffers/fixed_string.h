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

#ifndef INTRINSIC_ICON_FLATBUFFERS_FIXED_STRING_H_
#define INTRINSIC_ICON_FLATBUFFERS_FIXED_STRING_H_

#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "flatbuffers/array.h"
#include "intrinsic/icon/utils/fixed_string.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/icon/utils/strerror.h"

namespace intrinsic_fbs {

// Copies the contents of `source` to `destination`, up to the size of
// `destination` - 1 (to leave room for a trailing null byte). If `source` is
// shorter than `destination` - 1, the remaining bytes in `destination` are set
// to `\0`.
//
// The type `T` must be a character type, i.e. `char`, `unsigned char` or
// `signed char`.
// `size` is the size of the `destination` array and must be greater than 0.
// The function returns an InvalidArgumentError if `destination` is a nullptr or
// if copying the string fails due to an error in `absl::SNPrintF`.
template <typename T, uint16_t size>
  requires requires {
    std::is_same_v<T, char> || std::is_same_v<T, unsigned char> ||
        std::is_same_v<T, signed char>;
    size > 0;
  }
intrinsic::icon::RealtimeStatus StringCopy(
    ::flatbuffers::Array<T, size>* destination, absl::string_view source) {
  if (destination == nullptr) {
    return intrinsic::icon::InvalidArgumentError(
        "destination must not be nullptr");
  }
  // Zero-out the destination array just in case SNPrintF might fail below.
  std::memset(destination->Data(), '\0', size);
  auto data = reinterpret_cast<char*>(destination->Data());
  const int bytes_copied = absl::SNPrintF(data, size, "%s", source);
  if (bytes_copied != source.size()) {
    if (bytes_copied < 0) {
      return intrinsic::icon::InvalidArgumentError(
          intrinsic::icon::RealtimeStatus::StrCat(
              "Failed to copy to FixedString: ",
              intrinsic::icon::StrError(errno)));
    }
    if (bytes_copied < source.size()) {
      return intrinsic::icon::InvalidArgumentError(
          "Copied fewer bytes than expected to FixedString. Source might "
          "contain a null byte.");
    }
    if (bytes_copied > size - 1) {
      // This should never happen, but we check it just in case.
      return intrinsic::icon::InvalidArgumentError(
          "Copied more bytes than allowed.");
    }
    return intrinsic::icon::InternalError("Failed to copy to fixedstring.");
  }
  return intrinsic::icon::OkStatus();
}

// Creates a string_view from the contents of `source`.
//
// The type `T` must be a character type, i.e. `char`, `unsigned char` or
// `signed char`.
// `size` is the size of the `source` array and must be greater than 0.
// The string_view extends to the first `\0` byte in `source`, or to the end
// of `source` if no `\0` byte is found, which might cause undefined behavior.
//
// Note that this function does not allocate memory, so the returned
// string_view is valid as long as the original buffer is still allocated.
//
// The function returns an InvalidArgumentError if `source` is a nullptr or if
// the source string does not end with a null byte.
template <typename T, uint16_t size>
  requires requires {
    std::is_same_v<T, char> || std::is_same_v<T, unsigned char> ||
        std::is_same_v<T, signed char>;
    size > 0;
  }
intrinsic::icon::RealtimeStatusOr<absl::string_view> StringView(
    const ::flatbuffers::Array<T, size>* source) {
  if (source == nullptr) {
    return intrinsic::icon::InvalidArgumentError("source must not be nullptr");
  }
  const T* data = source->data();
  size_t string_size = size;
  for (size_t i = 0; i < size; ++i) {
    if (data[i] == '\0') {
      string_size = i;
      break;
    }
  }
  return absl::string_view{reinterpret_cast<const char*>(source->Data()),
                           string_size};
}

// Helper traits to get the size of a flatbuffers array.
namespace internal {
namespace flatbuffers {
template <typename>
struct ArraySize;

template <typename T, uint16_t size_value>
struct ArraySize<::flatbuffers::Array<T, size_value>> {
  static constexpr uint16_t value = size_value;
};
}  // namespace flatbuffers
}  // namespace internal

// Enable the function if T is a flatbuffers array, or more specifically,
// - if the array has a 1st field named "data" that is a pointer to a int8_t or
//   uint8_t array.
// - if the array has another field named "size" that is unsigned.
template <typename T,
          typename ArrayType = std::remove_cv_t<
              std::remove_pointer_t<typename T::Traits::template FieldType<0>>>,
          uint16_t MaxSize = internal::flatbuffers::ArraySize<ArrayType>::value>
  requires requires(T& obj) {
    requires std::is_convertible_v<decltype(obj.mutable_data()),
                                   ::flatbuffers::Array<int8_t, MaxSize>*> ||
                 std::is_convertible_v<decltype(obj.mutable_data()),
                                       ::flatbuffers::Array<uint8_t, MaxSize>*>;
    requires std::is_unsigned_v<decltype(obj.size())>;
    {
      obj.mutate_size(std::declval<decltype(obj.size())>())
    } -> std::same_as<void>;
  }
void StringCopy(T* destination, absl::string_view source) {
  if (destination == nullptr) {
    return;
  }
  intrinsic::icon::FixedString<MaxSize> fixed_string(
      source);  // Creating a FixedString that outlives the string_view.
  absl::string_view fixed_string_view{fixed_string};
  std::memcpy(destination->mutable_data()->Data(), fixed_string_view.data(),
              fixed_string_view.size());
  destination->mutate_size(fixed_string_view.size());
}

// Enable the function if T is a flatbuffers array, or more specifically,
// - if the array has a 1st field named "data" that is a pointer to a int8_t or
//   uint8_t array.
// - if the array has another field named "size" that is unsigned.
template <typename T,
          typename ArrayType = std::remove_cv_t<
              std::remove_pointer_t<typename T::Traits::template FieldType<0>>>,
          uint16_t MaxSize = internal::flatbuffers::ArraySize<ArrayType>::value>
  requires requires(T& obj) {
    requires std::is_convertible_v<
                 decltype(obj.data()),
                 const ::flatbuffers::Array<int8_t, MaxSize>*> ||
                 std::is_convertible_v<
                     decltype(obj.data()),
                     const ::flatbuffers::Array<uint8_t, MaxSize>*>;
    requires std::is_unsigned_v<decltype(obj.size())>;
  }
absl::string_view StringView(const T* source) {
  if (source == nullptr) {
    return absl::string_view();
  }
  const size_t length =
      std::min<size_t>(source->size(), static_cast<size_t>(MaxSize));
  return absl::string_view(
      reinterpret_cast<const char*>(source->data()->data()), length);
}

}  // namespace intrinsic_fbs

#endif  // INTRINSIC_ICON_FLATBUFFERS_FIXED_STRING_H_
