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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_PROTOPATH_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_PROTOPATH_H_

#include <ostream>
#include <variant>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace intrinsic::executive::clips {

inline constexpr absl::string_view kProtoPathSeparator = ".";
inline constexpr absl::string_view kProtoPathArrayOpeningBracket = "[";
inline constexpr absl::string_view kProtoPathArrayClosingBracket = "]";
inline constexpr absl::string_view kProtoPathCustomArrayAppendIndex = "*";

// This represents a field index, it can be:
// absent: no index given
// add: the addition operation [*] was used
// int: a specific index number was given (e.g. [3], [-5])
class IndexVariant {
 public:
  IndexVariant() = delete;

  static IndexVariant CreateAbsent() { return IndexVariant(std::monostate()); }
  static IndexVariant CreateAdd() { return IndexVariant(true); }
  static IndexVariant CreateInt(int index) { return IndexVariant(index); }

  bool IsAbsent() const {
    return std::holds_alternative<std::monostate>(index_);
  }

  bool IsPresent() const { return !IsAbsent(); }

  bool IsAdd() const { return std::holds_alternative<bool>(index_); }
  bool IsInt() const { return std::holds_alternative<int>(index_); }

  int operator*() const { return std::get<int>(index_); }

  bool operator==(const IndexVariant& other) const = default;

 private:
  explicit IndexVariant(std::variant<std::monostate, bool, int> index)
      : index_(index) {}

  std::variant<std::monostate, bool, int> index_;
};

std::ostream& operator<<(std::ostream& os, const IndexVariant& index);

// Checks if the given proto field path is well-formed (it contains only valid
// characters and operators etc.), otherwise returns an error. Valid field paths
// are, e.g., "foo", "foo.bar", "foo[3].bar", "foo.bar[*]".
//
// Note that this check is not overly strict so that we can give better error
// messages later when semantically interpreting the field path. E.g., this
// function will not complain about "foo[*].bar" even though we currently only
// support "[*]" in the last path element and for write access.
//
// path_context is either empty or a parenthesised string that is inserted into
// error messages describing in which context this path is used, e.g. " (path
// specified to assign expression 'foo')"
absl::Status CheckIsValidFieldPath(absl::string_view field_path,
                                   absl::string_view path_context);

// The result of a successful call to GetFieldFromPath() below.
struct FieldFromPath {
  const google::protobuf::Message* msg;
  const google::protobuf::FieldDescriptor* descriptor;
  IndexVariant index;
};

// GetFieldFromPath() / GetMutableFieldFromPath() resolves the given
// 'field_path' within the given 'msg' and returns the message, field and index
// corresponding to the last element of the given 'field_path'.
//
// Examples (where FD = FieldDescriptor):
//   msg, "foo"           -> msg,          FD for "foo", <no index>    (1)
//   msg, "foo.bar"       -> &msg->foo(),  FD for "bar", <no index>    (1)
//   msg, "foo[3].bar"    -> &msg->foo(3), FD for "bar", <no index>    (1,2)
//   msg, "foo.bar[3]"    -> &msg->foo(),  FD for "bar", [3]           (1,2)
//   msg, "foo.bar[*]"    -> &msg->foo(),  FD for "bar", [*]           (1,3)
//
// (1) When using GetMutableFieldFromPath(), a mutable message pointer is
//     returned (msg->mutable_foo() instead of &msg->foo()).
// (2) Negative indices are also allowed. [-1] is the last element, [-2] is the
//     second to last etc.
// (3) When using GetMutableFieldFromPath(), [*] can be used in the *last* path
//     element to "append to a repeated field". Note that this function will
//     simply return an [*] index object and will NOT mutate the repeated field.
//     The appending has to be done by the caller.
//
// If a sub-field of an unset message field is referenced by the given field
// path (e.g., 'field_path' is "foo.bar" and "foo" is unset in 'msg'):
// - GetFieldFromPath(): A not found error is returned. This behavior is
//   different from using msg->foo().
// - GetMutableFieldFromPath(): The unset message field is initialized with an
//   empty value ('msg' gets mutated). This behavior is equivalent to using
//   msg->mutable_foo().
//
// If 'allow_index' is set to false, index expressions will raise an error if
// used *in the last* element of the given 'field_path'. Index expressions are
// always allowed and required for intermediate path elements which reference
// repeated fields.
//
// path_context is either empty or a parenthesised string that is inserted into
// error messages describing in which context this path is used, e.g. " (path
// specified to assign expression 'foo')"
absl::StatusOr<FieldFromPath> GetFieldFromPath(
    const google::protobuf::Message* msg, absl::string_view field_path,
    bool allow_index = true, absl::string_view path_context = "");

// The result of a successful call to GetMutableFieldFromPath() below.
struct MutableFieldFromPath {
  google::protobuf::Message* msg;
  const google::protobuf::FieldDescriptor* descriptor;
  IndexVariant index;
};

// Mutable version of GetFieldFromPath(). See GetFieldFromPath() for details.
absl::StatusOr<MutableFieldFromPath> GetMutableFieldFromPath(
    google::protobuf::Message* msg, absl::string_view field_path,
    bool allow_index = true, absl::string_view path_context = "");

}  // namespace intrinsic::executive::clips

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_PROTOPATH_H_
