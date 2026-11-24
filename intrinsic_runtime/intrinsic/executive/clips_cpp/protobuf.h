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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_PROTOBUF_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_PROTOBUF_H_

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/log.h"
#include "absl/log/log_streamer.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/map_field.h"
#include "google/protobuf/message.h"
#include "google/protobuf/message_lite.h"
#include "google/protobuf/text_format.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protopath.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/proto/parsed_type_url.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/strong_int.h"

namespace intrinsic {
namespace executive {
namespace clips {

DEFINE_STRONG_INT_TYPE(ProtoMessageId, int64_t);
DEFINE_STRONG_INT_TYPE(DescriptorPoolId, int64_t);

// A generic proto message that is built-in from the generated pool, i.e., a
// known message at compile-time included by a header and built into the binary.
template <typename T>
concept GeneratedProtoMessage =
    std::derived_from<std::remove_cvref_t<T>, google::protobuf::Message> &&
    !std::is_same_v<std::remove_cvref_t<T>, google::protobuf::Message> &&
    !std::is_same_v<std::remove_cvref_t<T>, google::protobuf::DynamicMessage> &&
    // There is a static T::descriptor() method, which only exists on generated
    // types
    requires {
      {
        std::remove_cvref_t<T>::descriptor()
      } -> std::same_as<const google::protobuf::Descriptor*>;
    };

// Extracts the message from the type_url and removes `type.googleapis.com/`
// prefix. Modifies type_url from
// `type.googleapis.com/intrinsic_proto.test_skill.Parameter` to
// `intrinsic_proto.test_skill.Parameter`
absl::StatusOr<std::string_view> GetAnyTypeNameFromTypeUrl(
    std::string_view type_url ABSL_ATTRIBUTE_LIFETIME_BOUND);

// Extracts the message from the type_url and removes `type.googleapis.com/`
// prefix Modifies type_url from
// `type.googleapis.com/intrinsic_proto.test_skill.Parameter` to
// `intrinsic_proto.test_skill.Parameter`
absl::StatusOr<std::string_view> GetAnyTypeNameFromMessage(
    const google::protobuf::Any& any_proto ABSL_ATTRIBUTE_LIFETIME_BOUND);

// Manage proto messages shared between CLIPS and C++ contexts.
// Maintains a mapping from ID (int64) to proto message. It also provides useful
// CLIPS functions to deal with proto messages from CLIPS.
// Class is thread-unsafe.
// TODO(timdn): consider thread-safety for concurrent incoming updates
#pragma clang diagnostic push
// TODO(ferstl): Add nullability annotations. Until then: suppress warning spam.
#pragma clang diagnostic ignored "-Wnullability-completeness"
class ProtobufManager {
 public:
  // ID representing a non-existing message. Particularly relevant on the CLIPS
  // side where we cannot deal with StatusOr.
  static constexpr ProtoMessageId kInvalidId = ProtoMessageId(0);

  static constexpr DescriptorPoolId kInvalidDescriptorPoolId =
      DescriptorPoolId(0);
  static constexpr DescriptorPoolId kGeneratedDescriptorPoolId =
      DescriptorPoolId(1);
  static constexpr DescriptorPoolId kStandardMessagesDescriptorPoolId =
      DescriptorPoolId(3);

  static absl::StatusOr<std::unique_ptr<ProtobufManager>> Create(
      Environment* environment) ABSL_LOCKS_EXCLUDED(environment->mutex());
  ~ProtobufManager() ABSL_LOCKS_EXCLUDED(env_->mutex())
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  // Add a new message. Manager takes ownership of the unique_ptr.
  // The message must have been created from the given pool_id.
  absl::StatusOr<ProtoMessageId> AddProto(
      absl_nonnull std::unique_ptr<google::protobuf::Message>&& proto,
      DescriptorPoolId pool_id) ABSL_LOCKS_EXCLUDED(protos_mutex_);
  // Add a proto. T must be a built-in message from the generated pool.
  // For messages casted from dynamic descriptor pools use AddProto and pass
  // which pool the message was created from instead.
  template <typename T>
    requires GeneratedProtoMessage<T>
  ProtoMessageId AddGeneratedProto(T&& proto)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  void RemoveProto(ProtoMessageId id) ABSL_LOCKS_EXCLUDED(protos_mutex_);
  void RemoveAllProtos() ABSL_LOCKS_EXCLUDED(protos_mutex_);

