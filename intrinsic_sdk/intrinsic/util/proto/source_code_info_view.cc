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

#include "intrinsic/util/proto/source_code_info_view.h"

#include <algorithm>
#include <memory>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/map.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

absl::Status SourceCodeInfoView::Init(
    const google::protobuf::FileDescriptorSet& file_descriptor_set) {
  pool_ = std::make_unique<Pool>();
  if (!std::all_of(
          file_descriptor_set.file().begin(), file_descriptor_set.file().end(),
          [pool =
               pool_.get()](const google::protobuf::FileDescriptorProto& file) {
            return pool->descriptor_database.Add(file);
          })) {
    pool_.reset();
    return absl::InvalidArgumentError(
        "`file_descriptor_set` contains duplicate files.");
  }

  return absl::OkStatus();
}

absl::StatusOr<google::protobuf::Map<std::string, std::string>>
SourceCodeInfoView::GetNestedFieldCommentMap(absl::string_view message_name) {
  if (pool_ == nullptr) {
    return absl::FailedPreconditionError("SourceCodeInfoView not Init()ed.");
  }
  google::protobuf::Map<std::string, std::string> comment_map;
  absl::flat_hash_set<std::string> visited;

  const google::protobuf::Descriptor* const message =
      pool_->descriptor_pool.FindMessageTypeByName(std::string(message_name));
  if (message != nullptr) {
    INTR_RETURN_IF_ERROR(
        GetNestedFieldCommentMap(message, comment_map, visited));
    return comment_map;
  }
  const google::protobuf::ServiceDescriptor* const service =
      pool_->descriptor_pool.FindServiceByName(std::string(message_name));
  if (service != nullptr) {
    INTR_RETURN_IF_ERROR(
        GetNestedFieldCommentMap(service, comment_map, visited));
    return comment_map;
  }
  return absl::NotFoundError(
      absl::StrCat("Message does not exist with: ", message_name));
}

bool SourceCodeInfoView::MaybeCollectComment(
    const google::protobuf::Descriptor* message,
    google::protobuf::Map<std::string, std::string>& comment_map,
    absl::flat_hash_set<std::string>& visited) const {
  absl::string_view name = message->full_name();
  if (!visited.contains(name)) {
    google::protobuf::SourceLocation source_location;
    if (message->GetSourceLocation(&source_location)) {
      comment_map.emplace(name, source_location.leading_comments);
    }
    visited.emplace(name);
    return true;
  }
  return false;
}

bool SourceCodeInfoView::MaybeCollectComment(
    const google::protobuf::FieldDescriptor* field,
    google::protobuf::Map<std::string, std::string>& comment_map,
    absl::flat_hash_set<std::string>& visited) const {
  absl::string_view name = field->full_name();
  if (!visited.contains(name)) {
    google::protobuf::SourceLocation source_location;
    if (field->GetSourceLocation(&source_location)) {
      comment_map.emplace(name, source_location.leading_comments);
    }
    visited.emplace(name);
    return true;
  }
  return false;
}

bool SourceCodeInfoView::MaybeCollectComment(
    const google::protobuf::ServiceDescriptor* service,
    google::protobuf::Map<std::string, std::string>& comment_map,
    absl::flat_hash_set<std::string>& visited) const {
  absl::string_view name = service->full_name();
  if (!visited.contains(name)) {
    google::protobuf::SourceLocation source_location;
    if (service->GetSourceLocation(&source_location)) {
      comment_map.emplace(name, source_location.leading_comments);
    }
    visited.emplace(name);
    return true;
  }
  return false;
}

bool SourceCodeInfoView::MaybeCollectComment(
    const google::protobuf::MethodDescriptor* method,
    google::protobuf::Map<std::string, std::string>& comment_map,
    absl::flat_hash_set<std::string>& visited) const {
  absl::string_view name = method->full_name();
  if (!visited.contains(name)) {
    google::protobuf::SourceLocation source_location;
    if (method->GetSourceLocation(&source_location)) {
      comment_map.emplace(name, source_location.leading_comments);
    }
    visited.emplace(name);
    return true;
  }
  return false;
}

absl::Status SourceCodeInfoView::GetNestedFieldCommentMap(
    const google::protobuf::Descriptor* message,
    google::protobuf::Map<std::string, std::string>& comment_map,
    absl::flat_hash_set<std::string>& visited) const {
  MaybeCollectComment(message, comment_map, visited);

  for (int field_index = 0; field_index < message->field_count();
       ++field_index) {
    const google::protobuf::FieldDescriptor* field =
        message->field(field_index);
    // Add leading comments for this message field.
    MaybeCollectComment(field, comment_map, visited);

    // Recursively process the message type of the field.
    const google::protobuf::FieldDescriptor* field_to_recursively_process =
        message->field(field_index);
    if (field->is_map()) {
      // If the field is a map, we'll recursively process the value only. The
      // key should always be a primitive type.
      const google::protobuf::Descriptor* msg_descriptor =
          field->message_type();
      field_to_recursively_process = msg_descriptor->map_value();
    }
    if (field_to_recursively_process->cpp_type() ==
        google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      const google::protobuf::Descriptor* msg_descriptor =
          field_to_recursively_process->message_type();
      // Recursively process the field's message type.
      if (MaybeCollectComment(msg_descriptor, comment_map, visited)) {
        INTR_RETURN_IF_ERROR(
            GetNestedFieldCommentMap(msg_descriptor, comment_map, visited));
      }
    }
  }

  return absl::OkStatus();
}

absl::Status SourceCodeInfoView::GetNestedFieldCommentMap(
    const google::protobuf::ServiceDescriptor* service,
    google::protobuf::Map<std::string, std::string>& comment_map,
    absl::flat_hash_set<std::string>& visited) const {
  MaybeCollectComment(service, comment_map, visited);

  for (int method_index = 0; method_index < service->method_count();
       ++method_index) {
    const google::protobuf::MethodDescriptor* method =
        service->method(method_index);
    // Add leading comments for this method.
    MaybeCollectComment(method, comment_map, visited);

    // Recursively process the service's input message type.
    const google::protobuf::Descriptor* input_type = method->input_type();
    if (MaybeCollectComment(input_type, comment_map, visited)) {
      INTR_RETURN_IF_ERROR(
          GetNestedFieldCommentMap(input_type, comment_map, visited));
    }
    // Recursively process the service's output message type.
    const google::protobuf::Descriptor* output_type = method->output_type();
    if (MaybeCollectComment(output_type, comment_map, visited)) {
      INTR_RETURN_IF_ERROR(
          GetNestedFieldCommentMap(output_type, comment_map, visited));
    }
  }
  return absl::OkStatus();
}

}  // namespace intrinsic
