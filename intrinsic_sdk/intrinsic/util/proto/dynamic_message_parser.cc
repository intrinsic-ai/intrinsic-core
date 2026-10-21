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

#include "intrinsic/util/proto/dynamic_message_parser.h"

#include <memory>
#include <string_view>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/dynamic_message.h"
#include "intrinsic/util/proto/descriptor_pools.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

absl::StatusOr<std::unique_ptr<DynamicMessageParser>>
DynamicMessageParser::Create(
    const google::protobuf::FileDescriptorSet& file_descriptor_set) {
  auto db = std::make_unique<google::protobuf::SimpleDescriptorDatabase>();
  INTR_RETURN_IF_ERROR(
      PopulateDescriptorDatabase(db.get(), file_descriptor_set));
  auto pool = std::make_unique<google::protobuf::DescriptorPool>(db.get());
  auto factory =
      std::make_unique<google::protobuf::DynamicMessageFactory>(pool.get());
  return absl::WrapUnique(new DynamicMessageParser(
      std::move(db), std::move(pool), std::move(factory)));
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
DynamicMessageParser::ParseTextProto(std::string_view message_full_name,
                                     std::string_view input) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> msg,
      CreateProtoInstanceFromDescriptorPool(
          message_full_name, this->pool_.get(), this->factory_.get()));

  INTR_RETURN_IF_ERROR(ParseTextProtoInto(input, msg.get()));

  return std::move(msg);
}

absl::StatusOr<MessageAndDeps> DynamicMessageParser::ParseSingleTextProto(
    const google::protobuf::FileDescriptorSet& file_descriptor_set,
    std::string_view message_full_name, std::string_view input) {
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<DynamicMessageParser> parser,
                        Create(file_descriptor_set));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<google::protobuf::Message> message,
                        parser->ParseTextProto(message_full_name, input));
  return MessageAndDeps{
      std::move(parser),
      std::move(message),
  };
}

}  // namespace intrinsic
