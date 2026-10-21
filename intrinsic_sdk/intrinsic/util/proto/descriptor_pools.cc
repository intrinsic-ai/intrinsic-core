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

#include "intrinsic/util/proto/descriptor_pools.h"

#include <memory>
#include <string_view>

#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"

namespace intrinsic {

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
CreateProtoInstanceFromDescriptorPool(
    std::string_view message_type_name,
    const google::protobuf::DescriptorPool* desc_pool,
    google::protobuf::MessageFactory* msg_factory) {
  if (desc_pool == nullptr) {
    return absl::InternalError("Failed to get descriptor pool");
  }
  if (msg_factory == nullptr) {
    return absl::InternalError("Failed to get message factory");
  }

  const google::protobuf::Descriptor* descriptor =
      desc_pool->FindMessageTypeByName(message_type_name);
  if (descriptor == nullptr) {
    return absl::NotFoundError(absl::StrFormat(
        "The message type '%s' does not exist in the descriptor pool",
        message_type_name));
  }

  // Prototype message is owned by the factory.
  const google::protobuf::Message* prototype_msg =
      msg_factory->GetPrototype(descriptor);
  if (prototype_msg == nullptr) {
    return absl::InternalError(absl::StrFormat(
        "Failed to get prototype message for %s", message_type_name));
  }

  // This message holds references into the factory and descriptor pool and must
  // be destroyed before the pool & factory. See
  // google::protobuf::DynamicMessageFactory::GetPrototype() for more details.
  google::protobuf::Message* mutable_msg = prototype_msg->New();
  if (mutable_msg == nullptr) {
    return absl::InternalError(absl::StrFormat(
        "Failed to create new message for %s", message_type_name));
  }

  return absl::WrapUnique(mutable_msg);
}

}  // namespace intrinsic
