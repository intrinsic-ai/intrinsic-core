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

#ifndef INTRINSIC_EXECUTIVE_TOOLS_DESCRIPTOR_POOL_LOADER_H_
#define INTRINSIC_EXECUTIVE_TOOLS_DESCRIPTOR_POOL_LOADER_H_

#include <memory>
#include <string_view>

#include "absl/status/statusor.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"

namespace intrinsic::executive {

// Class that encapsulates a FileDescriptorSet, DescriptorDatabase, and
// DescriptorPool. Supports loading binary FileDescriptorSet files directly from
// disk paths or Bazel runfiles, while ensuring proper lifetime management
// (DescriptorDatabase outlives DescriptorPool).
class LoadedDescriptorPool {
 public:
  // Creates a LoadedDescriptorPool from an existing FileDescriptorSet.
  static absl::StatusOr<std::unique_ptr<LoadedDescriptorPool>> Create(
      google::protobuf::FileDescriptorSet fdset);

  // Creates a LoadedDescriptorPool by loading a FileDescriptorSet from a disk
  // path.
  static absl::StatusOr<std::unique_ptr<LoadedDescriptorPool>> LoadFromPath(
      std::string_view file_path);

  // Creates a LoadedDescriptorPool by loading a FileDescriptorSet from Bazel
  // runfiles.
  static absl::StatusOr<std::unique_ptr<LoadedDescriptorPool>> LoadFromRunfiles(
      std::string_view runfiles_path);

  LoadedDescriptorPool(const LoadedDescriptorPool&) = delete;
  LoadedDescriptorPool& operator=(const LoadedDescriptorPool&) = delete;

  LoadedDescriptorPool(LoadedDescriptorPool&&) = delete;
  LoadedDescriptorPool& operator=(LoadedDescriptorPool&&) = delete;

  const google::protobuf::FileDescriptorSet& file_descriptor_set() const {
    return file_descriptor_set_;
  }

  google::protobuf::DescriptorDatabase* descriptor_db() {
    return descriptor_db_.get();
  }
  const google::protobuf::DescriptorDatabase* descriptor_db() const {
    return descriptor_db_.get();
  }

  google::protobuf::DescriptorPool* descriptor_pool() {
    return descriptor_pool_.get();
  }
  const google::protobuf::DescriptorPool* descriptor_pool() const {
    return descriptor_pool_.get();
  }

 private:
  explicit LoadedDescriptorPool(
      google::protobuf::FileDescriptorSet file_descriptor_set);

  google::protobuf::FileDescriptorSet file_descriptor_set_;
  std::unique_ptr<google::protobuf::SimpleDescriptorDatabase> descriptor_db_;
  std::unique_ptr<google::protobuf::DescriptorPool> descriptor_pool_;
};

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_TOOLS_DESCRIPTOR_POOL_LOADER_H_