  // The passed file_descriptor_set is moved into the DescriptorPoolInfo
  absl::StatusOr<DescriptorPoolId> AddDescriptorPool(
      google::protobuf::FileDescriptorSet file_descriptor_set,
      std::string_view display_name, std::string_view type_url_prefix,
      std::string_view operation_name);
  absl::StatusOr<DescriptorPoolId> AddDescriptorPool(
      ProtoMessageId file_descriptor_set_proto_id,
      std::string_view display_name, std::string_view type_url_prefix,
      std::string_view operation_name);
  absl::StatusOr<DescriptorPoolId> AddDescriptorPool(
      std::string_view file_descriptor_set_proto_file_path,
      std::string_view display_name, std::string_view type_url_prefix,
      std::string_view operation_name);

  // This removes the pool `id`.
  // Also removes all the protos in that pool as the pool descriptors are
  // supposed to be deleted.
  void RemoveDescriptorPool(DescriptorPoolId id)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  // Removes all descriptor pools created in the context of operation_name.
  // operation_name must not be empty.
  // Also removes all protos contained in these pools.
  void RemoveOperationProtosAndPools(absl::string_view operation_name)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  // This gives access to the descriptor pool and associated message factory. Do
  // not store the DescriptorPoolInfo for later use. Do not try to modify its
  // members.
  // Proto messages created using this factory must be stored in the proto
  // manager and never copied/stored outside the ProtobufManager.
  // If a proto message is created from the factory, the resulting proto must be
  // added using AddProto(msg, DescriptorPoolId) with the pool id used to
  // retrieve this DescriptorPoolInfo.
  struct DescriptorPoolInfo {
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool;
    google::protobuf::MessageFactory* absl_nonnull message_factory;
    google::protobuf::DescriptorDatabase* absl_nullable descriptor_db = nullptr;
    std::string display_name;
    std::string type_url_prefix;
  };
  absl::StatusOr<DescriptorPoolInfo> GetDescriptorPool(DescriptorPoolId id);
  absl::StatusOr<std::optional<google::protobuf::FileDescriptorSet>>
  GetDescriptorPoolFileDescriptorSet(DescriptorPoolId id);

  // Get a message by ID.
  absl::StatusOr<const google::protobuf::Message*> GetProto(
      ProtoMessageId id) const ABSL_LOCKS_EXCLUDED(protos_mutex_);

  // Retrieves the given proto id as a message of type M. The functions fails of
  // id doesn't exist or is not of the same type as M.
  // M must be a built-in type, i.e., it cannot be a generic
  // google::protobuf::Message. The returned message is guaranteed to be built
  // from the internal generated pool.
  template <typename M>
    requires GeneratedProtoMessage<M>
  absl::StatusOr<std::unique_ptr<M>> GetProtoAs(ProtoMessageId id) const {
    INTR_ASSIGN_OR_RETURN(const google::protobuf::Message* proto, GetProto(id));

    // Dynamic cast works, i.e., compiled in protos
    const M* typed_proto = google::protobuf::DynamicCastMessage<M>(proto);
    if (typed_proto != nullptr) {
      auto typed_proto_ptr = std::make_unique<M>(*typed_proto);
      return typed_proto_ptr;
    }

    // Descriptors named differently? Error!
    if (M::GetDescriptor()->full_name() !=
        proto->GetDescriptor()->full_name()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Proto is of type '%s', not of type '%s' (internal id: %d)",
          proto->GetDescriptor()->full_name(), M::GetDescriptor()->full_name(),
          id.value()));
    }

