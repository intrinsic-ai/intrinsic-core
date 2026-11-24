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

#include "intrinsic/executive/clips_cpp/protopath.h"

#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "re2/re2.h"

namespace intrinsic::executive::clips {

namespace {

static LazyRE2 kFieldPathExpr = {
    R"(^([a-zA-Z_][a-zA-Z_0-9]*(\[(-?[0-9]+|\*)\])?\.)*)"
    R"([a-zA-Z_][a-zA-Z_0-9]*(\[(-?[0-9]+|\*)\])?$)"};

// Splits the given path element into name and index.
// "foo" -> ("foo", <no index>)
// "foo[3]" -> ("foo", [3])
// "foo[*]" -> ("foo", [*])
absl::StatusOr<std::pair<absl::string_view, IndexVariant>>
SplitPathElementIntoNameAndIndex(absl::string_view path_element) {
  std::string::size_type pos_open =
      path_element.find(kProtoPathArrayOpeningBracket);
  if (pos_open == std::string::npos) {
    // No index operator present
    return std::make_pair(path_element, IndexVariant::CreateAbsent());
  }

  // Parse index expression
  if (path_element.find(kProtoPathArrayClosingBracket, pos_open + 1) !=
      path_element.length() - 1) {
    return CreateStatus(
        15800,
        absl::StrFormat(
            "Failed to parse field path element '%s', expected '%s'",
            path_element, kProtoPathArrayClosingBracket),
        absl::StatusCode::kInvalidArgument);
  }
  absl::string_view name = path_element.substr(0, pos_open);
  absl::string_view index_str =
      path_element.substr(pos_open + 1, path_element.length() - pos_open - 2);
  int index_num;
  if (index_str == kProtoPathCustomArrayAppendIndex) {
    return std::make_pair(name, IndexVariant::CreateAdd());
  }
  if (!absl::SimpleAtoi(index_str, &index_num)) {
    return CreateStatus(
        15800,
        absl::StrFormat(
            "Failed to parse field path elemen '%s', invalid index '%s'",
            path_element, index_str),
        absl::StatusCode::kInvalidArgument);
  }
  return std::make_pair(name, IndexVariant::CreateInt(index_num));
}

absl::Status CheckIndexForField(IndexVariant index,
                                const google::protobuf::FieldDescriptor* field,
                                bool is_last_path_element, bool allow_index,
                                bool read_access, std::string_view field_path,
                                int path_element_index,
                                std::string_view path_source_annotation) {
  if (field->is_map()) {
    // Map field: only allowed as last path element without index expression.
    if (!is_last_path_element) {
      return CreateStatus(
          15808,
          absl::StrFormat(
              "Intermediate field '%s' referenced in part %d of field path "
              "'%s'%s is a map field",
              field->name(), path_element_index, field_path,
              path_source_annotation),
          absl::StatusCode::kUnimplemented);
    } else if (index.IsPresent()) {
      return CreateStatus(
          15808,
          absl::StrFormat(
              "Field '%s' referenced in part %d of field path '%s'%s "
              "is a map field and does not support index expressions",
              field->name(), path_element_index, field_path,
              path_source_annotation),
          absl::StatusCode::kUnimplemented);
    }
  } else if (field->is_repeated()) {
    if (!is_last_path_element) {
      // Repeated field, intermediate path element:
      // Fixed index expression is required.
      if (index.IsAbsent()) {
        return CreateStatus(
            15805,
            absl::StrFormat(
                "Intermediate field '%s' referenced in part %d of "
                "field path '%s'%s is repeated, expected index expression",
                field->name(), path_element_index, field_path,
                path_source_annotation),
            absl::StatusCode::kInvalidArgument);
      } else if (index.IsAdd()) {
        return CreateStatus(
            15805,
            absl::StrFormat(
                "[*] is not supported for repeated intermediate field '%s' "
                "referenced in part %d of field path '%s'%s",
                field->name(), path_element_index, field_path,
                path_source_annotation),
            absl::StatusCode::kInvalidArgument);
      }
    } else {
      // Repeated field, last path element:
      // Empty index expression is always allowed. An index expression is
      // allowed depending index type, read/write mode and caller-context
      // ('allow_index').
      if (index.IsPresent()) {
        if (!allow_index) {
          return CreateStatus(
              15807,
              absl::StrFormat("Field '%s' referenced in part %d of field path "
                              "'%s'%s has index expression which is not "
                              "allowed in this context",
                              field->name(), path_element_index, field_path,
                              path_source_annotation),
              absl::StatusCode::kInvalidArgument);
        }
        if (index.IsAdd() && read_access) {
          return CreateStatus(
              15807,
              absl::StrFormat("Field '%s' referenced in part %d of field path "
                              "'%s'%s has [*] operator which is not allowed in "
                              "read contexts",
                              field->name(), path_element_index, field_path,
                              path_source_annotation),
              absl::StatusCode::kInvalidArgument);
        }
      }
    }
  } else {
    // Singular field, intermediate or last path element:
    // Index expressions are not allowed.
    if (index.IsPresent()) {
      return CreateStatus(
          15806,
          absl::StrFormat(
              "Field '%s' referenced in part %d of field path '%s'%s is "
              "singular, index expression is not allowed",
              field->name(), path_element_index, field_path,
              path_source_annotation),
          absl::StatusCode::kInvalidArgument);
    }
  }

  return absl::OkStatus();
}

template <typename MessageT,
          typename = std::enable_if_t<
              std::is_same_v<MessageT, const google::protobuf::Message> ||
              std::is_same_v<MessageT, google::protobuf::Message>>>
absl::StatusOr<std::tuple<MessageT*, const google::protobuf::FieldDescriptor*,
                          IndexVariant>>
GetFieldFromPathImpl(MessageT* msg, absl::string_view field_path,
                     bool allow_index, absl::string_view path_context) {
  INTR_RETURN_IF_ERROR(CheckIsValidFieldPath(field_path, path_context));

  std::vector<absl::string_view> path_elements =
      absl::StrSplit(field_path, kProtoPathSeparator);

  MessageT* current_msg = msg;

  for (int i = 0; i < path_elements.size(); ++i) {
    INTR_ASSIGN_OR_RETURN((auto [field_name, index]),
                          SplitPathElementIntoNameAndIndex(path_elements[i]));
    bool is_last_path_element = (i == path_elements.size() - 1);

    const google::protobuf::FieldDescriptor* field =
        current_msg->GetDescriptor()->FindFieldByName(field_name);
    if (field == nullptr) {
      return CreateStatus(
          15801,
          absl::StrFormat("Field '%s' is not defined in message '%s', but is "
                          "referenced in part %d of field path '%s'%s",
                          field_name, current_msg->GetDescriptor()->full_name(),
                          i, field_path, path_context),
          absl::StatusCode::kInvalidArgument);
    }

    INTR_RETURN_IF_ERROR(CheckIndexForField(
        index, field, is_last_path_element, allow_index,
        /*read_access=*/
        std::is_same_v<MessageT, const google::protobuf::Message>, field_path,
        i, path_context));

    // Perform index out of bounds check here so that it is done both for
    // intermediate and last path elements.
    int positive_index = -1;
    if (field->is_repeated() && index.IsInt()) {
      int field_size =
          current_msg->GetReflection()->FieldSize(*current_msg, field);
      if (*index < -field_size || *index >= field_size) {
        return CreateStatus(
            15802,
            absl::StrFormat(
                "Failed to access field '%s' referenced in part %d of field "
                "path '%s'%s: index %d out of bounds [%d, %d] (actual size of "
                "field: %d)",
                path_elements[i], i, field_path, path_context, *index,
                -field_size, field_size - 1, field_size),
            absl::StatusCode::kOutOfRange);
      }
      positive_index = *index < 0 ? field_size + *index : *index;
    }

    // We are done if we reached the last path element. 'current_msg' is the
    // message containing the 'field' corresponding to the last path element.
    if (is_last_path_element) {
      return std::make_tuple(current_msg, field, index);
    }

    // In case of an intermediate path element: descend into corresponding sub
    // message and then continue with next path element. This is only possible
    // if 'field' has message type ("x" in "x.y" must be a message field).
    if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      return CreateStatus(
          15803,
          absl::StrFormat(
              "Intermediate field '%s' referenced in part %d of field path "
              "'%s'%s is of type '%s', expected a message type",
              field_name, i, field_path, path_context, field->type_name()),
          absl::StatusCode::kInvalidArgument);
    }

    if (!field->is_repeated()) {
      // Descend into singular message field
      if constexpr (std::is_same_v<MessageT, const google::protobuf::Message>) {
        if (!current_msg->GetReflection()->HasField(*current_msg, field)) {
          return CreateStatus(
              15804,
              absl::StrFormat("Intermediate field '%s' referenced in part %d "
                              "of field path '%s'%s is not set in message",
                              field_name, i, field_path, path_context),
              absl::StatusCode::kNotFound);
        }

        current_msg =
            &current_msg->GetReflection()->GetMessage(*current_msg, field);
      } else {
        // This initializes the field with an empty message if it is not set,
        // 'current_msg' might get modified.
        current_msg =
            current_msg->GetReflection()->MutableMessage(current_msg, field);
      }
    } else {
      // Descend into repeated message field
      if constexpr (std::is_same_v<MessageT, const google::protobuf::Message>) {
        current_msg = &current_msg->GetReflection()->GetRepeatedMessage(
            *current_msg, field, positive_index);
      } else {
        // In contrast to the singular field case above, 'current_msg' can not
        // get modified here.
        current_msg = current_msg->GetReflection()->MutableRepeatedMessage(
            current_msg, field, positive_index);
      }
    }
  }
  return CreateStatus(15809,
                      absl::StrFormat("This code should be unreachable "
                                      "(field_path: '%s', path_context: '%s')",
                                      field_path, path_context),
                      absl::StatusCode::kInternal);
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const IndexVariant& index) {
  if (index.IsAbsent()) {
    return os << "<no index>";
  } else if (index.IsAdd()) {
    return os << "[*]";
  } else {
    return os << "[" << *index << "]";
  }
}

absl::Status CheckIsValidFieldPath(absl::string_view field_path,
                                   absl::string_view path_context) {
  if (field_path.empty()) {
    return CreateStatus(
        15800, absl::StrFormat("Field path%s must not be empty", path_context),
        absl::StatusCode::kInvalidArgument);
  }
  if (!RE2::FullMatch(field_path, *kFieldPathExpr)) {
    return CreateStatus(15800,
                        absl::StrFormat("Field path '%s'%s is invalid",
                                        field_path, path_context),
                        absl::StatusCode::kInvalidArgument);
  }

  return absl::OkStatus();
}

absl::StatusOr<FieldFromPath> GetFieldFromPath(
    const google::protobuf::Message* msg, absl::string_view field_path,
    bool allow_index, absl::string_view path_context) {
  INTR_ASSIGN_OR_RETURN((auto [field_msg, field_descriptor, index]),
                        GetFieldFromPathImpl<const google::protobuf::Message>(
                            msg, field_path, allow_index, path_context));
  return FieldFromPath{
      .msg = field_msg, .descriptor = field_descriptor, .index = index};
}

absl::StatusOr<MutableFieldFromPath> GetMutableFieldFromPath(
    google::protobuf::Message* msg, absl::string_view field_path,
    bool allow_index, absl::string_view path_context) {
  INTR_ASSIGN_OR_RETURN((auto [field_msg, field_descriptor, index]),
                        GetFieldFromPathImpl<google::protobuf::Message>(
                            msg, field_path, allow_index, path_context));
  return MutableFieldFromPath{
      .msg = field_msg, .descriptor = field_descriptor, .index = index};
}

}  // namespace intrinsic::executive::clips
