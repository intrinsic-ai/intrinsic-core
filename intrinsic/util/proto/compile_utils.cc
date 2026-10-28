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

#include "intrinsic/util/proto/compile_utils.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/compiler/importer.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/io/zero_copy_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

DescriptorPoolErrorCollector::DescriptorPoolErrorCollector()
    : google::protobuf::DescriptorPool::ErrorCollector(),
      return_status_(absl::OkStatus()) {}

void DescriptorPoolErrorCollector::RecordError(
    absl::string_view filename, absl::string_view element_name,
    const google::protobuf::Message* descriptor, ErrorLocation location,
    absl::string_view message) {
  return_status_ = absl::Status(absl::StatusCode::kInvalidArgument, message);
}

class InMemorySourceTree : public google::protobuf::compiler::SourceTree {
 public:
  absl::Status AddFile(std::string_view name, std::string_view content) {
    if (files_.find(name) != files_.end()) {
      return absl::AlreadyExistsError(
          absl::StrFormat("File %s already exists in source tree (standard "
                          "types cannot be overridden)",
                          name));
    }
    files_[name] = absl::Cord(content);
    return absl::OkStatus();
  }

  google::protobuf::io::ZeroCopyInputStream* Open(
      absl::string_view filename) override {
    auto file_it = files_.find(filename);
    if (file_it != files_.end()) {
      return new google::protobuf::io::CordInputStream(&file_it->second);
    }

    return nullptr;
  }

 private:
  absl::flat_hash_map<std::string, absl::Cord> files_;
};

class ErrorCollector
    : public google::protobuf::compiler::MultiFileErrorCollector {
 public:
  void RecordError(absl::string_view file, int line, int col,
                   absl::string_view detail) override {
    if (line > 0) {
      errors_.push_back(absl::StrCat(file, ":", line, ": ", detail));
    } else {
      errors_.push_back(absl::StrCat(file, ": ", detail));
    }
  }

  const std::vector<std::string>& errors() const { return errors_; }

 private:
  std::vector<std::string> errors_;
};

absl::Status ResolveFileDescriptorSet(
    const google::protobuf::FileDescriptor* fd,
    absl::flat_hash_map<std::string, const google::protobuf::FileDescriptor*>&
        fd_map) {
  if (fd_map.contains(fd->name())) {
    return absl::OkStatus();
  }
  fd_map[fd->name()] = fd;

  for (int i = 0; i < fd->dependency_count(); ++i) {
    INTR_RETURN_IF_ERROR(ResolveFileDescriptorSet(fd->dependency(i), fd_map));
  }

  return absl::OkStatus();
}

// Alternative to google::protobuf::FileDescriptor::CopySourceCodeInfoTo() which
// only copies the source code locations that have non-empty comments. This
// makes the resulting FileDescriptorProtos / FileDescriptorSets smaller.
void CopySourceCodeInfoWithComments(
    const google::protobuf::FileDescriptor& src_file_descriptor,
    google::protobuf::FileDescriptorProto* dst_file_descriptor_proto) {
  // Get all source code locations into an intermediate proto.
  google::protobuf::FileDescriptorProto intermediate_file_descriptor_proto;
  src_file_descriptor.CopySourceCodeInfoTo(&intermediate_file_descriptor_proto);

  // Copy only source code locations with comments to output proto.
  dst_file_descriptor_proto->clear_source_code_info();
  for (const google::protobuf::SourceCodeInfo::Location& location :
       intermediate_file_descriptor_proto.source_code_info().location()) {
    if (location.leading_detached_comments_size() > 0 ||
        location.has_leading_comments() || location.has_trailing_comments()) {
      *dst_file_descriptor_proto->mutable_source_code_info()->add_location() =
          location;
    }
  }
}

absl::StatusOr<google::protobuf::FileDescriptorSet>
CreateFileDescriptorSetFromFileDescriptors(
    const std::vector<const google::protobuf::FileDescriptor*>&
        file_descriptors) {
  absl::flat_hash_map<std::string, const google::protobuf::FileDescriptor*>
      fd_map;
  for (const google::protobuf::FileDescriptor* fd : file_descriptors) {
    INTR_RETURN_IF_ERROR(ResolveFileDescriptorSet(fd, fd_map));
  }

  google::protobuf::FileDescriptorSet fds;
  for (const auto& [name, fd] : fd_map) {
    google::protobuf::FileDescriptorProto* file_proto = fds.add_file();
    fd->CopyTo(file_proto);
    // CopyTo() only sets json_name for fields that specify it explicitly. Set
    // it for all fields like protoc does, so that clients get the JSON names
    // without having to compute them.
    fd->CopyJsonNameTo(file_proto);
    CopySourceCodeInfoWithComments(*fd, file_proto);
  }
  return fds;
}

absl::StatusOr<google::protobuf::FileDescriptorSet> CompileSchemas(
    std::initializer_list<std::pair<std::string_view, std::string_view>>
        filenames_and_schemas,
    google::protobuf::DescriptorDatabase* dependency_db) {
  InMemorySourceTree source_tree;
  for (const auto& [filename, schema] : filenames_and_schemas) {
    INTR_RETURN_IF_ERROR(source_tree.AddFile(filename, schema));
  }

  google::protobuf::compiler::SourceTreeDescriptorDatabase source_tree_database(
      &source_tree, dependency_db);
  ErrorCollector error_collector;
  source_tree_database.RecordErrorsTo(&error_collector);
  google::protobuf::DescriptorPool source_tree_pool(
      &source_tree_database,
      source_tree_database.GetValidationErrorCollector());

  std::vector<const google::protobuf::FileDescriptor*> file_descriptors;
  file_descriptors.reserve(filenames_and_schemas.size());
  for (const auto& [filename, _] : filenames_and_schemas) {
    // This call triggers the actual proto compilation.
    const google::protobuf::FileDescriptor* file_descriptor =
        source_tree_pool.FindFileByName(filename);
    if (file_descriptor == nullptr) {
      absl::Status status = absl::InvalidArgumentError(
          absl::StrFormat("Failed to parse proto '%s': %s", filename,
                          absl::StrJoin(error_collector.errors(), "; ")));
      return status;
    }
    file_descriptors.push_back(file_descriptor);
  }
  return CreateFileDescriptorSetFromFileDescriptors(file_descriptors);
}

}  // namespace intrinsic