    // Same name, but dynamic cast fails. We have at least one dynamic message,
    // try to serialize/deserialize it.
    auto typed_proto_ptr = std::make_unique<M>();
    std::string serialized_proto;
    if (!proto->SerializeToString(&serialized_proto)) {
      return absl::InternalError(absl::StrFormat(
          "Serialization failed for proto of type '%s' (internal id: %d)",
          proto->GetDescriptor()->name(), id.value()));
    }
    if (!typed_proto_ptr->ParseFromString(serialized_proto)) {
      return absl::InternalError(absl::StrFormat(
          "Parsing from serialized proto of type '%s' failed (internal id: %d)",
          proto->GetDescriptor()->name(), id.value()));
    }
    return typed_proto_ptr;
  }

  // Retrieves a proto pointer to the stored proto. Use this only to modify the
  // proto. Do not copy the message or store the pointer anywhere.
  template <typename M, typename = std::enable_if_t<
                            std::is_base_of_v<google::protobuf::Message, M>>>
  absl::StatusOr<M*> GetMutableProtoAs(ProtoMessageId id)
      ABSL_LOCKS_EXCLUDED(protos_mutex_) {
    INTR_ASSIGN_OR_RETURN(google::protobuf::Message * proto,
                          GetMutableProto(id));
    M* typed_proto = google::protobuf::DynamicCastMessage<M>(proto);
    if (!typed_proto) {
      if (M::GetDescriptor()->name() != proto->GetDescriptor()->name()) {
        return absl::UnavailableError(absl::StrFormat(
            "Proto %i is of type %s, not of type %s", id.value(),
            proto->GetDescriptor()->name(), M::GetDescriptor()->name()));
      } else {
        return absl::FailedPreconditionError(
            absl::StrFormat("Proto %i is of type %s, but cannot be casted",
                            id.value(), proto->GetDescriptor()->name()));
      }
    }
    return typed_proto;
  }

  // Find the DescriptorPoolId that proto_id was created from. This performs a
  // reverse lookup searching through all pools.
  absl::StatusOr<DescriptorPoolId> FindPoolIdForProto(ProtoMessageId proto_id)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  // Create a new message for the given message type name. The name is a fully
  // qualified name (including package prefix) for which the generated
  // descriptor pool is queried. Returns an error or the message ID of the new
  // message.
  absl::StatusOr<ProtoMessageId> CreateBuiltInProto(
      absl::string_view message_type_name);

  // Copy data from one proto to another.
  absl::Status CopyProto(ProtoMessageId src_msg_id, ProtoMessageId dst_msg_id);

  absl::StatusOr<Symbol> IsDefault(ProtoMessageId id)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  // In the following, a field_name (or returned names) is the direct field
  // member of the respective message ID. To access field in sub-messages, first
  // retrieve the message (adds a copy to the internal map and returns an ID as
  // a separate message), and then access the field in that sub-message. Be
  // cautious about removing unused temporary messages created this way to avoid
  // memory overrun.

  absl::StatusOr<Value> GetFieldValue(ProtoMessageId id,
                                      absl::string_view field_path)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);
  absl::StatusOr<Values> GetRepeatedFieldValues(ProtoMessageId id,
                                                absl::string_view field_path);
  absl::StatusOr<int64_t> GetRepeatedFieldLength(ProtoMessageId id,
                                                 absl::string_view field_path);
  absl::StatusOr<Value> GetMapFieldValue(ProtoMessageId id,
                                         absl::string_view field_path,
                                         const Value& key);
  absl::StatusOr<Value> GetMapFieldValue(ProtoMessageId id,
                                         absl::string_view field_path,
                                         absl::string_view key) {
    return GetMapFieldValue(id, field_path, Value(key));
  }
  absl::StatusOr<Values> GetMapFieldKeys(ProtoMessageId id,
                                         absl::string_view field_path);

  absl::StatusOr<std::string> ToString(ProtoMessageId id);
  absl::StatusOr<std::string> ToStringWithPool(ProtoMessageId id,
                                               DescriptorPoolId pool_id,
                                               int64_t max_lines);

  absl::StatusOr<Symbol> HasField(ProtoMessageId id,
                                  absl::string_view field_path);
  absl::StatusOr<Symbol> WhichOneof(ProtoMessageId id,
                                    absl::string_view field_path);
  absl::StatusOr<Symbol> IsRepeated(ProtoMessageId id,
                                    absl::string_view field_path);

