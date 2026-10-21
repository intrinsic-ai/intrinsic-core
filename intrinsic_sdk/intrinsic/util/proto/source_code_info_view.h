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

#ifndef INTRINSIC_UTIL_PROTO_SOURCE_CODE_INFO_VIEW_H_
#define INTRINSIC_UTIL_PROTO_SOURCE_CODE_INFO_VIEW_H_

#include <memory>
#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/map.h"

namespace intrinsic {

// Provides convenient access to `source_code_info` from a
// google::protobuf::FileDescriptorSet.
class SourceCodeInfoView {
 public:
  SourceCodeInfoView() = default;

  // SourceCodeInfoView is move-only.
  SourceCodeInfoView(SourceCodeInfoView&& other) = default;
  SourceCodeInfoView& operator=(SourceCodeInfoView&& other) = default;

  // Initializes this with the given `file_descriptor_set`.
  //
  // Allows for files in the file_descriptor_set to be missing source_code_info.
  // When inspecting the SourceCodeInfoView via Get*() methods, the user will
  // receive an error on if the descriptor being inspected does not have source
  // code info associated with it.
  //
  // Returns an InvalidArgument error if the `file_descriptor_set` contains
  // multiple FileDescriptorProtos with the same filename.
  absl::Status Init(
      const google::protobuf::FileDescriptorSet& file_descriptor_set);

  // Retrieves all field comments and message comments of the given message and
  // all of its nested submessages. They keys of the map are the full name of
  // the message or field that the comment applies to, the value is the comment.
  absl::StatusOr<google::protobuf::Map<std::string, std::string>>
  GetNestedFieldCommentMap(absl::string_view message_name);

 private:
  bool MaybeCollectComment(
      const google::protobuf::Descriptor* message,
      google::protobuf::Map<std::string, std::string>& comment_map,
      absl::flat_hash_set<std::string>& visited) const;
  bool MaybeCollectComment(
      const google::protobuf::FieldDescriptor* field,
      google::protobuf::Map<std::string, std::string>& comment_map,
      absl::flat_hash_set<std::string>& visited) const;
  bool MaybeCollectComment(
      const google::protobuf::ServiceDescriptor* service,
      google::protobuf::Map<std::string, std::string>& comment_map,
      absl::flat_hash_set<std::string>& visited) const;
  bool MaybeCollectComment(
      const google::protobuf::MethodDescriptor* method,
      google::protobuf::Map<std::string, std::string>& comment_map,
      absl::flat_hash_set<std::string>& visited) const;

  absl::Status GetNestedFieldCommentMap(
      const google::protobuf::Descriptor* message,
      google::protobuf::Map<std::string, std::string>& comment_map,
      absl::flat_hash_set<std::string>& visited) const;

  absl::Status GetNestedFieldCommentMap(
      const google::protobuf::ServiceDescriptor* service,
      google::protobuf::Map<std::string, std::string>& comment_map,
      absl::flat_hash_set<std::string>& visited) const;

  struct Pool {
    Pool() : descriptor_pool(&descriptor_database) {}
    google::protobuf::SimpleDescriptorDatabase descriptor_database;
    google::protobuf::DescriptorPool descriptor_pool;
  };

  std::unique_ptr<Pool> pool_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_SOURCE_CODE_INFO_VIEW_H_
