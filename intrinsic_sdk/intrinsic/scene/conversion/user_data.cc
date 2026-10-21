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

#include "intrinsic/scene/conversion/user_data.h"

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/proto/dynamic_message_parser.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::scene_object {
namespace {

using ::intrinsic_proto::scene_object::v1::SceneObject;

// A helper class to find message types and extensions from a DescriptorPool.
// This is needed for TextFormat::Printer to correctly print Any protos.
class PoolTypeFinderByName : public google::protobuf::TextFormat::Finder {
 public:
  explicit PoolTypeFinderByName(const google::protobuf::DescriptorPool* pool)
      : pool_(pool) {}

  const google::protobuf::Descriptor* FindAnyType(
      const google::protobuf::Message& /*message*/,
      const std::string& /*prefix*/, const std::string& name) const override {
    return pool_->FindMessageTypeByName(name);
  }

 private:
  const google::protobuf::DescriptorPool* pool_;
};

absl::Status AnyCanBeUnpackedIntoConcreteType(
    const google::protobuf::Any& any_proto,
    const google::protobuf::DescriptorPool& pool,
    google::protobuf::DynamicMessageFactory& factory) {
  std::string full_type_name;
  if (const auto& url = any_proto.type_url();
      !google::protobuf::Any::ParseAnyTypeUrl(url, &full_type_name)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Failed to parse type URL: `", url, "`."));
  }

  const google::protobuf::Descriptor* descriptor =
      pool.FindMessageTypeByName(full_type_name);
  if (descriptor == nullptr) {
    return absl::NotFoundError(absl::StrCat(
        "Proto descriptor not found for type: `", full_type_name, "`"));
  }

  const google::protobuf::Message* prototype = factory.GetPrototype(descriptor);
  if (prototype == nullptr) {
    return absl::InternalError(absl::StrCat(
        "Failed to create message for type `", full_type_name, "`."));
  }

  std::unique_ptr<google::protobuf::Message> concrete_msg(prototype->New());
  if (!any_proto.UnpackTo(concrete_msg.get())) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Failed to unpack Any proto to type `", full_type_name, "`."));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status UserDataCanBeParsed(
    const google::protobuf::Map<std::string, google::protobuf::Any>& user_data,
    const google::protobuf::FileDescriptorSet& fds) {
  if (user_data.empty()) {
    return absl::OkStatus();
  }

  google::protobuf::SimpleDescriptorDatabase db;
  INTR_RETURN_IF_ERROR(intrinsic::PopulateDescriptorDatabase(&db, fds));
  google::protobuf::DescriptorPool pool(&db);
  auto factory = std::make_unique<google::protobuf::DynamicMessageFactory>();
  for (const auto& [key, any_proto] : user_data) {
    INTR_RETURN_IF_ERROR(
        AnyCanBeUnpackedIntoConcreteType(any_proto, pool, *factory))
        << "Failed to parse user_data for key: " << key;
  }

  return absl::OkStatus();
}

absl::StatusOr<google::protobuf::Map<std::string, google::protobuf::Any>>
UserDataFromString(const std::string& textproto,
                   const google::protobuf::FileDescriptorSet& fds) {
  google::protobuf::FileDescriptorSet combined_fds = fds;
  intrinsic::MergeFileDescriptorSet<SceneObject>(combined_fds);

  INTR_ASSIGN_OR_RETURN(
      MessageAndDeps parsed_msg,
      DynamicMessageParser::ParseSingleTextProto(
          combined_fds, "intrinsic_proto.scene_object.v1.SceneObject",
          textproto));

  std::string serialized;
  if (!parsed_msg.message->SerializeToString(&serialized)) {
    return absl::InternalError("Failed to serialize dynamic message.");
  }
  SceneObject scene_object;
  if (!scene_object.ParseFromString(serialized)) {
    return absl::InternalError("Failed to parse scene object proto.");
  }
  return scene_object.user_data();
}

absl::StatusOr<std::string> UserDataToString(
    const google::protobuf::Map<std::string, google::protobuf::Any>& user_data,
    const google::protobuf::FileDescriptorSet& fds) {
  google::protobuf::FileDescriptorSet combined_fds = fds;
  intrinsic::MergeFileDescriptorSet<SceneObject>(combined_fds);

  INTR_RETURN_IF_ERROR(UserDataCanBeParsed(user_data, combined_fds));

  SceneObject scene_object;
  *scene_object.mutable_user_data() = user_data;

  google::protobuf::TextFormat::Printer printer;
  printer.SetExpandAny(true);

  google::protobuf::SimpleDescriptorDatabase db;
  INTR_RETURN_IF_ERROR(
      intrinsic::PopulateDescriptorDatabase(&db, combined_fds));
  google::protobuf::DescriptorPool pool(&db);
  auto finder = std::make_unique<PoolTypeFinderByName>(&pool);
  printer.SetFinder(finder.get());

  std::string textproto;
  if (!printer.PrintToString(scene_object, &textproto)) {
    return absl::InvalidArgumentError(
        "Failed to print user_data to text proto.");
  }

  return textproto;
}

}  // namespace intrinsic::scene_object