  absl::Status ClearField(ProtoMessageId id, absl::string_view field_path);
  absl::Status SetFieldValue(ProtoMessageId id, absl::string_view field_path,
                             const Value& value);
  // Set a specific proto field from a proto. The expectations of the type of
  // the value proto depend on the field type. When types have different value
  // ranges a check is performed and out of range values are rejected, e.g.,
  // when setting an int32 from a Int64Value wrapper.
  // field type                proto type
  // DOUBLE/FLOAT              DoubleValue or FloatValue
  // All INT types             UInt32Value, Int32Value, UInt64Value, Int64Value
  // BOOL                      BoolValue
  // STRING                    StringValue
  // BYTES                     BytesValue
  // ENUM                      Not supported
  // MESSAGE                   The field's specific message type
  // Repeated                  Expects AnyList with the type proto from above
  absl::Status SetFieldFromProto(ProtoMessageId id,
                                 absl::string_view field_path,
                                 DescriptorPoolId descriptor_pool_id,
                                 ProtoMessageId value);
  // Set field_path in message to the given value.
  // path_source_expression is an optional string from debug outputs indicating
  // where the value comes from.
  absl::Status SetFieldFromProto(google::protobuf::Message* message,
                                 absl::string_view field_path,
                                 const DescriptorPoolInfo& pool_info,
                                 const google::protobuf::Message* value,
                                 absl::string_view path_source_expression);

  absl::Status SetRepeatedFieldValues(ProtoMessageId id,
                                      absl::string_view field_path,
                                      const Values& values);

  // Converts and AnyList proto to a list of protos. any_list_message must be of
  // type intrinsic_proto.executive.AnyList. All protos in the AnyList must
  // exist in the given pool_info.
  absl::StatusOr<std::vector<std::unique_ptr<google::protobuf::Message>>>
  ConvertAnyListToMessages(
      const google::protobuf::Message* absl_nonnull any_list_message,
      const DescriptorPoolInfo& pool_info, absl::string_view source_expression);

  // Returns true, if following the path in field_path goes through the same
  // oneof options that are set in the message referenced by proto_id.
  // field_path must be a proto path consisting of '/' separated fields.
  // If the function returns false, this indicates that assigning to field_path
  // would change a oneof option in proto_id that is not the last field in path,
  // i.e., implicitly changing the oneof option.
  //
  // Consider this example proto:
  // message TestMessage {
  //   int32 int32_value = 1;
  //   oneof foo_or_bar {
  //     TestMessage foo_msg = 2;
  //     TestMessage bar_msg = 3;
  //   }
  // }
  // with the message referred to by proto_id being:
  // foo_msg { int32_value: 42 }
  //
  // Queries for field_path:
  // /foo_msg/int32 -> true
  // /bar_msg/int32 -> false
  // /bar_msg -> true (field value is not in a different oneof option it is
  //                   changing the complete oneof message)
  // /int32_value -> true
  // /foo_msg/bar_msg/int32_value -> true
  // /bar_msg/foo_msg/int32_value -> false
  absl::StatusOr<bool> IsFieldValueInSameOneofOption(
      ProtoMessageId proto_id, absl::string_view field_path);
  absl::StatusOr<bool> IsFieldValueInSameOneofOption(
      const google::protobuf::Message* message, absl::string_view field_path);
  // Logs a warning if IsFieldValueInSameOneofOption is false or fails.
  void WarnIfFieldValueNotInSameOneofOption(
      const google::protobuf::Message* message, absl::string_view field_path);

  // Gets the type name of the proto stored at proto_id.
  absl::StatusOr<std::string> GetMessageTypeName(ProtoMessageId proto_id);

