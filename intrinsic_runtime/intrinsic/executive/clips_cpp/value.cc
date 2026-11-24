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

#include "intrinsic/executive/clips_cpp/value.h"

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace intrinsic {
namespace executive {
namespace clips {

Value::Value() : type_(Type::kUnknown) {}
Value::Value(Type type) : type_(type) {}
Value::Value(float value) : type_(Type::kFloat), double_value_(value) {}
Value::Value(double value) : type_(Type::kFloat), double_value_(value) {}
Value::Value(int value) : type_(Type::kInteger), integer_value_(value) {}
Value::Value(unsigned int value)
    : type_(Type::kInteger), integer_value_(value) {}
Value::Value(int64_t value) : type_(Type::kInteger), integer_value_(value) {}
Value::Value(std::string_view value, Type type)
    : type_(type), string_value_(value) {}
Value::Value(void* pointer)
    : type_(Type::kExternalAddress), pointer_(pointer) {}
Value::Value(const Symbol& symbol)
    : type_(Type::kSymbol), string_value_(symbol.ToString()) {}
Value::Value(const Value& other) { *this = other; }

Value& Value::operator=(const Value& other) {
  type_ = other.type_;
  switch (type_) {
    case Type::kInteger:
      integer_value_ = other.integer_value_;
      break;
    case Type::kFloat:
      double_value_ = other.double_value_;
      break;
    case Type::kString:
      [[fallthrough]];
    case Type::kSymbol:
      string_value_ = other.string_value_;
      break;
    case Type::kExternalAddress:
      pointer_ = other.pointer_;
      break;
    default: {
    };  // nothing, it's unknown
  }
  return *this;
}

bool Value::operator==(const Value& other) const {
  if (type_ != other.type_) {
    LOG(WARNING) << "Value comparison failed due to type mismatch: "
                 << TypeToString(type_) << " vs " << TypeToString(other.type_)
                 << " (when comparing values: " << this->ToString() << " vs "
                 << other.ToString() << ")";
    return false;
  }
  switch (type_) {
    case Type::kInteger:
      return integer_value_ == other.integer_value_;
    case Type::kFloat:
      return double_value_ == other.double_value_;
    case Type::kString:
      [[fallthrough]];
    case Type::kSymbol:
      return string_value_ == other.string_value_;
    case Type::kExternalAddress:
      return pointer_ == other.pointer_;
    case Type::kUnknown:
      return false;
  }
}

bool Value::operator!=(const Value& other) const { return !(*this == other); }

bool Value::operator==(const Symbol& sym) const {
  if (type_ != Type::kSymbol) {
    LOG(WARNING)
        << "Value comparison to Symbol failed due to type mismatch, got: "
        << TypeToString(type_);
    return false;
  }
  return string_value_ == sym.ToString();
}

bool Value::operator!=(const Symbol& sym) const { return !(*this == sym); }

void Value::Set(float value) {
  type_ = Type::kFloat;
  double_value_ = value;
}

void Value::Set(double value) {
  type_ = Type::kFloat;
  double_value_ = value;
}
void Value::Set(int value) {
  type_ = Type::kInteger;
  integer_value_ = value;
}

void Value::Set(unsigned int value) {
  type_ = Type::kInteger;
  integer_value_ = value;
}

void Value::Set(int64_t value) {
  type_ = Type::kInteger;
  integer_value_ = value;
}

void Value::Set(absl::string_view value, Type type) {
  type_ = type;
  string_value_ = value;
}

void Value::Set(void* pointer) {
  type_ = Type::kExternalAddress;
  pointer_ = pointer;
}

void Value::Set(const Value& other) { *this = other; }

absl::StatusOr<float> Value::GetFloat() const {
  if (type_ != Type::kFloat) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Value is not a float (but %s)", TypeToString(type_)));
  }
  return static_cast<float>(double_value_);
}

absl::StatusOr<double> Value::GetDouble() const {
  if (type_ != Type::kFloat) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Value is not a float (but %s)", TypeToString(type_)));
  }
  return double_value_;
}

absl::StatusOr<int> Value::GetInt() const {
  if (type_ != Type::kInteger) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Value is not an integer (but %s)", TypeToString(type_)));
  }
  return static_cast<int>(integer_value_);
}

absl::StatusOr<unsigned int> Value::GetUInt() const {
  if (type_ != Type::kInteger) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Value is not an integer (but %s)", TypeToString(type_)));
  }
  return static_cast<unsigned int>(integer_value_);
}

absl::StatusOr<int64_t> Value::GetLong() const {
  if (type_ != Type::kInteger) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Value is not an integer (but %s)", TypeToString(type_)));
  }
  return integer_value_;
}

absl::StatusOr<std::string> Value::GetString() const {
  if (type_ != Type::kString) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Value is not a string (but %s)", TypeToString(type_)));
  }
  return string_value_;
}

absl::StatusOr<Symbol> Value::GetSymbol() const {
  if (type_ != Type::kSymbol) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Value is not a symbol (but %s)", TypeToString(type_)));
  }
  return Symbol(string_value_);
}

absl::StatusOr<std::string> Value::GetSymbolAsString() const {
  if (type_ != Type::kSymbol) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Value is not a symbol (but %s)", TypeToString(type_)));
  }
  return string_value_;
}

absl::StatusOr<std::string> Value::GetStringOrSymbol() const {
  if (type_ != Type::kString && type_ != Type::kSymbol) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Value is neither string nor symbol (but %s)", TypeToString(type_)));
  }
  return string_value_;
}

absl::StatusOr<void*> Value::GetPointer() const {
  if (type_ != Type::kExternalAddress) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Value is not an external address (but %s)", TypeToString(type_)));
  }
  return pointer_;
}

std::string Value::ToString(bool quote_strings) const {
  switch (type_) {
    case Type::kUnknown:
      return "";
    case Type::kFloat:
      return absl::StrCat(double_value_);
    case Type::kInteger:
      return absl::StrCat(integer_value_);
    case Type::kSymbol:
      return string_value_;
    case Type::kString:
      if (quote_strings) {
        return absl::StrFormat(R"("%s")", string_value_);
      }
      return string_value_;
    case Type::kExternalAddress:
      return absl::StrFormat("%p", pointer_);
  }
}

std::string Value::TypeToString(Type type) {
  switch (type) {
    case Type::kUnknown:
      return "unknown";
    case Type::kFloat:
      return "float";
    case Type::kInteger:
      return "integer";
    case Type::kSymbol:
      return "symbol";
    case Type::kString:
      return "string";
    case Type::kExternalAddress:
      return "externaladdress";
  }
}

Symbol::Symbol() : value_(Symbol::Nil()) {}

const Symbol& Symbol::True() {
  static absl::NoDestructor<Symbol> true_sym("TRUE");
  return *true_sym;
}

const Symbol& Symbol::False() {
  static absl::NoDestructor<Symbol> false_sym("FALSE");
  return *false_sym;
}

const Symbol& Symbol::Nil() {
  static absl::NoDestructor<Symbol> nil_sym("nil");
  return *nil_sym;
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  return os << value.ToString(/*quote_strings=*/true);
}

std::ostream& operator<<(std::ostream& os, const Values& values) {
  bool first = true;
  os << "[";
  for (const Value& value : values) {
    if (!first) {
      os << ", ";
    }
    os << value.ToString(/*quote_strings=*/true);
    first = false;
  }
  return os << "]";
}

std::ostream& operator<<(std::ostream& os, const Symbol& symbol) {
  return os << symbol.ToString();
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
