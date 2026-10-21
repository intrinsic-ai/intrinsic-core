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

#ifndef INTRINSIC_UTIL_PROTO_DESCRIPTOR_POOLS_H_
#define INTRINSIC_UTIL_PROTO_DESCRIPTOR_POOLS_H_

#include <memory>
#include <string_view>

#include "absl/status/statusor.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace intrinsic {

// Creates a new, empty proto instance of the message type with the given name
// using the prototype message of the given descriptor pool and message factory.
//
// The returned message is owned by the caller and must be destroyed before the
// given pool and factory are destroyed (the message has references to pool and
// factory).
absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
CreateProtoInstanceFromDescriptorPool(
    std::string_view message_type_name,
    const google::protobuf::DescriptorPool* desc_pool,
    google::protobuf::MessageFactory* msg_factory);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_DESCRIPTOR_POOLS_H_