  // Gets the type name of the proto message represented by the Any proto stored
  // at proto_id. proto_id must be an Any proto.
  absl::StatusOr<std::string> GetAnyTypeName(ProtoMessageId proto_id);
  absl::StatusOr<std::string> GetAnyFieldTypeName(ProtoMessageId proto_id,
                                                  absl::string_view field_path);
  // Extract the skill_id from a full Intrinsic type URL.
  absl::StatusOr<std::string> GetAssetIdFromTypeUrl(absl::string_view type_url);
  // Extract the skill id from a Intrinsic type URL prefix, i.e., a type URL
  // without its message type (e.g., as stored in DescriptorPoolInfoInternal)
  absl::StatusOr<std::string> GetAssetIdFromTypeUrlPrefix(
      absl::string_view type_url_prefix);
  absl::StatusOr<std::string> GetAssetIdFromParsedUrl(
      const ParsedUrl& parsed_url);

  absl::StatusOr<ProtoMessageId> CastFromAnyWithPool(
      ProtoMessageId proto_id, DescriptorPoolId pool_id,
      absl::string_view expected_message_type);
  static absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
  CastFromAnyWithPool(const google::protobuf::Any& any_proto,
                      const DescriptorPoolInfo& pool_info,
                      absl::string_view expected_message_type);

  absl::StatusOr<ProtoMessageId> UnpackFromAnyField(
      ProtoMessageId proto_id, absl::string_view field_path,
      DescriptorPoolId pool_id);

