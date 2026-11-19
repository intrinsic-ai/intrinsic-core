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

#ifndef INTRINSIC_PROTO_TOOLS_REGISTRY_FILE_DESCRIPTOR_SET_NAME_H_
#define INTRINSIC_PROTO_TOOLS_REGISTRY_FILE_DESCRIPTOR_SET_NAME_H_

#include <string_view>

#include "absl/status/statusor.h"
#include "intrinsic/proto_tools/proto/proto_registry_internal.pb.h"
#include "intrinsic/util/string_type.h"

namespace intrinsic::proto_registry {

// A FileDescriptorSetName is a unique identifier for a
// NamedFileDescriptorSet in the proto registry. It is handed out to clients
// which should not try to interpret it, hence we make it non-human readable by
// obfuscating its value. It is used as a cache key. Use the methods below to
// obfuscate/unobfuscate.
INTRINSIC_DEFINE_STRING_TYPE(FileDescriptorSetName);

// Returns an obfuscated FileDescriptorSetName for the given type URL prefix.
absl::StatusOr<FileDescriptorSetName> FileDescriptorSetNameForTypeUrlPrefix(
    std::string_view type_url_prefix);

// Returns an obfuscated FileDescriptorSetName for the given type URL.
absl::StatusOr<FileDescriptorSetName> FileDescriptorSetNameForTypeUrl(
    std::string_view type_url);

// Converts an obfuscated FileDescriptorSetName back to its internal proto
// representation, which holds either a type URL prefix or a type URL.
absl::StatusOr<
    intrinsic_proto::proto_registry::internal::FileDescriptorSetNameProto>
UnobfuscateFileDescriptorSetName(const FileDescriptorSetName& name);

}  // namespace intrinsic::proto_registry

#endif  // INTRINSIC_PROTO_TOOLS_REGISTRY_FILE_DESCRIPTOR_SET_NAME_H_
