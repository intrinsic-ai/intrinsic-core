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

#include "intrinsic_runtime/intrinsic/proto_tools/util/descriptor_pool_loader.h"

#include <memory>
#include <string_view>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/file.h"

namespace intrinsic::executive {

namespace {

absl::StatusOr<google::protobuf::FileDescriptorSet>
LoadFileDescriptorSetFromPath(std::string_view file_path) {
  google::protobuf::FileDescriptorSet fdset;
  INTR_RETURN_IF_ERROR(
      file::GetBinaryProto(file_path, &fdset, file::Defaults()));
  return fdset;
}

absl::StatusOr<google::protobuf::FileDescriptorSet>
LoadFileDescriptorSetFromRunfiles(std::string_view runfiles_path) {
  return LoadFileDescriptorSetFromPath(
      PathResolver::ResolveRunfilesPath(runfiles_path));
}

// Adds every file of `fdset` to `db` without copying (via AddUnowned).
// `fdset` must outlive `db`.
absl::Status PopulateDatabase(const google::protobuf::FileDescriptorSet& fdset,
                              google::protobuf::SimpleDescriptorDatabase& db) {
  for (const google::protobuf::FileDescriptorProto& file : fdset.file()) {
    if (!db.AddUnowned(&file)) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Failed to add descriptor '%s' to descriptor database", file.name()));
    }
  }
  return absl::OkStatus();
}

}  // namespace

LoadedDescriptorPool::LoadedDescriptorPool(
    google::protobuf::FileDescriptorSet fdset)
    : owned_file_descriptor_set_(std::move(fdset)),
      file_descriptor_set_(&owned_file_descriptor_set_),
      descriptor_db_(
          std::make_unique<google::protobuf::SimpleDescriptorDatabase>()),
      descriptor_pool_(std::make_unique<google::protobuf::DescriptorPool>(
          descriptor_db_.get())) {}

LoadedDescriptorPool::LoadedDescriptorPool(
    const google::protobuf::FileDescriptorSet* external_fdset)
    : file_descriptor_set_(external_fdset),
      descriptor_db_(
          std::make_unique<google::protobuf::SimpleDescriptorDatabase>()),
      descriptor_pool_(std::make_unique<google::protobuf::DescriptorPool>(
          descriptor_db_.get())) {}

absl::StatusOr<std::unique_ptr<LoadedDescriptorPool>>
LoadedDescriptorPool::Create(google::protobuf::FileDescriptorSet fdset) {
  // Move the fdset into its final destination first. That allows us to use
  // `AddUnowned` later, as we are sure file pointers will stay valid
  // throughout the lifetime of the descriptor pool. The pool only queries the
  // database lazily, so it is fine to populate the database after the pool has
  // been created.
  auto loaded = absl::WrapUnique(new LoadedDescriptorPool(std::move(fdset)));
  INTR_RETURN_IF_ERROR(
      PopulateDatabase(*loaded->file_descriptor_set_, *loaded->descriptor_db_));
  return loaded;
}

absl::StatusOr<std::unique_ptr<LoadedDescriptorPool>>
LoadedDescriptorPool::CreateUnowned(
    const google::protobuf::FileDescriptorSet& fdset) {
  auto loaded = absl::WrapUnique(new LoadedDescriptorPool(&fdset));
  INTR_RETURN_IF_ERROR(PopulateDatabase(fdset, *loaded->descriptor_db_));
  return loaded;
}

absl::StatusOr<std::unique_ptr<LoadedDescriptorPool>>
LoadedDescriptorPool::LoadFromPath(std::string_view file_path) {
  INTR_ASSIGN_OR_RETURN(google::protobuf::FileDescriptorSet fdset,
                        LoadFileDescriptorSetFromPath(file_path));
  return Create(std::move(fdset));
}

absl::StatusOr<std::unique_ptr<LoadedDescriptorPool>>
LoadedDescriptorPool::LoadFromRunfiles(std::string_view runfiles_path) {
  INTR_ASSIGN_OR_RETURN(google::protobuf::FileDescriptorSet fdset,
                        LoadFileDescriptorSetFromRunfiles(runfiles_path));
  return Create(std::move(fdset));
}

}  // namespace intrinsic::executive
