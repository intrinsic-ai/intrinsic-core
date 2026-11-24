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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_VALUE_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_VALUE_H_

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace intrinsic {
namespace executive {
namespace clips {

class Symbol;

class Value {
 public:
  // Type specifier, numbers from clips/constant.h.
  // Must be in sync since we pass the value to CLIPS in some cases.
  enum class Type {
    kUnknown = -1,
    kFloat = 0,
    kInteger = 1,
    kSymbol = 2,
    kString = 3,
    kExternalAddress = 5
  };

  explicit Value();
  explicit Value(Type type);
  explicit Value(float value);
  explicit Value(double value);
  explicit Value(int value);
  explicit Value(unsigned int value);
  explicit Value(int64_t value);
  explicit Value(std::string_view value, Type type = Type::kString);
  explicit Value(void* pointer);
  explicit Value(const Symbol& symbol);
  Value(const Value& other);

  Value& operator=(const Value& other);
  bool operator==(const Value& other) const;
  bool operator!=(const Value& other) const;
  bool operator==(const Symbol& sym) const;
  bool operator!=(const Symbol& sym) const;

  // Get type of value. Cannot name as GetType() due to conflict
  // with macro in CLIPS.
  Type GetValueType() const { return type_; }

  void Set(float value);
  void Set(double value);
  void Set(int value);
  void Set(unsigned int value);
  void Set(int64_t value);
  void Set(absl::string_view value, Type type = Type::kString);
  void Set(void* pointer);
  void Set(const Value& other);

  absl::StatusOr<float> GetFloat() const;
  absl::StatusOr<double> GetDouble() const;
  absl::StatusOr<int> GetInt() const;
  absl::StatusOr<unsigned int> GetUInt() const;
  absl::StatusOr<int64_t> GetLong() const;
  absl::StatusOr<int64_t> GetInteger() const { return GetLong(); }
  absl::StatusOr<std::string> GetString() const;
  absl::StatusOr<Symbol> GetSymbol() const;
  absl::StatusOr<std::string> GetSymbolAsString() const;
  absl::StatusOr<std::string> GetStringOrSymbol() const;
  absl::StatusOr<void*> GetPointer() const;

  std::string ToString(bool quote_strings = false) const;

  static std::string TypeToString(Type type);

 private:
  Type type_;
  std::string string_value_;
  int64_t integer_value_;
  double double_value_;
  void* pointer_;
};

// Class to represent symbols explicitly. It is particularly handy to accept or
// return symbols explicitly in user-defined CLIPS functions.
class Symbol {
 public:
  Symbol();
  explicit Symbol(std::string_view symbol)
      : value_(symbol, Value::Type::kSymbol) {}

  void Set(absl::string_view symbol) {
    value_.Set(symbol, Value::Type::kSymbol);
  }

  // We allow implicit conversion to Value, because every Symbol is a Value (and
  // thus conversion is always safe).  We do not make this an explicit
  // inheritance because then a Symbol could be inadvertently changed to, e.g.,
  // a string through Value's Set methods. Implicit conversion enables simple
  // construction of a Values vector, for example.
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator const Value&() const { return value_; }

  explicit operator std::string() const { return ToString(); }
  bool operator==(const Symbol& other) const {
    return other.ToString() == ToString();
  }
  bool operator!=(const Symbol& other) const { return !(*this == other); }

  std::string ToString() const { return value_.ToString(); }

  static const Symbol& True();
  static const Symbol& False();
  static const Symbol& Nil();

 private:
  Value value_;
};

using Values = std::vector<Value>;
using ValueOrValues = std::variant<Value, Values>;

std::ostream& operator<<(std::ostream& os, const Value& value);
std::ostream& operator<<(std::ostream& os, const Values& values);
std::ostream& operator<<(std::ostream& os, const Symbol& symbol);

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_VALUE_H_