  absl::StatusOr<google::protobuf::Any> CastToAny(ProtoMessageId proto_id)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);
  absl::Status PackToAnyField(ProtoMessageId proto_id,
                              absl::string_view field_path,
                              ProtoMessageId value_proto_id)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  // Creates a new updated proto that is the same type as proto_to_create_from,
  // but has the content of value_proto. The created proto has the same message
  // type and is created from the same pool as proto_to_create_from.
  // proto_to_create_from itself is unchanged. The resulting new proto is
  // initialized with the contents of value_proto. For this to succeed
  // proto_to_create_from must exist and value_proto must be of the same message
  // type with compatible contents as proto_to_create_from.
  absl::StatusOr<ProtoMessageId> CreateUpdatedProtoFromAny(
      ProtoMessageId proto_to_create_from,
      const google::protobuf::Any& value_proto)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  absl::Status SetMapFieldValue(ProtoMessageId id, absl::string_view field_path,
                                const Value& key, const Value& value);
  absl::Status SetMapFieldValue(ProtoMessageId id, absl::string_view field_name,
                                absl::string_view key, const Value& value) {
    return SetMapFieldValue(id, field_name, Value(key), value);
  }

  absl::Status CopyBytesValue(ProtoMessageId src_msg_id,
                              absl::string_view src_field_path,
                              ProtoMessageId dst_msg_id,
                              absl::string_view dst_field_path);

  absl::StatusOr<int64_t> BytesSize(ProtoMessageId msg_id,
                                    absl::string_view field_path);

  size_t GetNumProtos() const ABSL_LOCKS_EXCLUDED(protos_mutex_) {
    absl::MutexLock lock(protos_mutex_);
    return protos_.size();
  }
  size_t GetNumPools() const { return pools_.size(); }

  // Print registered pools with their associated protos. If pool_names is
  // optionally given the pool names are printed alongside the pools, if they
  // exist. FileDescriptorSets only show the associated file for identification.
  // Use DebugPrintAllProtos for the full content.
  void DebugPrintPools(
      absl::flat_hash_map<clips::DescriptorPoolId, std::string> pool_names = {},
      std::ostream* stream =
          &absl::LogInfoStreamer(__builtin_FILE(), __builtin_LINE()).stream());
  void DebugPrintAllProtos(
      size_t max_lines_per_proto = 0,
      std::ostream* stream = &absl::LogInfoStreamer(__builtin_FILE(),
                                                    __builtin_LINE())
                                  .stream()) const
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  absl::flat_hash_map<ProtoMessageId, std::string> DebugGetAllProtosAsStrings()
      const ABSL_LOCKS_EXCLUDED(protos_mutex_);

 private:
  struct DescriptorPoolInfoInternal {
    // Constructs an Info for a custom pool created from a file descriptor set.
    // The pool and factory are now owned by the DescriptorPoolInfoInternal.
    // These must have been created from the passed file_descriptor_set.
    DescriptorPoolInfoInternal(
        google::protobuf::FileDescriptorSet file_descriptor_set,
        absl_nonnull std::unique_ptr<google::protobuf::DescriptorDatabase> db,
        absl_nonnull std::unique_ptr<google::protobuf::DescriptorPool> pool,
        absl_nonnull std::unique_ptr<google::protobuf::MessageFactory> factory,
        absl::string_view display_name, absl::string_view type_url_prefix,
        std::optional<absl::string_view> operation_name);
    // Constructor to store a predefined pool and factory (e.g., the generated
    // pool). These must outlive this pool and implicitly any protos derived
    // from that.
    DescriptorPoolInfoInternal(
        const google::protobuf::DescriptorPool* absl_nonnull pool,
        google::protobuf::MessageFactory* absl_nonnull factory,
        absl::string_view display_name, absl::string_view type_url_prefix);

    // If the descriptor_db is non null, this is the FDS that was supplied when
    // adding this pool and that is stored in descriptor_db.
    std::optional<google::protobuf::FileDescriptorSet> file_descriptor_set;
    absl_nullable std::unique_ptr<google::protobuf::DescriptorDatabase>
        descriptor_db;
    // The pool and factory associated with the pool.
    // If file_descriptor_set_/descriptor_db_ are not empty, the pool must
    // have been created from these.
    const google::protobuf::DescriptorPool* absl_nonnull descriptor_pool;
    google::protobuf::MessageFactory* absl_nonnull message_factory;

    std::string display_name;
    std::string type_url_prefix;
    // Operation that this pool is associated with.
    // This implies that when the operation with 'operation_name' is gone it is
    // safe to remove all protos from this pool and the pool itself.
    // Almost all pools must be associated with an operation. The exception are
    // the pools created on constructions, e.g., the generated pool.
    std::optional<std::string> operation_name;

   private:
    absl_nullable std::unique_ptr<google::protobuf::DescriptorPool>
        owned_descriptor_pool;
    absl_nullable std::unique_ptr<google::protobuf::MessageFactory>
        owned_message_factory;
  };

  // A proto and associated information managed by this ProtobufManager.
  // The underlying proto can be accessed similar to standard wrapper classes
  // like unique_ptr via operation*/-> or .get().
  struct ManagedProto {
    explicit ManagedProto(std::unique_ptr<google::protobuf::Message> proto,
                          std::shared_ptr<DescriptorPoolInfoInternal> pool_info)
        : pool_info(std::move(pool_info)), proto(std::move(proto)) {}

    absl_nonnull std::shared_ptr<DescriptorPoolInfoInternal> pool_info;

    google::protobuf::Message& operator*() { return *proto; }
    const google::protobuf::Message& operator*() const { return *proto; }
    google::protobuf::Message* operator->() { return proto.get(); }
    const google::protobuf::Message* operator->() const { return proto.get(); }
    google::protobuf::Message* get() { return proto.get(); }
    const google::protobuf::Message* get() const { return proto.get(); }

   private:
    absl_nonnull std::unique_ptr<google::protobuf::Message> proto;
  };

  explicit ProtobufManager(Environment* environment);

  // Adds a DescriptorPoolInfoInternal for the generated pool. This will not
  // have AddStandardMessageTypes called explicitly, but the standard messages
  // are expected to be contained implicitly.
  absl::Status AddGeneratedDescriptorPool();
  // Adds the descriptor pool that contains standard messages required, e.g.,
  // for CEL.
  absl::Status AddStandardMessagesDescriptorPool();
  // Adds standard message types required, e.g., for CEL expressions to the
  // given file descriptor set. If standard message types file descriptors are
  // already in the given file_descriptor_set, then these are removed to avoid
  // duplicate file descriptors.
  // An error can occur when new message types cannot be added, e.g., because db
  // already contains a subset or superset of the types of a file descriptor.
  // This should never happen as the standard types consist of google.protobuf
  // standard types, which are assumed to be stable.
  absl::Status AddStandardMessageTypes(
      google::protobuf::FileDescriptorSet& file_descriptor_set);

  void RemoveDescriptorPoolNoLock(DescriptorPoolId id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_);

  void RemoveProtosForPool(DescriptorPoolId pool_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_);

  // The message must have been created from the given pool_info.
  absl::StatusOr<ProtoMessageId> AddProto(
      absl_nonnull std::unique_ptr<google::protobuf::Message>&& proto,
      absl_nonnull std::shared_ptr<DescriptorPoolInfoInternal> pool_info)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_);
  // The message must have been created from the given pool_info.
  absl::StatusOr<ProtoMessageId> AddProto(
      const google::protobuf::Message& proto,
      absl_nonnull std::shared_ptr<DescriptorPoolInfoInternal> pool_info)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_);

  // Get a ManagedProto to work with. The result must not be stored beyond the
  // protos_mutex_ lock.
  absl::StatusOr<const ManagedProto&> GetManagedProto(ProtoMessageId id) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_);

  // Only intended for accessing the proto to mutate the message itself.
  // Never store the result or make copies of the result.
  absl::StatusOr<google::protobuf::Message*> GetMutableProto(ProtoMessageId id)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  absl::StatusOr<ProtoMessageId> CastToAnyClips(ProtoMessageId proto_id)
      ABSL_LOCKS_EXCLUDED(protos_mutex_);

  DescriptorPoolInfo DescriptorPoolInfoFromInternal(
      const DescriptorPoolInfoInternal& pool_info);
  absl::StatusOr<std::shared_ptr<DescriptorPoolInfoInternal>>
  GetDescriptorPoolInternal(DescriptorPoolId id);

  absl::StatusOr<FieldFromPath> GetFieldFromPath(ProtoMessageId id,
                                                 absl::string_view field_path,
                                                 bool allow_index = true);

  absl::StatusOr<MutableFieldFromPath> GetMutableFieldFromPath(
      ProtoMessageId id, absl::string_view field_path, bool allow_index = true);

  absl::StatusOr<Value> InternalGetFieldValue(
      const google::protobuf::Message& msg,
      const google::protobuf::FieldDescriptor* field,
      const std::shared_ptr<DescriptorPoolInfoInternal>& pool_info)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_);
  absl::StatusOr<Value> InternalGetRepeatedFieldValue(
      const google::protobuf::Message& msg,
      const google::protobuf::FieldDescriptor* field, int index,
      const std::shared_ptr<DescriptorPoolInfoInternal>& pool_info)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_);
  absl::Status InternalSetFieldValue(
      google::protobuf::Message* msg,
      const google::protobuf::FieldDescriptor* field, const Value& value);
  absl::Status InternalSetRepeatedFieldValue(
      google::protobuf::Message* msg,
      const google::protobuf::FieldDescriptor* field, int index,
      const Value& value);
  absl::Status InternalSetRepeatedFieldValues(
      google::protobuf::Message* msg,
      const google::protobuf::FieldDescriptor* field, const Values& values,
      bool clear_existing);

  // Internal setters for fields from proto(s). Used for assignments. field in
  // msg is set from the value(s) protos.
  // assigned_source_expression, full_path and full_message_descriptor are
  // provided for error reporting.
  absl::Status InternalSetFieldFromProto(
      google::protobuf::Message* msg,
      const google::protobuf::FieldDescriptor* field,
      const google::protobuf::Message* value,
      absl::string_view assigned_source_expression, absl::string_view full_path,
      const google::protobuf::Descriptor* full_message_descriptor);
  absl::Status InternalSetRepeatedFieldFromProto(
      google::protobuf::Message* msg,
      const google::protobuf::FieldDescriptor* field, int index,
      const google::protobuf::Message* value,
      absl::string_view assigned_source_expression, absl::string_view full_path,
      const google::protobuf::Descriptor* full_message_descriptor);
  absl::Status InternalSetRepeatedFieldFromProtos(
      google::protobuf::Message* msg,
      const google::protobuf::FieldDescriptor* field,
      absl::Span<const std::unique_ptr<google::protobuf::Message>> values,
      bool clear_existing, absl::string_view assigned_source_expression,
      absl::string_view full_path,
      const google::protobuf::Descriptor* full_message_descriptor);
  absl::Status InternalSetRepeatedFieldFromProtos(
      google::protobuf::Message* msg,
      const google::protobuf::FieldDescriptor* field,
      const std::vector<const google::protobuf::Message*>& values,
      bool clear_existing, absl::string_view assigned_source_expression,
      absl::string_view full_path,
      const google::protobuf::Descriptor* full_message_descriptor);
  absl::Status InternalSetRepeatedFieldFromAnyListProto(
      const MutableFieldFromPath& field, const DescriptorPoolInfo& pool_info,
      const google::protobuf::Message* value_msg,
      absl::string_view assigned_source_expression, absl::string_view full_path,
      const google::protobuf::Descriptor* full_message_descriptor);
  absl::StatusOr<std::vector<std::unique_ptr<google::protobuf::Message>>>
  ConvertAnyListToMessages(
      const google::protobuf::Message* absl_nonnull any_list_message,
      const DescriptorPoolInfo& pool_info,
      absl::string_view assigned_field_type,
      absl::string_view assigned_source_expression, absl::string_view full_path,
      const google::protobuf::Descriptor* absl_nullable
          full_message_descriptor);

  template <typename ReturnType, typename... Args>
  absl::Status RegisterFunction(
      const std::string& name,
      const std::function<ReturnType(Args...)>& function)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
    INTR_RETURN_IF_ERROR(env_->AddFunction(name, function));
    functions_.push_back(name);
    return absl::OkStatus();
  }

  absl::Status RegisterFunctions() ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());
  void UnregisterFunctions() ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());

  ProtoMessageId GenerateNextProtoId()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_);
  DescriptorPoolId GenerateNextDescriptorPoolId();

  Environment* env_;
  absl::BitGen id_random_generator_;

  mutable absl::Mutex protos_mutex_;
  absl::node_hash_map<ProtoMessageId, ManagedProto> protos_
      ABSL_GUARDED_BY(protos_mutex_);

  absl::node_hash_map<DescriptorPoolId,
                      std::shared_ptr<DescriptorPoolInfoInternal>>
      pools_;
  std::vector<std::string> functions_;

  // This is expected to be valid at all times. Filled on construction.
  std::shared_ptr<DescriptorPoolInfoInternal> generated_pool_info_;
};

