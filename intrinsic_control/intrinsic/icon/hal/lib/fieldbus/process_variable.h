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

#ifndef INTRINSIC_ICON_HAL_LIB_FIELDBUS_PROCESS_VARIABLE_H_
#define INTRINSIC_ICON_HAL_LIB_FIELDBUS_PROCESS_VARIABLE_H_

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_or.h"

namespace intrinsic::fieldbus {

// Facilitates read and write access to a variable in memory, like the process
// image of a bus. Supports arithmetic types as well as bit-arrays/bitsets of
// with N (1 <= N <= 64) bits. Assumes little endian memory layout.
class ProcessVariable {
 public:
  // The type of the variable in memory.
  enum Type { kSignedIntegral, kUnsignedIntegral, kFloat, kDouble, kUnknown };

  // Checks whether T is a specialization of std::bitset.
  template <typename T>
  struct is_bitset : std::false_type {};

  template <std::size_t N>
  struct is_bitset<std::bitset<N>> : std::true_type {};

  template <typename T>
  static constexpr std::size_t bitsizeof() {
    if constexpr (is_bitset<T>::value) {
      return T().size();
    } else if constexpr (std::is_same_v<T, bool>) {
      return 1;
    } else {
      return sizeof(T) * 8;
    }
  }

  // Constructor.
  // `data` points to the byte location in memory where the variable starts.
  // The variable is allowed to be shifted by `bit_offset`, which might make it
  // occupy more bytes than the original type. `bit_offset` must be less than 8!
  // `type` specifies the arithmetic type. For bit- or boolean-arrays use
  // kUnsignedIntegral. `bit_size` holds the size of the variable in bits.
  explicit ProcessVariable(uint8_t* data, Type type, std::size_t bit_size,
                           uint8_t bit_offset = 0);

  // Checks whether the internal type (as specified in the constructor) and T
  // are compatible, i.e. the internal type can be written from a value of type
  // T and we can read/convert the internal value into a variable of type T.
  // If `ignore_signedness` is true, then signedness of integral types is
  // ignored and both signed and unsigned integrals are considered compatible.
  template <typename T>
  intrinsic::icon::RealtimeStatus IsCompatibleType(
      bool ignore_signedness = false) const {
    return IsCompatibleType<T>(type_, bit_size_, ignore_signedness);
  }

  // Checks whether the T and a type defined by `Type` and `bit_size` are
  // compatible, i.e. the internal type can be written from a value of type T
  // and we can read/convert the internal value into a variable of type T.
  // If `ignore_signedness` is true, then signedness of integral types is
  // ignored and both signed and unsigned integrals are considered compatible.
  template <typename T>
  static intrinsic::icon::RealtimeStatus IsCompatibleType(
      Type type, std::size_t bit_size, bool ignore_signedness = false) {
    if (type == kUnknown) {
      return intrinsic::icon::FailedPreconditionError(
          "Internal type is unknown");
    }
    // Bitsizes don't match.
    if (bit_size != bitsizeof<T>()) {
      return intrinsic::icon::InvalidArgumentError(
          intrinsic::icon::RealtimeStatus::StrCat(
              "Bitsize mismatch: ", bit_size, " vs. ", bitsizeof<T>()));
    }
    // Either bitsize is zero.
    if (bit_size == 0 || bitsizeof<T>() == 0) {
      return intrinsic::icon::InvalidArgumentError(
          "Bitsize must be greater than zero.");
    }

    bool is_compatible_unsigned_integral =
        (type == kUnsignedIntegral ||
         (ignore_signedness && type == kSignedIntegral));
    bool is_compatible_signed_integral =
        (type == kSignedIntegral ||
         (ignore_signedness && type == kUnsignedIntegral));

    // T is unsigned integral, but internal type is not.
    if (std::is_integral_v<T> && std::is_unsigned_v<T> &&
        !is_compatible_unsigned_integral) {
      return intrinsic::icon::InvalidArgumentError(
          "External type is unsigned integral, while internal type is NOT "
          " a compatible integral.");
    }

    // T is signed integral, but internal type is not.
    if (std::is_integral_v<T> && std::is_signed_v<T> &&
        !is_compatible_signed_integral) {
      return intrinsic::icon::InvalidArgumentError(
          "External type is signed integral, while internal type is NOT "
          "a compatible integral.");
    }

    // T is floating point, but internal type is not.
    if (std::is_floating_point_v<T> && !(type == kFloat || type == kDouble)) {
      return intrinsic::icon::InvalidArgumentError(
          "External type is floating point, while internal is neither float "
          "nor double.");
    }

    // T is a bitset, but internal type is not unsigned integral.
    if (is_bitset<T>::value && !is_compatible_unsigned_integral) {
      return intrinsic::icon::InvalidArgumentError(
          "External type is a bitset, while internal type is NOT a compatible  "
          "integral.");
    }

    // T is neither integral, floating point or bitset nor enum.
    if (!std::is_integral_v<T> && !std::is_floating_point_v<T> &&
        !is_bitset<T>::value && !std::is_enum_v<T>) {
      return intrinsic::icon::InvalidArgumentError(
          "External type is neither integral, floating point, nor bitset.");
    }
    return intrinsic::icon::OkStatus();
  }

