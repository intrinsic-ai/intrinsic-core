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

#ifndef INTRINSIC_UTIL_PROTO_DESCRIPTORS_H_
#define INTRINSIC_UTIL_PROTO_DESCRIPTORS_H_

#include <type_traits>

#include "absl/status/status.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/message.h"

namespace intrinsic {

// Generates a FileDescriptorSet given a Descriptor. The returned
// FileDescriptorSet includes all transitive dependencies of the descriptor.
google::protobuf::FileDescriptorSet GenFileDescriptorSet(
    const google::protobuf::Descriptor& descriptor);

// Generates a FileDescriptorSet given a Descriptor, including source code info.
google::protobuf::FileDescriptorSet GenFileDescriptorSetWithSourceInfo(
    const google::protobuf::Descriptor& descriptor);

// Generates a FileDescriptorSet for message type ProtoT. The returned
// FileDescriptorSet includes all transitive dependencies of the ProtoT.
template <class ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                            google::protobuf::Message, ProtoT>>>
google::protobuf::FileDescriptorSet GenFileDescriptorSet() {
  return GenFileDescriptorSet(*ProtoT::GetDescriptor());
}

// Generates a FileDescriptorSet for message type ProtoT, including source code
// info.
template <class ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                            google::protobuf::Message, ProtoT>>>
google::protobuf::FileDescriptorSet GenFileDescriptorSetWithSourceInfo() {
  return GenFileDescriptorSetWithSourceInfo(*ProtoT::GetDescriptor());
}

// Merges files from a descriptor into an existing file descriptor set without
// adding duplicates.
void MergeFileDescriptorSet(const google::protobuf::Descriptor& descriptor,
                            google::protobuf::FileDescriptorSet& set);

// Merges files from a descriptor into an existing file descriptor set without
// adding duplicates, including source code info.
void MergeFileDescriptorSetWithSourceInfo(
    const google::protobuf::Descriptor& descriptor,
    google::protobuf::FileDescriptorSet& set);

// Merges files from the descriptor for ProtoT into an existing file descriptor
// set without adding duplicates.
template <class ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                            google::protobuf::Message, ProtoT>>>
void MergeFileDescriptorSet(google::protobuf::FileDescriptorSet& set) {
  return MergeFileDescriptorSet(*ProtoT::GetDescriptor(), set);
}

// Merges files from the descriptor for ProtoT into an existing file descriptor
// set without adding duplicates, including source code info.
template <class ProtoT, typename = std::enable_if_t<std::is_base_of_v<
                            google::protobuf::Message, ProtoT>>>
void MergeFileDescriptorSetWithSourceInfo(
    google::protobuf::FileDescriptorSet& set) {
  return MergeFileDescriptorSetWithSourceInfo(*ProtoT::GetDescriptor(), set);
}

// Merges files from a file descriptor set into another file descriptor set. If
// a file descriptor already exists in the destination set it is skipped.
absl::Status MergeFileDescriptorSet(
    const google::protobuf::FileDescriptorSet& src_set,
    google::protobuf::FileDescriptorSet& dst_set);

// Merges files from a file descriptor set into another file descriptor set. If
// a file descriptor already exists in the destination set it is skipped.
// Includes source code info from the source set.
absl::Status MergeFileDescriptorSetWithSourceInfo(
    const google::protobuf::FileDescriptorSet& src_set,
    google::protobuf::FileDescriptorSet& dst_set);

// Populates the given descriptor database with all file descriptors in the
// given FileDescriptorSet.
absl::Status PopulateDescriptorDatabase(
    google::protobuf::SimpleDescriptorDatabase* db,
    const google::protobuf::FileDescriptorSet& file_descriptor_set);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_DESCRIPTORS_H_