template <typename T>
  requires GeneratedProtoMessage<T>
ProtoMessageId ProtobufManager::AddGeneratedProto(T&& proto)
    ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  absl::StatusOr<ProtoMessageId> added_proto =
      AddProto(std::make_unique<std::remove_cvref_t<T>>(std::forward<T>(proto)),
               generated_pool_info_);
  if (!added_proto.ok()) {
    LOG(ERROR) << added_proto.status();
    return kInvalidId;
  }
  return *added_proto;
}

// Implements RAII for a proto in the protobuf manager.
// Given a proto id, remove that proto from the protobuf manager on
// destruction. Given a proto, add the proto on initialization and then remove
// on destruction.
class ScopedProto {
 public:
  ScopedProto(ProtoMessageId proto_id, ProtobufManager* proto_mgr)
      : proto_id_(proto_id), proto_mgr_(proto_mgr) {}
  template <typename T>
    requires GeneratedProtoMessage<T>
  ScopedProto(T proto, ProtobufManager* proto_mgr)
      : proto_id_(proto_mgr->AddGeneratedProto(std::move(proto))),
        proto_mgr_(proto_mgr) {}
  ScopedProto(const ScopedProto&) = delete;
  ScopedProto(ScopedProto&& other) noexcept
      : proto_id_(other.proto_id_), proto_mgr_(other.proto_mgr_) {
    other.proto_id_ = ProtobufManager::kInvalidId;
  }
  ~ScopedProto() { proto_mgr_->RemoveProto(proto_id_); }

  ProtoMessageId proto_id() const { return proto_id_; }

 private:
  ProtoMessageId proto_id_;
  ProtobufManager* proto_mgr_;
};
#pragma clang diagnostic pop

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_PROTOBUF_H_