  // Reads the internal variable and returns its bit representation as a
  // uint64_t. Highest significant bits beyond `bit_size()` are set to zero.
  uint64_t ReadRawUnchecked() const {
    // In the extreme case (64bit variable + 7 bit offset) the internal variable
    // will consume 9 bytes. Thus, we always use two uint64_t to represent the
    // variable in memory. We refer to the least significant bits with "low" and
    // to the most significant bits with "high".
    // Example of the memory layout of a 64bit variable (all ones) with a 7 bit
    // offset, assuming the surrounding memory is all 0s:
    // Variables (uint64_t): |    high    |    low     |
    // Binary memory layout: |0...01111111|1...10000000|
    // The value's bits:     |     xxxxxxx|x...x       |
    // The offset bits:      |            |     xxxxxxx|
    //
    // For reading, we read the low and high memory locations, correct for the
    // `bit_offset` by shifting and reconstruct the unshifted original value
    // with a final OR.
    if (bit_size_ == 0) {
      return 0;
    }
    // Make a copy of the affected memory.
    uint64_t low_raw_value = MemCast<uint64_t>(data_, byte_size_);

    // Shift the raw value if it has an offset.
    if (bit_offset_ > 0) {
      // Correct the offset of the `low` bits.
      low_raw_value >>= bit_offset_;

      // Only read the adjacent `high` byte if the variable actually spills past
      // `byte_size_` bytes.
      if (bit_size_ + bit_offset_ > byte_size_ * 8) {
        uint64_t high_raw_value = MemCast<uint64_t>(data_ + byte_size_, 1);
        // Shift these `high` bits back to where they belong and merge.
        low_raw_value |= high_raw_value << (byte_size_ * 8 - bit_offset_);
      }
    }

    // Clear any bits beyond bit_size_.
    const uint64_t bit_size_ones =
        bit_size_ >= 64 ? ~uint64_t{0} : (uint64_t{1} << bit_size_) - 1;
    low_raw_value &= bit_size_ones;
    return low_raw_value;
  }

  // Reads the internal variable and returns its value as a value of type T.
  // Does NOT perform compatibility checks. Use only if you've verified
  // compatibility of internal and external type via `IsCompatibleType` first.
  template <typename T>
  T ReadUnchecked() const {
    if (bit_size_ == 0) {
      return T();
    }
    uint64_t raw_value = ReadRawUnchecked();
    return MemCast<T>(&raw_value, byte_size_);
  }

  // Reads the internal variable and returns its value as a value of type T.
  // Returns an error if the internal and external type are not compatible. As
  // an alternative: Consider checking the type first with `IsCompatibleType`
  // and then use `ReadUnchecked` to avoid repeatedly checking for
  // compatibility.
  template <typename T>
  intrinsic::icon::RealtimeStatusOr<T> Read() const {
    auto compatibility = IsCompatibleType<T>();
    if (!compatibility.ok()) {
      return compatibility;
    }
    return ReadUnchecked<T>();
  }

