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

#ifndef INTRINSIC_UTIL_PROTO_DYNAMIC_MESSAGE_PARSER_H_
#define INTRINSIC_UTIL_PROTO_DYNAMIC_MESSAGE_PARSER_H_

#include <memory>
#include <string_view>
#include <utility>

#include "absl/status/statusor.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/dynamic_message.h"

namespace intrinsic {

class DynamicMessageParser;

// Wraps a single, dynamically-parsed proto message and its dependencies (such
// as the descriptor pool and message factory from which it was created) that
// need to be kept alive while working with the message.
struct MessageAndDeps {
  std::unique_ptr<DynamicMessageParser> parser;
  // Keep below 'parser' (the message needs to be destroyed before the
  // descriptor pool & message factory from which it was created).
  std::unique_ptr<google::protobuf::Message> message;

  google::protobuf::Message* operator*() { return message.get(); }
};

// Dynamically parses proto messages based on a given file descriptor set and
// not using the global descriptor pool.
class DynamicMessageParser {
 public:
  DynamicMessageParser() = delete;
  DynamicMessageParser(const DynamicMessageParser&) = delete;

  // Creates a DynamicMessageParser which uses the given file descriptor set.
  static absl::StatusOr<std::unique_ptr<DynamicMessageParser>> Create(
      const google::protobuf::FileDescriptorSet& file_descriptor_set);

  // Parses the given text proto into a message of the given full name. The
  // returned message is owned by the caller and must be destroyed before the
  // DynamicMessageParser is destroyed.
  //
  // Usage example:
  //   ASSERT_OK_AND_ASSIGN(std::unique_ptr<DynamicMessageParser> parser,
  //                        DynamicMessageParser::Create(file_descriptor_set));
  //   ASSERT_OK_AND_ASSIGN(
  //       std::unique_ptr<google::protobuf::Message> message,
  //       parser->ParseTextProto("my_protos.MyMessage", "my_field: 42"));
  //   LOG(INFO) << message->DebugString();
  absl::StatusOr<std::unique_ptr<google::protobuf::Message>> ParseTextProto(
      std::string_view message_full_name, std::string_view input);

  // Parses the given text proto into a message of the given full name using the
  // given file descriptor set. Returns an object that wraps the created message
  // and its dependencies (descriptor pool, message factory, ...) and which can
  // be destroyed safely.
  //
  // This is a convenience shorthand for ParseTextProto() above which reduces
  // the amount of boilerplate in cases where only one message needs to be
  // parsed.
  //
  // Usage example:
  //    ASSERT_OK_AND_ASSIGN(
  //        MessageAndDeps message,
  //        DynamicMessageParser::ParseSingleTextProto(
  //            file_descriptor_set, "my_protos.MyMessage", "my_field: 42"));
  //    LOG(INFO) << message->msg()->DebugString();
  static absl::StatusOr<MessageAndDeps> ParseSingleTextProto(
      const google::protobuf::FileDescriptorSet& file_descriptor_set,
      std::string_view message_full_name, std::string_view input);

 private:
  DynamicMessageParser(
      std::unique_ptr<google::protobuf::SimpleDescriptorDatabase> db,
      std::unique_ptr<google::protobuf::DescriptorPool> pool,
      std::unique_ptr<google::protobuf::DynamicMessageFactory> factory)
      : db_(std::move(db)),
        pool_(std::move(pool)),
        factory_(std::move(factory)) {}

  std::unique_ptr<google::protobuf::SimpleDescriptorDatabase> db_;
  std::unique_ptr<google::protobuf::DescriptorPool> pool_;
  std::unique_ptr<google::protobuf::DynamicMessageFactory> factory_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_DYNAMIC_MESSAGE_PARSER_H_
