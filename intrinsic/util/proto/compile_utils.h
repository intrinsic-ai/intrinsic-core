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

#ifndef INTRINSIC_UTIL_PROTO_COMPILE_UTILS_H_
#define INTRINSIC_UTIL_PROTO_COMPILE_UTILS_H_

// Defines helpers for compiling proto schemas into transitively closed
// FileDescriptorSets which contain the file descriptors for the input schemas
// and all of their transitive dependencies (via file dependencies, i.e., import
// statements).

#include <initializer_list>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"

namespace intrinsic {

// Creates a transitively closed FileDescriptorSet proto that contains the
// descriptors of the given files and all of their transitive dependencies (via
// file dependencies, i.e., import statements).
absl::StatusOr<google::protobuf::FileDescriptorSet>
CreateFileDescriptorSetFromFileDescriptors(
    const std::vector<const google::protobuf::FileDescriptor*>&
        file_descriptors);

// Compiles one or more proto schemas and returns them as a transitively closed
// FileDescriptorSet that contains the file descriptors of the given schemas and
// all of their transitive dependencies (via file dependencies, i.e., import
// statements). Takes as arguments a sequence of pairs of (file name, proto
// schema) where "proto schema" refers to the text contents of a .proto file.
//
// Optionally, already compiled dependencies can be provided via the
// 'dependency_fds' and 'dependency_db' parameters. Dependencies linked into to
// the current binary are automatically provided and need not be passed
// manually.
absl::StatusOr<google::protobuf::FileDescriptorSet> CompileSchemas(
    std::initializer_list<std::pair<std::string_view, std::string_view>>
        filenames_and_schemas,
    google::protobuf::DescriptorDatabase* dependency_db = nullptr);

// Shorthand for CompileSchemas() that takes a single (filename, schema) pair.
inline absl::StatusOr<google::protobuf::FileDescriptorSet> CompileSchema(
    std::string_view filename, std::string_view schema,
    google::protobuf::DescriptorDatabase* dependency_db = nullptr) {
  return CompileSchemas({{filename, schema}}, dependency_db);
}

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_COMPILE_UTILS_H_