  // Writes the internal variable from a uint64_t raw bit representation.
  // Highest significant bits beyond `bit_size()` are ignored.
  void WriteRawUnchecked(uint64_t value) {
    // In the worst case (64bit variable + 7 bit offset) the internal variable
    // will consume 9 bytes. Thus, we always use one uint64_t and one uint8_t to
    // represent the variable in memory. We refer to the uint64_t as "low" as it
    // captures the least significant bits of the value. We refer to the uint8_t
    // as "high" since it captures (if bit_offset is > 0) the most significant
    // bits. For writing a value we first clear the bits in the target memory
    // using appropriately shifted masks. Then we calculate the low and high
    // memory locations (considering the actual size of the variable and a
    // potential `bit_offset_`). Finally, we write the low and high memory
    // locations by performing an OR with the target memory locations.

    if (bit_size_ == 0) {
      return;
    }

    // Take a copy of the target memory.
    uint64_t low_raw_value = MemCast<uint64_t>(data_, byte_size_);

    // Clear the target bits.
    const uint64_t bit_size_ones =
        bit_size_ >= 64 ? ~uint64_t{0} : (uint64_t{1} << bit_size_) - 1;

    const uint64_t low_raw_mask = ~(bit_size_ones << bit_offset_);
    low_raw_value &= low_raw_mask;

    // Align the target value.
    const uint64_t target_raw_value = value & bit_size_ones;
    const uint64_t low_target_raw_value = target_raw_value << bit_offset_;

    // Write the low bytes.
    low_raw_value |= low_target_raw_value;
    memcpy(data_, &low_raw_value, byte_size_);

    // Handle the high bits only when the variable spills past `byte_size_`
    // bytes.
    const std::size_t total_bits = bit_size_ + bit_offset_;
    const std::size_t low_capacity_bits = byte_size_ * 8;
    if (total_bits > low_capacity_bits) {
      const std::size_t high_bit_count = total_bits - low_capacity_bits;
      const std::size_t low_bit_count = low_capacity_bits - bit_offset_;

      // Take a copy of the adjacent high byte.
      uint8_t high_raw_value =
          MemCast<uint8_t>(data_ + byte_size_, sizeof(high_raw_value));

      // Clear only the `high_bit_count` least significant bits occupied by the
      // spilled portion of this variable, preserving all higher adjacent bits.
      const uint8_t high_raw_mask =
          static_cast<uint8_t>(~((1u << high_bit_count) - 1u));
      high_raw_value &= high_raw_mask;

      // Align the spilled bits of the target value.
      const uint8_t high_target_raw_value =
          static_cast<uint8_t>(target_raw_value >> low_bit_count);
      high_raw_value |= high_target_raw_value;

      // Write the high byte.
      memcpy(data_ + byte_size_, &high_raw_value, sizeof(high_raw_value));
    }
  }
  // Writes the internal variable from a value of type T.
  // Does NOT perform compatibility checks. Use only, if you've verified
  // compatibility of internal and external type via `IsCompatibleType` first!
  template <typename T>
  void WriteUnchecked(const T& value) {
    // Convert `value` to its raw representation.
    uint64_t target_raw_value = 0;
    memcpy(&target_raw_value, &value, sizeof(value));

    WriteRawUnchecked(target_raw_value);
  }

  // Writes the internal variable from a value of type T.
  // Returns an error if the internal and external type are not compatible. As
  // an alternative: Consider checking the type first with `IsCompatibleType`
  // and then use `WriteUnchecked` to avoid repeatedly checking for
  // compatibility.
  template <typename T>
  intrinsic::icon::RealtimeStatus Write(const T& value) {
    auto compatibility = IsCompatibleType<T>();
    if (!compatibility.ok()) {
      return compatibility;
    }
    WriteUnchecked(value);
    return intrinsic::icon::OkStatus();
  }

  // Returns the size of the internal backing variable in bits.
  std::size_t bit_size() const { return bit_size_; }

 private:
  // Copies `sizeof(CastType)` bytes from the memory at `source` and returns
  // them as a `CastType`. If `CastType` is a specialization of `std::bitset`
  // the conversion will use a `uint64_t` as intermediary, i.e. the returned
  // `std::bitset` will be created from 8 bytes at `source` interpreted as
  // `uint64_t`.
  template <typename CastType, typename DataType>
  static CastType MemCast(const DataType source, std::size_t bytes_to_copy) {
    static_assert(std::is_pointer_v<DataType>);
    static_assert(!std::is_pointer_v<CastType>);

    if (is_bitset<CastType>::value) {
      // Use a `uint64_t` as intermediary, since we cannot simply `memcpy`
      // into a `std::bitset<N>`.
      return CastType(MemCast<uint64_t>(source, bytes_to_copy));
    } else {
      CastType result{0};
      memset(&result, 0, sizeof(CastType));
      memcpy(&result, source, bytes_to_copy);
      return result;
    }
  }

  // Pointer to the byte where the internal variable "starts" with
  // `bit_offset_`.
  uint8_t* data_;
  // Arithmetic type of the internal variable.
  Type type_;
  // Size of the internal variable in bits.
  std::size_t bit_size_;
  // Size of the internal variable in bytes.
  std::size_t byte_size_;
  // Bit offset to the actual "start" of the variable in memory.
  uint8_t bit_offset_;
};

}  // namespace intrinsic::fieldbus

#endif  // INTRINSIC_ICON_HAL_LIB_FIELDBUS_PROCESS_VARIABLE_H_
