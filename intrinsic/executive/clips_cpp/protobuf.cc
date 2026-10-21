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

#include "intrinsic/executive/clips_cpp/protobuf.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "eval/public/structs/cel_proto_descriptor_pool_builder.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/descriptor_database.h"
#include "google/protobuf/dynamic_message.h"
#include "google/protobuf/message.h"
#include "google/protobuf/message_lite.h"
#include "google/protobuf/util/message_differencer.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protopath.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/any_list.pb.h"
#include "intrinsic/util/proto/descriptor_pools.h"
#include "intrinsic/util/proto/descriptors.h"
#include "intrinsic/util/proto/parsed_type_url.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"

using intrinsic_proto::status::ExtendedStatus;

namespace intrinsic {
namespace executive {
namespace clips {
namespace {
constexpr char kPbCreate[] = "pb-create";
constexpr char kPbClone[] = "pb-clone";
constexpr char kPbCopy[] = "pb-copy";
constexpr char kPbRemove[] = "pb-remove";
constexpr char kPbIsDefault[] = "pb-is-default";
constexpr char kPbGetTypeName[] = "pb-get-type-name";
constexpr char kPbGetFieldValue[] = "pb-get-field";
constexpr char kPbGetRepeatedFieldValues[] = "pb-get-repeated-values";
constexpr char kPbGetRepeatedFieldLength[] = "pb-get-repeated-length";
constexpr char kPbGetMapFieldValue[] = "pb-get-map-value";
constexpr char kPbGetMapFieldKeys[] = "pb-get-map-keys";
constexpr char kPbSetFieldValue[] = "pb-set-field";
constexpr char kPbSetFieldFromProto[] = "pb-set-field-from-proto";
constexpr char kPbClearField[] = "pb-clear-field";
constexpr char kPbCopyBytesFieldValue[] = "pb-copy-bytes";
constexpr char kPbBytesSizeValue[] = "pb-bytes-size";
constexpr char kPbIsFieldValueInSameOneofOption[] =
    "pb-is-field-value-in-same-oneof-option";
constexpr char kPbSetRepeatedFieldValues[] = "pb-set-repeated-values";
constexpr char kPbSetMapFieldValue[] = "pb-set-map-value";
constexpr char kPbHasField[] = "pb-has-field";
constexpr char kPbWhichOneof[] = "pb-which-oneof";
constexpr char kPbIsRepeated[] = "pb-is-repeated";
constexpr char kPbToString[] = "pb-tostring";
constexpr char kPbToStringWithPool[] = "pb-tostring-with-pool";
constexpr char kPbAddDescriptorPool[] = "pb-add-descriptor-pool";
constexpr char kPbRemoveDescriptorPool[] = "pb-remove-descriptor-pool";
constexpr char kPbRemoveOperationProtosAndPools[] =
    "pb-remove-operation-protos-and-pools";
constexpr char kPbGetAnyTypeName[] = "pb-get-any-type-name";
constexpr char kPbGetAnyFieldTypeName[] = "pb-get-any-field-type-name";
constexpr char kPbGetAssetIdFromTypeUrl[] =
    "pb-get-skill-or-asset-id-from-type-url";
constexpr char kPbCastFromAnyWithPool[] = "pb-cast-from-any-with-pool";
constexpr char kPbCastToAny[] = "pb-cast-to-any";
constexpr char kPbPackToAnyField[] = "pb-pack-to-any-field";
constexpr char kPbUnpackFromAnyField[] = "pb-unpack-from-any-field";
constexpr char kPbDebugPrintAllProtos[] = "pb-debug-print-all-protos";

using ::google::protobuf::Descriptor;
using ::google::protobuf::DescriptorPool;
using ::google::protobuf::FieldDescriptor;
using ::google::protobuf::OneofDescriptor;
using ::google::protobuf::Reflection;

template <typename T, typename = std::enable_if_t<std::is_floating_point_v<T>>>
absl::StatusOr<T> CheckAndGetFloat(const google::protobuf::Message* msg,
                                   const FieldDescriptor* field,
                                   const Value& value) {
  if (value.GetValueType() != Value::Type::kFloat) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Trying to set %s value %s::%s from non-float (%s)",
        msg->GetDescriptor()->name(), field->type_name(), field->name(),
        Value::TypeToString(value.GetValueType())));
  }
  double double_value = value.GetFloat().value();
  if (double_value < std::numeric_limits<T>::lowest() ||
      double_value > std::numeric_limits<T>::max()) {
    return absl::OutOfRangeError(
        absl::StrFormat("Trying to set %s value %s::%s to %f (out of range)",
                        msg->GetDescriptor()->name(), field->type_name(),
                        field->name(), double_value));
  }
  return static_cast<T>(double_value);
}

template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
absl::StatusOr<T> CheckAndGetInt(const google::protobuf::Message* msg,
                                 const FieldDescriptor* field,
                                 const Value& value) {
  if (value.GetValueType() != Value::Type::kInteger) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Trying to set %s value %s::%s from non-integer (%s)",
        msg->GetDescriptor()->name(), field->type_name(), field->name(),
        Value::TypeToString(value.GetValueType())));
  }
  int64_t int_value = value.GetInteger().value();
  if (int_value < std::numeric_limits<T>::min() ||
      int_value > std::numeric_limits<T>::max()) {
    return absl::OutOfRangeError(
        absl::StrFormat("Trying to set %s value %s::%s to %d (out of range)",
                        msg->GetDescriptor()->name(), field->type_name(),
                        field->name(), int_value));
  }
  return static_cast<T>(int_value);
}

// Splits a proto path into stem/prefix and last field part.
// foo -> ("", "foo")
// foo.bar -> ("foo", "bar")
// foo.bar[5].baz -> ("foo.bar[5]", "baz")
std::pair<absl::string_view, absl::string_view> SplitPathStemAndField(
    absl::string_view path) {
  absl::string_view::size_type pos = path.find_last_of(kProtoPathSeparator);
  if (pos == absl::string_view::npos) {
    return std::make_pair(path.substr(0, 0), path);
  }

  return std::make_pair(path.substr(0, pos),
                        absl::ClippedSubstr(path, pos + 1));
}

class AnyTypeDescriptorFinder : public google::protobuf::TextFormat::Finder {
 public:
  explicit AnyTypeDescriptorFinder(const google::protobuf::DescriptorPool* pool)
      : pool_(pool) {}

  const google::protobuf::Descriptor* FindAnyType(
      const google::protobuf::Message& message, const std::string& prefix,
      const std::string& name) const override {
    return pool_->FindMessageTypeByName(name);
  }

 private:
  const google::protobuf::DescriptorPool* pool_;
};

absl::Status InvalidArgumentErrorWithSymbol(absl::string_view message,
                                            absl::string_view symbol) {
  absl::Status s = absl::InvalidArgumentError(message);
  google::protobuf::StringValue sym_name;
  sym_name.set_value(symbol);
  s.SetPayload(
      absl::StrCat(intrinsic::kTypeUrlPrefix,
                   google::protobuf::StringValue::GetDescriptor()->full_name()),
      sym_name.SerializeAsCord());
  return s;
}

// Outputs a nice type name for the message represented by descriptor.
//
// In the context of assignments results of CEL expressions are packed into a
// wrapper proto, e.g., google.protobuf.Int64Value even if the expression comes
// from an int64 field and is being assigned to an int64 field. Thus for all
// protos that are used as wrappers give their proto field type name and not the
// type of the wrapper proto.
// Nevertheless consider that the incoming message need not be a wrapper proto
// as a user could accidentally assign a custom message to a built-in field. In
// this case the message type name is provided.
std::string ProtoTypeNameFromMessage(
    const google::protobuf::Descriptor* descriptor) {
  constexpr absl::string_view known_types[] = {
      "google.protobuf.Int32Value", "google.protobuf.UInt32Value",
      "google.protobuf.Int64Value", "google.protobuf.UInt64Value",
      "google.protobuf.FloatValue", "google.protobuf.DoubleValue",
      "google.protobuf.BoolValue",  "google.protobuf.StringValue",
      "google.protobuf.BytesValue"};
  if (absl::c_contains(known_types, descriptor->full_name())) {
    return absl::AsciiStrToLower(
        absl::StripSuffix(descriptor->name(), "Value"));
  }

  if (descriptor->full_name() == "intrinsic_proto.executive.AnyList") {
    return "repeated field";
  }

  return std::string(descriptor->name());
}

absl::string_view ProtoTypeNameFromField(
    const google::protobuf::FieldDescriptor* field
        ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    return field->type_name();
  }
  return field->message_type()->name();
}

// Convert any proto message to string. If max_lines > 0 then at most max_lines
// are output and if max_string_field_length > 0 string fields are limited to
// max_string_field_length.
std::string MessageToString(
    const google::protobuf::Message& msg, int64_t max_lines,
    int64_t max_string_field_length,
    const google::protobuf::TextFormat::Finder* absl_nullable any_finder) {
  google::protobuf::TextFormat::Printer printer;
  printer.SetExpandAny(true);
  printer.SetUseShortRepeatedPrimitives(true);
  printer.SetTruncateStringFieldLongerThan(max_string_field_length);
  if (any_finder != nullptr) {
    printer.SetFinder(any_finder);
  }

  std::string debug_string;
  static_cast<void>(
      printer.PrintToString(msg, &debug_string));  // TODO: Handle return value
  if (max_lines > 0) {
    std::vector<absl::string_view> debug_lines =
        absl::StrSplit(debug_string, '\n');

    absl::Span<absl::string_view> debug_max_lines =
        absl::Span<absl::string_view>(debug_lines).subspan(0, max_lines);
    return absl::StrJoin(debug_max_lines, "\n");
  }
  return debug_string;
}

std::string GenerateAssignmentDebugMessage(
    const google::protobuf::Descriptor* absl_nullable target_message_descriptor,
    const google::protobuf::Message* absl_nonnull source_message,
    const google::protobuf::Descriptor* absl_nonnull
        target_full_message_descriptor,
    absl::string_view target_full_path) {
  std::string target_message_output;
  if (target_message_descriptor != nullptr) {
    target_message_output =
        absl::StrFormat("Type of field to assign: '%s'. ",
                        target_message_descriptor->full_name());
  }
  return absl::StrFormat(
      "Assignment target message: '%s'. Field path in message: '%s'. %sType of "
      "message to assign: '%s'. Message to assign: '%s'",
      target_full_message_descriptor->full_name(), target_full_path,
      target_message_output, source_message->GetDescriptor()->full_name(),
      MessageToString(*source_message, /*max_lines=*/20,
                      /*max_string_field_length=*/100, nullptr));
}

absl::Status GenerateAssignmentTypeError(
    uint32_t code,
    const google::protobuf::Descriptor* absl_nonnull target_message_descriptor,
    const google::protobuf::Message* absl_nonnull source_message,
    const google::protobuf::Descriptor* absl_nonnull
        target_full_message_descriptor,
    absl::string_view target_full_path,
    absl::string_view assigned_source_expression) {
  return CreateStatus(
      code,
      absl::StrFormat(
          "Cannot set field of type '%s' from type '%s' at field path '%s' in "
          "'%s' while trying to assign expression '%s'",
          ProtoTypeNameFromMessage(target_message_descriptor),
          ProtoTypeNameFromMessage(source_message->GetDescriptor()),
          target_full_path, target_full_message_descriptor->name(),
          assigned_source_expression),
      absl::StatusCode::kInvalidArgument,
      {.debug_message = GenerateAssignmentDebugMessage(
           target_message_descriptor, source_message,
           target_full_message_descriptor, target_full_path)});
}

template <typename M, typename = std::enable_if_t<
                          std::is_base_of_v<google::protobuf::Message, M>>>
absl::StatusOr<M> ConvertMessageValueTo(
    const google::protobuf::Message* value,
    const google::protobuf::Descriptor* target_full_message_descriptor,
    absl::string_view target_full_path,
    absl::string_view assigned_source_expression) {
  if (value->GetDescriptor()->full_name() != M::descriptor()->full_name()) {
    return GenerateAssignmentTypeError(
        15901, M::descriptor(), value, target_full_message_descriptor,
        target_full_path, assigned_source_expression);
  }
  M converted_value;
  if (!converted_value.ParseFromString(value->SerializeAsCord())) {
    return GenerateAssignmentTypeError(
        15902, M::descriptor(), value, target_full_message_descriptor,
        target_full_path, assigned_source_expression);
  }
  return converted_value;
}

template <typename T1, typename T2>
concept SameNumericCategory =
    (std::is_integral_v<T1> && std::is_integral_v<T2>) ||
    (std::is_floating_point_v<T1> && std::is_floating_point_v<T2>);

template <typename Dest, typename Src>
  requires SameNumericCategory<Dest, Src>
bool IsNumberAssignable(const Src& value) {
  if constexpr (std::is_integral_v<Src>) {
    return std::cmp_greater_equal(value, std::numeric_limits<Dest>::lowest()) &&
           std::cmp_less_equal(value, std::numeric_limits<Dest>::max());
  } else {
    return std::isnan(value) || std::isinf(value) ||
           (value >= std::numeric_limits<Dest>::lowest() &&
            value <= std::numeric_limits<Dest>::max());
  }
}

template <typename T, typename M,
          typename = std::enable_if_t<
              (std::is_integral_v<T> &&
               (std::is_same_v<std::remove_reference_t<M>,
                               google::protobuf::Int32Value> ||
                std::is_same_v<std::remove_reference_t<M>,
                               google::protobuf::UInt32Value> ||
                std::is_same_v<std::remove_reference_t<M>,
                               google::protobuf::Int64Value> ||
                std::is_same_v<std::remove_reference_t<M>,
                               google::protobuf::UInt64Value>)) ||
              (std::is_floating_point_v<T> &&
               (std::is_same_v<std::remove_reference_t<M>,
                               google::protobuf::FloatValue> ||
                std::is_same_v<std::remove_reference_t<M>,
                               google::protobuf::DoubleValue>))>>
absl::StatusOr<T> CheckAndGetNumberFromProtoImpl(
    const google::protobuf::Message* msg, const FieldDescriptor* field,
    const google::protobuf::Message* value_msg,
    const google::protobuf::Descriptor* target_full_message_descriptor,
    absl::string_view target_full_path,
    absl::string_view assigned_source_expression) {
  INTR_ASSIGN_OR_RETURN(auto value,
                        ConvertMessageValueTo<std::remove_reference_t<M>>(
                            value_msg, target_full_message_descriptor,
                            target_full_path, assigned_source_expression));
  if (!IsNumberAssignable<T>(value.value())) {
    return CreateStatus(
        15903,
        absl::StrFormat(
            "Value %s is out of %s range. Cannot set field from type '%s' at "
            "field path '%s' in '%s' while trying to assign expression '%s'",
            std::to_string(value.value()), field->type_name(),
            ProtoTypeNameFromMessage(value_msg->GetDescriptor()),
            target_full_path, target_full_message_descriptor->name(),
            assigned_source_expression),
        absl::StatusCode::kOutOfRange,
        {.debug_message = GenerateAssignmentDebugMessage(
             nullptr, value_msg, target_full_message_descriptor,
             target_full_path)});
  }
  return static_cast<T>(value.value());
}

template <typename T, typename TupleType, size_t... I>
absl::StatusOr<T> CheckAndGetNumberFromProtoTryTypes(
    const google::protobuf::Message* msg, const FieldDescriptor* field,
    const google::protobuf::Message* value_msg,
    std::index_sequence<I...> /* unused */,
    const google::protobuf::Descriptor* target_full_message_descriptor,
    absl::string_view target_full_path,
    absl::string_view assigned_source_expression) {
  bool converted_ok = false;
  std::vector<intrinsic_proto::status::ExtendedStatus> errors;
  T value;
  TupleType args;
  (
      [&] {
        if (converted_ok) return;
        using M = decltype(std::get<I>(args));
        absl::StatusOr<T> v = CheckAndGetNumberFromProtoImpl<T, M>(
            msg, field, value_msg, target_full_message_descriptor,
            target_full_path, assigned_source_expression);
        if (v.ok()) {
          converted_ok = true;
          value = *v;
        } else {
          std::optional<intrinsic_proto::status::ExtendedStatus> es =
              GetExtendedStatus(v);
          if (es.has_value()) {
            errors.push_back(std::move(*es));
          } else {
            errors.push_back(CreateExtendedStatus(
                15900,  // should never happen as called functions provide ES
                absl::StrFormat("Unexpected error during assignment: %s",
                                v.status().message()),
                {.debug_message = GenerateAssignmentDebugMessage(
                     nullptr, value_msg, target_full_message_descriptor,
                     target_full_path)}));
          }
        }
      }(),
      ...);
  if (!converted_ok) {
    std::vector<std::string> accepted_types;
    (
        [&] {
          accepted_types.push_back(ProtoTypeNameFromMessage(
              std::remove_reference_t<decltype(std::get<I>(
                  args))>::descriptor()));
        }(),
        ...);
    // If all errors are 15901 (type conversion) then the value_msg just wasn't
    // any in TupleType and we'll give a matching assignment error
    // Otherwise: value_msg is a matching message, but there was another error
    // during assignment. Forward that.
    for (const intrinsic_proto::status::ExtendedStatus& error : errors) {
      if (error.status_code().code() != 15901) {
        absl::Status error_status =
            absl::InvalidArgumentError(error.user_report().message());
        error_status.SetPayload(
            AddTypeUrlPrefix<intrinsic_proto::status::ExtendedStatus>(),
            error.SerializeAsCord());
        return error_status;
      }
    }

    return CreateStatus(
        15901,
        absl::StrFormat(
            "Cannot set field of type '%s' from type '%s' at field path '%s' "
            "in '%s' while trying to assign expression '%s'. Accepted types "
            "are: %s",
            field->type_name(),
            ProtoTypeNameFromMessage(value_msg->GetDescriptor()),
            target_full_path, target_full_message_descriptor->name(),
            assigned_source_expression, absl::StrJoin(accepted_types, ", ")),
        absl::StatusCode::kInvalidArgument,
        {.debug_message = GenerateAssignmentDebugMessage(
             nullptr, value_msg, target_full_message_descriptor,
             target_full_path)});
  }
  return value;
}

template <typename T, typename... MessageTypes>
absl::StatusOr<T> CheckAndGetNumberFromProto(
    const google::protobuf::Message* msg, const FieldDescriptor* field,
    const google::protobuf::Message* value_msg,
    const google::protobuf::Descriptor* target_full_message_descriptor,
    absl::string_view target_full_path,
    absl::string_view assigned_source_expression) {
  return CheckAndGetNumberFromProtoTryTypes<
      T, std::tuple<std::decay_t<MessageTypes>...>>(
      msg, field, value_msg,
      std::index_sequence_for<std::decay_t<MessageTypes>...>{},
      target_full_message_descriptor, target_full_path,
      assigned_source_expression);
}

}  // namespace

absl::StatusOr<std::string_view> GetAnyTypeNameFromTypeUrl(
    std::string_view type_url ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  size_t slash = type_url.rfind('/');
  std::string_view any_type_name;
  if (slash != std::string::npos) {
    any_type_name = type_url.substr(slash + 1);
  }
  if (any_type_name.empty()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Could not extract type information from any. Type URL is: '%s'",
        type_url));
  }
  return any_type_name;
}

// Extracts the message from the type_url and removes `type.googleapis.com/`
// prefix Modifies type_url from
// `type.googleapis.com/intrinsic_proto.test_skill.Parameter` to
// `intrinsic_proto.test_skill.Parameter`
absl::StatusOr<std::string_view> GetAnyTypeNameFromMessage(
    const google::protobuf::Any& any_proto ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return GetAnyTypeNameFromTypeUrl(any_proto.type_url());
}

ProtobufManager::DescriptorPoolInfoInternal::DescriptorPoolInfoInternal(
    google::protobuf::FileDescriptorSet file_descriptor_set,
    absl_nonnull std::unique_ptr<google::protobuf::DescriptorDatabase> db,
    absl_nonnull std::unique_ptr<google::protobuf::DescriptorPool> pool,
    absl_nonnull std::unique_ptr<google::protobuf::MessageFactory> factory,
    absl::string_view display_name, absl::string_view type_url_prefix,
    std::optional<absl::string_view> operation_name)
    : file_descriptor_set(file_descriptor_set),
      descriptor_db(std::move(db)),
      display_name(display_name),
      type_url_prefix(type_url_prefix),
      operation_name(operation_name),
      owned_descriptor_pool(std::move(pool)),
      owned_message_factory(std::move(factory)) {
  descriptor_pool = owned_descriptor_pool.get();
  message_factory = owned_message_factory.get();
}

ProtobufManager::DescriptorPoolInfoInternal::DescriptorPoolInfoInternal(
    const google::protobuf::DescriptorPool* absl_nonnull pool,
    google::protobuf::MessageFactory* absl_nonnull factory,
    absl::string_view display_name, absl::string_view type_url_prefix)
    : descriptor_pool(pool),
      message_factory(factory),
      display_name(display_name),
      type_url_prefix(type_url_prefix) {}

ProtobufManager::ProtobufManager(Environment* env) : env_(env) {
  CHECK_OK(AddGeneratedDescriptorPool());
  CHECK_OK(AddStandardMessagesDescriptorPool());
  CHECK_NE(generated_pool_info_, nullptr);
}

ProtobufManager::~ProtobufManager() ABSL_LOCKS_EXCLUDED(env_->mutex())
    ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  if (env_ != nullptr) {
    absl::MutexLock lock(env_->mutex());
    UnregisterFunctions();
  }
  // We must delete protos before pools, there could be DynamicMessages
  // generated for a pool whose custom delete operator needs access to
  // the DynamicMessageFactory.
  absl::MutexLock lock(protos_mutex_);
  protos_.clear();
  pools_.clear();
}

absl::StatusOr<std::unique_ptr<ProtobufManager>> ProtobufManager::Create(
    Environment* environment) {
  auto pm = absl::WrapUnique(new ProtobufManager(environment));
  absl::MutexLock lock(pm->env_->mutex());
  INTR_RETURN_IF_ERROR(pm->RegisterFunctions());
  return pm;
}

absl::Status ProtobufManager::RegisterFunctions() {
  auto return_error = [](const absl::Status& status) -> Value {
    switch (status.code()) {
      case absl::StatusCode::kOutOfRange:
        return Symbol("OUT-OF-RANGE");
      case absl::StatusCode::kNotFound:
        return Symbol("MISSING-FIELD");
      default:
        return Symbol("INVALID-FIELD");
    }
  };
  auto return_error_sym = [](const absl::Status& status) -> Symbol {
    switch (status.code()) {
      case absl::StatusCode::kOutOfRange:
        return Symbol("OUT-OF-RANGE");
      case absl::StatusCode::kNotFound:
        return Symbol("MISSING-FIELD");
      case absl::StatusCode::kInvalidArgument: {
        std::optional<absl::Cord> symbol = status.GetPayload(absl::StrCat(
            intrinsic::kTypeUrlPrefix,
            google::protobuf::StringValue::GetDescriptor()->full_name()));
        if (symbol) {
          google::protobuf::StringValue sym_name;
          static_cast<void>(
              sym_name.ParseFromString(*symbol));  // TODO: Handle return value
          return Symbol(sym_name.value());
        }
      }
        ABSL_FALLTHROUGH_INTENDED;
      default:
        LOG(ERROR) << status;
        return Symbol("INVALID-FIELD");
    }
  };
  auto return_clips_result = [this](const absl::Status& status) -> Values {
    std::optional<ExtendedStatus> es = GetExtendedStatus(status);
    if (!es.has_value()) {
      es.emplace(CreateExtendedStatus(13699, status.ToString()));
    }
    if (es->severity() == ExtendedStatus::DEFAULT) {
      // As this happens from an error handle set the severity accordingly if it
      // hasn't been labeled differently
      es->set_severity(ExtendedStatus::ERROR);
    }
    ProtoMessageId es_proto_id = AddGeneratedProto(std::move(*es));
    return {Symbol::False(), Value(es_proto_id.value())};
  };

  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbCreate,
      std::function([this](const std::string& message_type) -> int64_t {
        INTR_ASSIGN_OR_RETURN(ProtoMessageId msg_id,
                              CreateBuiltInProto(message_type),
                              _.LogError().With(Return(kInvalidId.value())));
        return msg_id.value();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbClone, std::function([this](int64_t msg_id) {
        absl::MutexLock lock(protos_mutex_);
        INTR_ASSIGN_OR_RETURN(const ManagedProto& managed_proto,
                              GetManagedProto(ProtoMessageId(msg_id)),
                              _.LogError().With(Return(kInvalidId.value())));
        google::protobuf::Message* clone = managed_proto->New();
        clone->CopyFrom(*managed_proto);
        INTR_ASSIGN_OR_RETURN(
            clips::ProtoMessageId cloned_id,
            AddProto(absl::WrapUnique(clone), managed_proto.pool_info),
            _.LogError().With(Return(kInvalidId.value())));
        return cloned_id.value();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbCopy,
      std::function([this](int64_t src_msg_id, int64_t dst_msg_id) -> Value {
        INTR_RETURN_IF_ERROR(
            CopyProto(ProtoMessageId(src_msg_id), ProtoMessageId(dst_msg_id)))
            .LogError()
            .With(Return(Symbol::False()));
        return Symbol::True();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbRemove,
      std::function([this](int64_t id) { RemoveProto(ProtoMessageId(id)); })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbIsDefault,
      std::function([this, return_error_sym](int64_t msg_id) -> Value {
        INTR_ASSIGN_OR_RETURN(Symbol sym, IsDefault(ProtoMessageId(msg_id)),
                              _.LogError().With(return_error_sym));
        return sym;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetTypeName, std::function([this](int64_t msg_id) -> Value {
        INTR_ASSIGN_OR_RETURN(const google::protobuf::Message* proto,
                              GetProto(ProtoMessageId(msg_id)),
                              _.LogError().With(Return(Value(""))));
        return Value(proto->GetTypeName());
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetFieldValue,
      std::function([this, return_error](int64_t msg_id,
                                         const std::string& field_name) {
        INTR_ASSIGN_OR_RETURN(Value value,
                              GetFieldValue(ProtoMessageId(msg_id), field_name),
                              _.LogError().With(return_error));
        return value;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbIsRepeated,
      std::function([this, return_error_sym](int64_t msg_id,
                                             const std::string& field_path) {
        INTR_ASSIGN_OR_RETURN(Symbol sym,
                              IsRepeated(ProtoMessageId(msg_id), field_path),
                              _.LogError().With(return_error_sym));
        return sym;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbHasField,
      std::function([this, return_error_sym](int64_t msg_id,
                                             const std::string& field_name) {
        INTR_ASSIGN_OR_RETURN(
            Symbol sym, HasField(ProtoMessageId(msg_id), field_name),
            _.With([return_error_sym](const absl::Status& status) {
              if (status.code() == absl::StatusCode::kNotFound) {
                return Symbol::False();
              } else {
                return return_error_sym(status);
              }
            }));
        return sym;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbWhichOneof,
      std::function([this, return_error_sym](int64_t msg_id,
                                             const std::string& field_name) {
        INTR_ASSIGN_OR_RETURN(Symbol sym,
                              WhichOneof(ProtoMessageId(msg_id), field_name),
                              _.LogError().With(return_error_sym));
        return sym;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetRepeatedFieldValues,
      std::function([this](int64_t msg_id, const std::string& field_name) {
        INTR_ASSIGN_OR_RETURN(
            Values values,
            GetRepeatedFieldValues(ProtoMessageId(msg_id), field_name),
            _.LogError().With(Return(Values{})));
        return values;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetRepeatedFieldLength,
      std::function(
          [this](int64_t msg_id, const std::string& field_name) -> int64_t {
            INTR_ASSIGN_OR_RETURN(
                int64_t length,
                GetRepeatedFieldLength(ProtoMessageId(msg_id), field_name),
                _.LogError().With(Return(0)));
            return length;
          })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetMapFieldValue,
      std::function([this, return_error](int64_t msg_id,
                                         const std::string& field_name,
                                         const Value& key) {
        INTR_ASSIGN_OR_RETURN(
            Value value,
            GetMapFieldValue(ProtoMessageId(msg_id), field_name, key),
            _.LogError().With(return_error));
        return value;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetMapFieldKeys,
      std::function([this](int64_t msg_id, const std::string& field_name) {
        INTR_ASSIGN_OR_RETURN(
            Values values, GetMapFieldKeys(ProtoMessageId(msg_id), field_name),
            _.LogError().With(Return(Values())));
        return values;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbSetFieldValue,
      std::function([this](int64_t msg_id, const std::string& field_path,
                           const Value& value) -> Value {
        INTR_RETURN_IF_ERROR(
            SetFieldValue(ProtoMessageId(msg_id), field_path, value))
            .LogError()
            .With(Return(Symbol::False()));
        return Symbol::True();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbSetFieldFromProto,
      std::function([this](int64_t msg_id, const std::string& field_path,
                           int64_t descriptor_pool_id,
                           int64_t value_msg_id) -> Value {
        INTR_RETURN_IF_ERROR(
            SetFieldFromProto(ProtoMessageId(msg_id), field_path,
                              DescriptorPoolId(descriptor_pool_id),
                              ProtoMessageId(value_msg_id)))
            .LogError()
            .With(Return(Symbol::False()));
        return Symbol::True();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbClearField,
      std::function(
          [this](int64_t msg_id, const std::string& field_path) -> Value {
            INTR_RETURN_IF_ERROR(ClearField(ProtoMessageId(msg_id), field_path))
                .LogError()
                .With(Return(Symbol::False()));
            return Symbol::True();
          })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbCopyBytesFieldValue,
      std::function([this](int64_t src_msg_id,
                           const std::string& src_field_path,
                           int64_t dst_msg_id,
                           const std::string& dst_field_path) -> Value {
        INTR_RETURN_IF_ERROR(
            CopyBytesValue(ProtoMessageId(src_msg_id), src_field_path,
                           ProtoMessageId(dst_msg_id), dst_field_path))
            .LogError()
            .With(Return(Symbol::False()));
        return Symbol::True();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbBytesSizeValue,
      std::function(
          [this](int64_t msg_id, const std::string& field_path) -> int64_t {
            INTR_ASSIGN_OR_RETURN(int64_t size,
                                  BytesSize(ProtoMessageId(msg_id), field_path),
                                  _.LogError().With(Return(0)));
            return size;
          })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbSetRepeatedFieldValues,
      std::function([this](int64_t msg_id, const std::string& field_path,
                           const Values& values) -> Value {
        INTR_RETURN_IF_ERROR(
            SetRepeatedFieldValues(ProtoMessageId(msg_id), field_path, values))
            .LogError()
            .With(Return(Symbol::False()));
        return Symbol::True();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbIsFieldValueInSameOneofOption,
      std::function([this](int64_t msg_id,
                           const std::string& field_name) -> Values {
        absl::StatusOr<bool> is_in_same_option =
            IsFieldValueInSameOneofOption(ProtoMessageId(msg_id), field_name);
        if (!is_in_same_option.ok()) {
          return clips::Values{
              Symbol::False(),
              clips::Value(is_in_same_option.status().message())};
        }
        return {Symbol::True(),
                *is_in_same_option ? Symbol::True() : Symbol::False()};
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbSetMapFieldValue,
      std::function([this](int64_t msg_id, const std::string& field_name,
                           const Value& key, const Value& value) -> Value {
        INTR_RETURN_IF_ERROR(
            SetMapFieldValue(ProtoMessageId(msg_id), field_name, key, value))
            .LogError()
            .With(Return(Symbol::False()));
        return Symbol::True();
      })));

  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbToString, std::function([this](int64_t msg_id) {
        INTR_ASSIGN_OR_RETURN(std::string proto_str,
                              ToString(ProtoMessageId(msg_id)),
                              _.LogError().With(Return(std::string())));
        return proto_str;
      })));

  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbToStringWithPool,
      std::function([this](int64_t msg_id, int64_t pool_id, int64_t max_lines) {
        INTR_ASSIGN_OR_RETURN(
            std::string proto_str,
            ToStringWithPool(ProtoMessageId(msg_id), DescriptorPoolId(pool_id),
                             max_lines),
            _.LogError().With(Return(std::string())));
        return proto_str;
      })));

  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbAddDescriptorPool,
      std::function([this](int64_t msg_id, const std::string& display_name,
                           const std::string& type_url_prefix,
                           const std::string& operation_name) -> int64_t {
        INTR_ASSIGN_OR_RETURN(
            DescriptorPoolId descriptor_pool_id,
            AddDescriptorPool(ProtoMessageId(msg_id), display_name,
                              type_url_prefix, operation_name),
            _.LogError().With(
                Return(ProtobufManager::kInvalidDescriptorPoolId.value())));
        return descriptor_pool_id.value();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbRemoveDescriptorPool, std::function([this](int64_t pool_id) {
        RemoveDescriptorPool(DescriptorPoolId(pool_id));
      })));
  INTR_RETURN_IF_ERROR(
      RegisterFunction(kPbRemoveOperationProtosAndPools,
                       std::function([this](const std::string& operation_name) {
                         RemoveOperationProtosAndPools(operation_name);
                       })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbCastFromAnyWithPool,
      std::function([this, return_clips_result](
                        int64_t msg_id, int64_t pool_id,
                        const std::string& expected_message_type) -> Values {
        INTR_ASSIGN_OR_RETURN(ProtoMessageId casted_msg_id,
                              CastFromAnyWithPool(ProtoMessageId(msg_id),
                                                  DescriptorPoolId(pool_id),
                                                  expected_message_type),
                              _.LogWarning().With(return_clips_result));
        return {Symbol::True(), Value(casted_msg_id.value())};
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbUnpackFromAnyField,
      std::function([this](int64_t msg_id, const std::string& field_path,
                           int64_t pool_id) -> int64_t {
        INTR_ASSIGN_OR_RETURN(
            ProtoMessageId casted_msg_id,
            UnpackFromAnyField(ProtoMessageId(msg_id), field_path,
                               DescriptorPoolId(pool_id)),
            _.LogError().With(Return(kInvalidId.value())));
        return casted_msg_id.value();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbCastToAny, std::function([this](int64_t msg_id) -> int64_t {
        INTR_ASSIGN_OR_RETURN(ProtoMessageId any_id,
                              CastToAnyClips(ProtoMessageId(msg_id)),
                              _.LogError().With(Return(kInvalidId.value())));
        return any_id.value();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbPackToAnyField,
      std::function([this](int64_t msg_id, const std::string& field_path,
                           int64_t value_msg_id) -> Value {
        INTR_RETURN_IF_ERROR(PackToAnyField(ProtoMessageId(msg_id), field_path,
                                            ProtoMessageId(value_msg_id)))
            .LogError()
            .With(Return(Symbol::False()));
        return Symbol::True();
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetAnyTypeName, std::function([this](int64_t msg_id) -> std::string {
        INTR_ASSIGN_OR_RETURN(std::string type_name,
                              GetAnyTypeName(ProtoMessageId(msg_id)),
                              _.LogError().With(Return("")));
        return type_name;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetAnyFieldTypeName,
      std::function(
          [this](int64_t msg_id, const std::string& field_path) -> std::string {
            INTR_ASSIGN_OR_RETURN(
                std::string type_name,
                GetAnyFieldTypeName(ProtoMessageId(msg_id), field_path),
                _.LogError().With(Return("")));
            return type_name;
          })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbGetAssetIdFromTypeUrl,
      std::function([this](const std::string& type_url) -> clips::Values {
        absl::StatusOr<std::string> skill_id = GetAssetIdFromTypeUrl(type_url);
        if (!skill_id.ok()) {
          return {clips::Symbol::False(),
                  clips::Value(skill_id.status().message())};
        }
        return {clips::Symbol::True(), clips::Value(*skill_id)};
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kPbDebugPrintAllProtos,
      std::function([this]() -> void { DebugPrintAllProtos(); })));

  return absl::OkStatus();
}

void ProtobufManager::UnregisterFunctions() {
  for (const std::string& f : functions_) {
    if (absl::Status s(env_->RemoveFunction(f)); !s.ok()) {
      LOG(ERROR) << "Failed to remove " << f << ": " << s;
    }
  }
  functions_.clear();
}

ProtoMessageId ProtobufManager::GenerateNextProtoId() {
  ProtoMessageId new_id = kInvalidId;
  do {
    // shift to fit into int64
    new_id = ProtoMessageId(absl::Uniform<uint64_t>(id_random_generator_) >> 1);
  } while (new_id == kInvalidId || protos_.find(new_id) != protos_.end());
  return new_id;
}

DescriptorPoolId ProtobufManager::GenerateNextDescriptorPoolId() {
  DescriptorPoolId new_id = kInvalidDescriptorPoolId;
  do {
    // shift to fit into int64
    new_id =
        DescriptorPoolId(absl::Uniform<uint64_t>(id_random_generator_) >> 1);
  } while (new_id == kInvalidDescriptorPoolId ||
           pools_.find(new_id) != pools_.end());
  return new_id;
}

absl::StatusOr<ProtoMessageId> ProtobufManager::AddProto(
    absl_nonnull std::unique_ptr<google::protobuf::Message>&& proto,
    DescriptorPoolId pool_id) {
  auto pool_info = pools_.find(pool_id);
  if (pool_info == pools_.end()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Adding proto with unknown pool id: %d", pool_id.value()));
  }
  absl::MutexLock lock(protos_mutex_);
  return AddProto(std::move(proto), pool_info->second);
}

absl::StatusOr<ProtoMessageId> ProtobufManager::AddProto(
    absl_nonnull std::unique_ptr<google::protobuf::Message>&& proto,
    absl_nonnull std::shared_ptr<DescriptorPoolInfoInternal> pool_info)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_) {
  const ProtoMessageId next_proto_id = GenerateNextProtoId();
  if (pool_info == nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Adding proto with null pool: %s", proto->DebugString()));
  } else {
    if (proto->GetDescriptor()->file()->pool() != pool_info->descriptor_pool) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Trying to add proto from pool '%s' but the message "
                          "was from another pool: %s",
                          pool_info->display_name, proto->DebugString()));
    }
  }
  protos_.try_emplace(next_proto_id, std::move(proto), std::move(pool_info));
  return next_proto_id;
}

absl::StatusOr<ProtoMessageId> ProtobufManager::AddProto(
    const google::protobuf::Message& proto,
    absl_nonnull std::shared_ptr<DescriptorPoolInfoInternal> pool_info)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_) {
  google::protobuf::Message* new_proto = proto.New();
  new_proto->CopyFrom(proto);
  return AddProto(absl::WrapUnique(new_proto), std::move(pool_info));
}

void ProtobufManager::RemoveProto(ProtoMessageId id)
    ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  protos_.erase(id);
}

void ProtobufManager::RemoveAllProtos() ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  protos_.clear();
}

absl::StatusOr<Symbol> ProtobufManager::IsDefault(ProtoMessageId id)
    ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  auto find_it = protos_.find(id);
  if (find_it == protos_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("Proto is unknown (internal id: %d)", id.value()));
  }
  const google::protobuf::Message* msg = find_it->second.get();
  std::unique_ptr<google::protobuf::Message> default_msg =
      absl::WrapUnique(msg->New());
  return google::protobuf::util::MessageDifferencer::Equals(*msg, *default_msg)
             ? Symbol::True()
             : Symbol::False();
}

absl::StatusOr<const ProtobufManager::ManagedProto&>
ProtobufManager::GetManagedProto(ProtoMessageId id) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_) {
  auto find_it = protos_.find(id);
  if (find_it == protos_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("Proto is unknown (internal id: %d)", id.value()));
  }
  return find_it->second;
}

absl::StatusOr<const google::protobuf::Message*> ProtobufManager::GetProto(
    ProtoMessageId id) const ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  INTR_ASSIGN_OR_RETURN(const ManagedProto& proto, GetManagedProto(id));
  return proto.get();
}

absl::StatusOr<google::protobuf::Message*> ProtobufManager::GetMutableProto(
    ProtoMessageId id) ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  auto find_it = protos_.find(id);
  if (find_it == protos_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("Proto is unknown (internal id: %d)", id.value()));
  }
  return find_it->second.get();
}

absl::StatusOr<DescriptorPoolId> ProtobufManager::FindPoolIdForProto(
    ProtoMessageId proto_id) ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  INTR_ASSIGN_OR_RETURN(const ManagedProto& proto, GetManagedProto(proto_id));
  for (const auto& [pool_id, pool_info] : pools_) {
    if (pool_info == proto.pool_info) {
      return pool_id;
    }
  }
  return absl::NotFoundError(absl::StrFormat(
      "Could not find matching pool for proto %d", proto_id.value()));
}

absl::StatusOr<ProtoMessageId> ProtobufManager::CreateBuiltInProto(
    absl::string_view message_type_name) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> new_proto,
      CreateProtoInstanceFromDescriptorPool(
          message_type_name, generated_pool_info_->descriptor_pool,
          generated_pool_info_->message_factory));
  absl::MutexLock lock(protos_mutex_);
  return AddProto(std::move(new_proto), generated_pool_info_);
}

absl::Status ProtobufManager::CopyProto(ProtoMessageId src_msg_id,
                                        ProtoMessageId dst_msg_id) {
  INTR_ASSIGN_OR_RETURN(const google::protobuf::Message* src_msg,
                        GetProto(src_msg_id));
  INTR_ASSIGN_OR_RETURN(google::protobuf::Message * dst_msg,
                        GetMutableProto(dst_msg_id));
  if (src_msg->GetDescriptor()->full_name() !=
      dst_msg->GetDescriptor()->full_name()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Descriptor mismatch on copy, source '%s', destination '%s'",
        src_msg->GetDescriptor()->full_name(),
        dst_msg->GetDescriptor()->full_name()));
  }

  dst_msg->CopyFrom(*src_msg);
  return absl::OkStatus();
}

absl::StatusOr<Value> ProtobufManager::GetFieldValue(
    ProtoMessageId id, absl::string_view field_path) {
  absl::MutexLock lock(protos_mutex_);
  INTR_ASSIGN_OR_RETURN(const ManagedProto& msg, GetManagedProto(id));
  INTR_ASSIGN_OR_RETURN(FieldFromPath field,
                        intrinsic::executive::clips::GetFieldFromPath(
                            msg.get(), field_path, /*allow_index=*/true));

  const Reflection* refl = field.msg->GetReflection();

  if (field.descriptor->is_repeated()) {
    if (field.index.IsAbsent()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("Field %s::%s is a repeated field and requires an "
                          "index for single value access",
                          field.msg->GetDescriptor()->name(), field_path));
    }
    return InternalGetRepeatedFieldValue(*field.msg, field.descriptor,
                                         *field.index, msg.pool_info);
  }

  if (field.descriptor->type() == FieldDescriptor::TYPE_MESSAGE &&
      !refl->HasField(*field.msg, field.descriptor)) {
    return absl::NotFoundError(
        absl::StrFormat("Field %s::%s not set",
                        field.msg->GetDescriptor()->name(), field_path));
  }

  return InternalGetFieldValue(*field.msg, field.descriptor, msg.pool_info);
}

absl::StatusOr<int64_t> ProtobufManager::GetRepeatedFieldLength(
    ProtoMessageId id, absl::string_view field_path) {
  INTR_ASSIGN_OR_RETURN(
      FieldFromPath field,
      GetFieldFromPath(id, field_path, /*allow_index=*/false));

  if (!field.descriptor->is_repeated()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("%s::%s is not a repeated field",
                        field.msg->GetDescriptor()->name(), field_path));
  }

  return field.msg->GetReflection()->FieldSize(*field.msg, field.descriptor);
}

absl::StatusOr<Values> ProtobufManager::GetRepeatedFieldValues(
    ProtoMessageId id, absl::string_view field_path) {
  absl::MutexLock lock(protos_mutex_);
  INTR_ASSIGN_OR_RETURN(const ManagedProto& msg, GetManagedProto(id));
  INTR_ASSIGN_OR_RETURN(FieldFromPath field,
                        intrinsic::executive::clips::GetFieldFromPath(
                            msg.get(), field_path, /*allow_index=*/false));

  const Reflection* refl = field.msg->GetReflection();
  if (!field.descriptor->is_repeated()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("%s::%s is not a repeated field",
                        field.msg->GetDescriptor()->name(), field_path));
  }

  const int field_size = refl->FieldSize(*field.msg, field.descriptor);
  Values rv;
  rv.reserve(field_size);
  for (int i = 0; i < field_size; ++i) {
    switch (field.descriptor->type()) {
      case FieldDescriptor::TYPE_DOUBLE:
        rv.push_back(
            Value(refl->GetRepeatedDouble(*field.msg, field.descriptor, i)));
        break;
      case FieldDescriptor::TYPE_FLOAT:
        rv.push_back(
            Value(refl->GetRepeatedFloat(*field.msg, field.descriptor, i)));
        break;
      case FieldDescriptor::TYPE_SFIXED32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SINT32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_INT32:
        rv.push_back(
            Value(refl->GetRepeatedInt32(*field.msg, field.descriptor, i)));
        break;
      case FieldDescriptor::TYPE_UINT32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_FIXED32:
        rv.push_back(
            Value(refl->GetRepeatedUInt32(*field.msg, field.descriptor, i)));
        break;
      case FieldDescriptor::TYPE_INT64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SFIXED64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SINT64:
        rv.push_back(
            Value(refl->GetRepeatedInt64(*field.msg, field.descriptor, i)));
        break;
      case FieldDescriptor::TYPE_UINT64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_FIXED64:
        rv.push_back(Value(static_cast<int64_t>(
            refl->GetRepeatedUInt64(*field.msg, field.descriptor, i))));
        break;
      case FieldDescriptor::TYPE_BOOL:
        rv.push_back(Symbol(
            refl->GetRepeatedBool(*field.msg, field.descriptor, i) ? "TRUE"
                                                                   : "FALSE"));
        break;
      case FieldDescriptor::TYPE_STRING:
        rv.push_back(
            Value(refl->GetRepeatedString(*field.msg, field.descriptor, i)));
        break;
      case FieldDescriptor::TYPE_BYTES:
        rv.push_back(
            Value(refl->GetRepeatedString(*field.msg, field.descriptor, i)));
        break;
      case FieldDescriptor::TYPE_ENUM:
        rv.push_back(Symbol(
            refl->GetRepeatedEnum(*field.msg, field.descriptor, i)->name()));
        break;
      case FieldDescriptor::TYPE_MESSAGE: {
        INTR_ASSIGN_OR_RETURN(
            clips::ProtoMessageId field_msg_copy,
            AddProto(refl->GetRepeatedMessage(*field.msg, field.descriptor, i),
                     msg.pool_info));
        rv.push_back(Value(field_msg_copy.value()));
      } break;
      default:
        return absl::InternalError(
            absl::StrFormat("Unknown protobuf field %s::%s[%d] encountered",
                            field.msg->GetDescriptor()->name(), field_path, i));
    }
  }

  return rv;
}

absl::StatusOr<Value> ProtobufManager::GetMapFieldValue(
    ProtoMessageId id, absl::string_view field_path, const Value& key) {
  absl::MutexLock lock(protos_mutex_);
  INTR_ASSIGN_OR_RETURN(const ManagedProto& msg, GetManagedProto(id));
  INTR_ASSIGN_OR_RETURN(FieldFromPath field,
                        intrinsic::executive::clips::GetFieldFromPath(
                            msg.get(), field_path, /*allow_index=*/false));

  if (!field.descriptor->is_map()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Field %s::%s is not a map",
                        field.msg->GetDescriptor()->name(), field_path));
  }

  const Descriptor* map_desc = field.descriptor->message_type();
  const FieldDescriptor* key_field = map_desc->FindFieldByName("key");
  const FieldDescriptor* value_field = map_desc->FindFieldByName("value");
  if (key_field == nullptr || value_field == nullptr) {
    return absl::InternalError("Map entry missing field 'key' or 'value'");
  }

  const Reflection* msg_refl = field.msg->GetReflection();
  const int field_size = msg_refl->FieldSize(*field.msg, field.descriptor);
  for (int i = 0; i < field_size; ++i) {
    const google::protobuf::Message& map_entry =
        msg_refl->GetRepeatedMessage(*field.msg, field.descriptor, i);
    const Reflection* refl = map_entry.GetReflection();

    switch (key.GetValueType()) {
      case Value::Type::kInteger: {
        int64_t search_key = key.GetInteger().value();
        switch (key_field->type()) {
          case FieldDescriptor::TYPE_UINT32:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_FIXED32:
            if (search_key == refl->GetUInt32(map_entry, key_field)) {
              return InternalGetFieldValue(map_entry, value_field,
                                           msg.pool_info);
            }
            break;
          case FieldDescriptor::TYPE_INT32:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_SFIXED32:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_SINT32:
            if (search_key == refl->GetInt32(map_entry, key_field)) {
              return InternalGetFieldValue(map_entry, value_field,
                                           msg.pool_info);
            }
            break;
          case FieldDescriptor::TYPE_INT64:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_SFIXED64:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_SINT64:
            if (search_key == refl->GetInt64(map_entry, key_field)) {
              return InternalGetFieldValue(map_entry, value_field,
                                           msg.pool_info);
            }
            break;
          case FieldDescriptor::TYPE_UINT64:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_FIXED64: {
            uint64_t key_value = refl->GetUInt64(map_entry, key_field);
            if (key_value <= std::numeric_limits<int64_t>::max() &&
                key_value == search_key) {
              return InternalGetFieldValue(map_entry, value_field,
                                           msg.pool_info);
            }
            break;
          }

          default:
            return absl::InvalidArgumentError(absl::StrFormat(
                "Map %s::%s key type is not an integer type",
                field.msg->GetDescriptor()->name(), field_path));
        }
        break;
      }
      // enum not supported as map key, hence treat symbol as string
      case Value::Type::kSymbol:
        ABSL_FALLTHROUGH_INTENDED;
      case Value::Type::kString:
        if (key_field->type() != FieldDescriptor::TYPE_STRING) {
          return absl::InvalidArgumentError(
              absl::StrFormat("Map %s::%s key type is not STRING",
                              field.msg->GetDescriptor()->name(), field_path));
        }
        if (refl->GetString(map_entry, key_field) ==
            key.GetStringOrSymbol().value()) {
          return InternalGetFieldValue(map_entry, value_field, msg.pool_info);
        }
        break;
      default:
        return absl::InvalidArgumentError(
            absl::StrFormat("Map %s::%s key type must be STRING or INTEGER",
                            field.msg->GetDescriptor()->name(), field_path));
    }
  }

  return absl::NotFoundError(absl::StrFormat("Map %s::%s key %s not found",
                                             field.msg->GetDescriptor()->name(),
                                             field_path, key.ToString()));
}

absl::StatusOr<Values> ProtobufManager::GetMapFieldKeys(
    ProtoMessageId id, absl::string_view field_path) {
  INTR_ASSIGN_OR_RETURN(
      FieldFromPath field,
      GetFieldFromPath(id, field_path, /*allow_index=*/false));

  if (!field.descriptor->is_map()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Field %s::%s is not a map",
                        field.msg->GetDescriptor()->name(), field_path));
  }

  const Descriptor* map_desc = field.descriptor->message_type();
  const FieldDescriptor* key_field = map_desc->FindFieldByName("key");
  const FieldDescriptor* value_field = map_desc->FindFieldByName("value");
  if (key_field == nullptr || value_field == nullptr) {
    return absl::InternalError("Map entry missing field 'key' or 'value'");
  }

  const Reflection* msg_refl = field.msg->GetReflection();
  const int field_size = msg_refl->FieldSize(*field.msg, field.descriptor);
  Values keys;
  for (int i = 0; i < field_size; ++i) {
    const google::protobuf::Message& map_entry =
        msg_refl->GetRepeatedMessage(*field.msg, field.descriptor, i);
    const Reflection* refl = map_entry.GetReflection();

    switch (key_field->type()) {
      case FieldDescriptor::TYPE_UINT32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_FIXED32:
        keys.push_back(Value(refl->GetUInt32(map_entry, key_field)));
        break;
      case FieldDescriptor::TYPE_INT32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SFIXED32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SINT32:
        keys.push_back(Value(refl->GetInt32(map_entry, key_field)));
        break;
      case FieldDescriptor::TYPE_INT64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SFIXED64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SINT64:
        keys.push_back(Value(refl->GetInt64(map_entry, key_field)));
        break;
      case FieldDescriptor::TYPE_UINT64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_FIXED64: {
        uint64_t key_value = refl->GetUInt64(map_entry, key_field);
        if (key_value > std::numeric_limits<int64_t>::max()) {
          return absl::OutOfRangeError(absl::StrFormat(
              "Map %s::%s key %d is out of range",
              field.msg->GetDescriptor()->name(), field_path, key_value));
        }
        // CLIPS only supports int64 integers, hence check above and cast
        keys.push_back(Value(static_cast<int64_t>(key_value)));
        break;
      }
      case FieldDescriptor::TYPE_STRING:
        keys.push_back(Value(refl->GetString(map_entry, key_field)));
        break;

      default:
        return absl::InvalidArgumentError(
            absl::StrFormat("Map %s::%s key type is not supported",
                            field.msg->GetDescriptor()->name(), field_path));
    }
  }

  return keys;
}

absl::Status ProtobufManager::SetFieldValue(ProtoMessageId id,
                                            absl::string_view field_path,
                                            const Value& value) {
  INTR_ASSIGN_OR_RETURN(
      MutableFieldFromPath field,
      GetMutableFieldFromPath(id, field_path, /*allow_index=*/true));
  if (field.index.IsPresent()) {
    if (field.index.IsAdd()) {
      INTR_RETURN_IF_ERROR(InternalSetRepeatedFieldValues(
          field.msg, field.descriptor, {value}, false))
          << absl::StrFormat("Failed to add %s::%s[%i]",
                             field.msg->GetDescriptor()->name(), field_path,
                             *field.index);
    } else {
      INTR_RETURN_IF_ERROR(InternalSetRepeatedFieldValue(
          field.msg, field.descriptor, *field.index, value))
          << absl::StrFormat("Failed to set %s::%s",
                             field.msg->GetDescriptor()->name(), field_path);
    }
  } else {
    if (field.descriptor->is_repeated()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Cannot set repeated field '%s' from singular value", field_path));
    }
    INTR_RETURN_IF_ERROR(
        InternalSetFieldValue(field.msg, field.descriptor, value))
        << absl::StrFormat("Failed to set %s::%s",
                           field.msg->GetDescriptor()->name(), field_path);
  }
  return absl::OkStatus();
}

// Creates an error when during AnyList conversion to protos the descriptors for
// the i'th message from any_list_message are not found in the given pool_info.
// There are two calling contexts:
// - during assignment in which all fields, in particular
// assigned_message_descriptor are set.
// - during general conversion from AnyList to list of protos, in which only the
// expression the AnyList originated from is set
absl::Status CreateAnyListMessageNotInPoolError(
    const google::protobuf::Message* absl_nonnull any_list_message, int i,
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    absl::string_view message_i_type_name,
    absl::string_view assigned_field_type, absl::string_view source_expression,
    absl::string_view assign_path,
    const google::protobuf::Descriptor* absl_nullable
        assigned_message_descriptor) {
  if (assigned_message_descriptor != nullptr) {
    // Assignment context. Provide a detailed error.
    return CreateStatus(
        15906,
        absl::StrFormat(
            "Cannot set repeated field of type '%s' from the message at "
            "index %d of type '%s' as it does not exist in the descriptor "
            "pool '%s'. Failed to set field at field path '%s' in '%s' while "
            "trying to assign expression '%s'",
            assigned_field_type, i, message_i_type_name, pool_info.display_name,
            assign_path, assigned_message_descriptor->name(),
            source_expression),
        absl::StatusCode::kInvalidArgument,
        {.debug_message = GenerateAssignmentDebugMessage(
             nullptr, any_list_message, assigned_message_descriptor,
             assign_path)});
  }

  return CreateStatus(
      13705,
      absl::StrFormat(
          "Cannot create message of type '%s' from AnyList value "
          "at index %d as it does not exist in the descriptor pool '%s' while "
          "evaluating expression '%s'",
          message_i_type_name, i, pool_info.display_name, source_expression),
      absl::StatusCode::kInvalidArgument);
}

absl::Status CreateAnyListMessageCastError(
    const absl::Status& cast_status,
    const google::protobuf::Message* any_list_message, int i,
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    absl::string_view message_i_type_name,
    absl::string_view assigned_field_type, absl::string_view source_expression,
    absl::string_view assign_path,
    const google::protobuf::Descriptor* assigned_message_descriptor) {
  if (assigned_message_descriptor != nullptr) {
    return CreateStatus(
        15906,
        absl::StrFormat(
            "Cannot set repeated field of type '%s' from the message at "
            "index %d of type '%s' using the descriptor pool '%s'. Failed to "
            "set field at field path '%s' in '%s' while trying to assign "
            "expression '%s'. Error: %s",
            assigned_field_type, i, message_i_type_name, pool_info.display_name,
            assign_path, assigned_message_descriptor->name(), source_expression,
            cast_status.message()),
        absl::StatusCode::kInternal,
        {.debug_message = GenerateAssignmentDebugMessage(
             nullptr, any_list_message, assigned_message_descriptor,
             assign_path)});
  }
  return CreateStatus(
      13703,
      absl::StrFormat("Cannot cast from the message at index %d of type '%s' "
                      "using the descriptor pool '%s' while evaluating "
                      "expression '%s'. Error: %s",
                      i, message_i_type_name, pool_info.display_name,
                      source_expression, cast_status.message()),
      absl::StatusCode::kInternal);
}

absl::Status CreateAnyListParsingError(
    const google::protobuf::Message* any_list_message, int i,
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    absl::string_view message_i_type_name,
    absl::string_view assigned_field_type,
    absl::string_view assigned_source_expression, absl::string_view assign_path,
    const google::protobuf::Descriptor* assigned_message_descriptor) {
  if (assigned_message_descriptor != nullptr) {
    return CreateStatus(
        15902,
        absl::StrFormat(
            "Cannot set repeated field of type '%s' from the message at "
            "index %d of type '%s' using the descriptor pool '%s'. Failed to "
            "set field at field path '%s' in '%s' while trying to assign "
            "expression '%s' as the data could not be deserialized",
            assigned_field_type, i, message_i_type_name, pool_info.display_name,
            assign_path, assigned_message_descriptor->name(),
            assigned_source_expression),
        absl::StatusCode::kInvalidArgument,
        {.debug_message = GenerateAssignmentDebugMessage(
             nullptr, any_list_message, assigned_message_descriptor,
             assign_path)});
  }

  return CreateStatus(
      13703,
      absl::StrFormat("Cannot parse the message at index %d of type '%s' using "
                      "the descriptor pool '%s' while evaluating expression "
                      "'%s' as the data could not be deserialized",
                      i, message_i_type_name, pool_info.display_name,
                      assigned_source_expression),
      absl::StatusCode::kInvalidArgument);
}

absl::StatusOr<std::vector<std::unique_ptr<google::protobuf::Message>>>
ProtobufManager::ConvertAnyListToMessages(
    const google::protobuf::Message* absl_nonnull any_list_message,
    const DescriptorPoolInfo& pool_info, absl::string_view assigned_field_type,
    absl::string_view assigned_source_expression, absl::string_view full_path,
    const google::protobuf::Descriptor* absl_nullable full_message_descriptor) {
  std::vector<std::unique_ptr<google::protobuf::Message>> messages;
  const google::protobuf::Reflection* any_list_refl =
      any_list_message->GetReflection();
  const google::protobuf::FieldDescriptor* items_field =
      any_list_message->GetDescriptor()->FindFieldByName("items");
  for (int i = 0; i < any_list_refl->FieldSize(*any_list_message, items_field);
       ++i) {
    const google::protobuf::Message& any_msg =
        any_list_refl->GetRepeatedMessage(*any_list_message, items_field, i);

    const google::protobuf::FieldDescriptor* type_url_field =
        any_msg.GetDescriptor()->FindFieldByName("type_url");
    const google::protobuf::Reflection* any_refl = any_msg.GetReflection();
    std::string type_url = any_refl->GetString(any_msg, type_url_field);
    std::string_view type_name = StripTypeUrlPrefix(type_url);
    const google::protobuf::FieldDescriptor* value_field =
        any_msg.GetDescriptor()->FindFieldByName("value");

    absl::StatusOr<std::unique_ptr<google::protobuf::Message>> casted_msg =
        CreateProtoInstanceFromDescriptorPool(
            type_name, pool_info.descriptor_pool, pool_info.message_factory);

    if (casted_msg.status().code() == absl::StatusCode::kNotFound) {
      return CreateAnyListMessageNotInPoolError(
          any_list_message, i, pool_info, type_name, assigned_field_type,
          assigned_source_expression, full_path, full_message_descriptor);
    } else if (!casted_msg.ok()) {
      return CreateAnyListMessageCastError(
          casted_msg.status(), any_list_message, i, pool_info, type_name,
          assigned_field_type, assigned_source_expression, full_path,
          full_message_descriptor);
    }

    if (!(*casted_msg)
             ->ParseFromString(any_refl->GetString(any_msg, value_field))) {
      return CreateAnyListParsingError(
          any_list_message, i, pool_info, type_name, assigned_field_type,
          assigned_source_expression, full_path, full_message_descriptor);
    }
    messages.emplace_back(std::move(*casted_msg));
  }

  return messages;
}

absl::StatusOr<std::vector<std::unique_ptr<google::protobuf::Message>>>
ProtobufManager::ConvertAnyListToMessages(
    const google::protobuf::Message* absl_nonnull any_list_message,
    const DescriptorPoolInfo& pool_info, absl::string_view source_expression) {
  return ConvertAnyListToMessages(any_list_message, pool_info, "",
                                  source_expression, "", nullptr);
}

// Setting a repeated field from a path without an index, i.e., overwriting the
// whole repeated field.
absl::Status ProtobufManager::InternalSetRepeatedFieldFromAnyListProto(
    const MutableFieldFromPath& field, const DescriptorPoolInfo& pool_info,
    const google::protobuf::Message* value_msg,
    absl::string_view assigned_source_expression, absl::string_view full_path,
    const google::protobuf::Descriptor* full_message_descriptor) {
  if (value_msg->GetDescriptor()->full_name() !=
      intrinsic_proto::executive::AnyList::descriptor()->full_name()) {
    return CreateStatus(
        15905,
        absl::StrFormat(
            "Cannot set repeated field of type '%s' from singular type '%s' at "
            "field path '%s' in '%s' while trying to assign expression '%s'",
            ProtoTypeNameFromField(field.descriptor),
            ProtoTypeNameFromMessage(value_msg->GetDescriptor()), full_path,
            full_message_descriptor->name(), assigned_source_expression),
        absl::StatusCode::kInvalidArgument,
        {.debug_message = GenerateAssignmentDebugMessage(
             nullptr, value_msg, full_message_descriptor, full_path)});
  }

  INTR_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<google::protobuf::Message>> messages,
      ConvertAnyListToMessages(
          value_msg, pool_info, ProtoTypeNameFromField(field.descriptor),
          assigned_source_expression, full_path, full_message_descriptor));

  return InternalSetRepeatedFieldFromProtos(
      field.msg, field.descriptor, messages, true, assigned_source_expression,
      full_path, full_message_descriptor);
}

absl::Status ProtobufManager::SetFieldFromProto(
    ProtoMessageId id, absl::string_view field_path,
    DescriptorPoolId descriptor_pool_id, ProtoMessageId value_id) {
  INTR_ASSIGN_OR_RETURN(google::protobuf::Message * msg, GetMutableProto(id));
  INTR_ASSIGN_OR_RETURN(DescriptorPoolInfo pool_info,
                        GetDescriptorPool(descriptor_pool_id));
  INTR_ASSIGN_OR_RETURN(
      const google::protobuf::Message* value_msg, GetProto(value_id),
      _ << "failed to retrieve value proto to set " << field_path);
  return SetFieldFromProto(msg, field_path, pool_info, value_msg, "");
}

absl::Status ProtobufManager::SetFieldFromProto(
    google::protobuf::Message* message, absl::string_view field_path,
    const DescriptorPoolInfo& pool_info,
    const google::protobuf::Message* value_msg,
    absl::string_view path_source_expression) {
  std::string path_context =
      path_source_expression.empty()
          ? ""
          : absl::StrFormat(" (specified to assign expression '%s')",
                            path_source_expression);
  INTR_ASSIGN_OR_RETURN(
      MutableFieldFromPath field,
      intrinsic::executive::clips::GetMutableFieldFromPath(
          message, field_path, /*allow_index=*/true, path_context));
  if (field.index.IsPresent()) {
    if (field.index.IsAdd()) {
      INTR_RETURN_IF_ERROR(InternalSetRepeatedFieldFromProtos(
          field.msg, field.descriptor, {value_msg}, false,
          path_source_expression, field_path, message->GetDescriptor()));
    } else {
      INTR_RETURN_IF_ERROR(InternalSetRepeatedFieldFromProto(
          field.msg, field.descriptor, *field.index, value_msg,
          path_source_expression, field_path, message->GetDescriptor()));
    }
  } else {
    if (field.descriptor->is_repeated()) {
      if (field.descriptor->is_map()) {
        return CreateStatus(
            15900,
            absl::StrFormat("Assignment to map field is not supported. Failed "
                            "to assign field path '%s' in '%s' while trying to "
                            "assign expression '%s'",
                            field_path, message->GetDescriptor()->name(),
                            path_source_expression),
            absl::StatusCode::kUnimplemented);
      }
      INTR_RETURN_IF_ERROR(InternalSetRepeatedFieldFromAnyListProto(
          field, pool_info, value_msg, path_source_expression, field_path,
          message->GetDescriptor()));
    } else {
      INTR_RETURN_IF_ERROR(InternalSetFieldFromProto(
          field.msg, field.descriptor, value_msg, path_source_expression,
          field_path, message->GetDescriptor()));
    }
  }
  return absl::OkStatus();
}

absl::Status ProtobufManager::ClearField(ProtoMessageId id,
                                         absl::string_view field_path) {
  INTR_ASSIGN_OR_RETURN(
      MutableFieldFromPath field,
      GetMutableFieldFromPath(id, field_path, /*allow_index=*/false));

  const Reflection* refl = field.msg->GetReflection();
  refl->ClearField(field.msg, field.descriptor);
  return absl::OkStatus();
}

absl::Status ProtobufManager::SetRepeatedFieldValues(
    ProtoMessageId id, absl::string_view field_path, const Values& values) {
  INTR_ASSIGN_OR_RETURN(
      MutableFieldFromPath field,
      GetMutableFieldFromPath(id, field_path, /*allow_index=*/false));
  INTR_RETURN_IF_ERROR(
      InternalSetRepeatedFieldValues(field.msg, field.descriptor, values, true))
      << absl::StrFormat("Failed to set %s::%s",
                         field.msg->GetDescriptor()->name(), field_path);
  return absl::OkStatus();
}

absl::Status ProtobufManager::InternalSetRepeatedFieldValue(
    google::protobuf::Message* msg,
    const google::protobuf::FieldDescriptor* field, int index,
    const Value& value) {
  const Reflection* refl = msg->GetReflection();
  const int field_size = refl->FieldSize(*msg, field);

  if (index >= field_size) {
    return absl::OutOfRangeError(absl::StrFormat(
        "%s::%s[%d] out of bounds (size %d)", msg->GetDescriptor()->name(),
        field->name(), index, field_size));
  }
  if (index < -field_size) {
    return absl::OutOfRangeError(
        absl::StrFormat("%s::%s[%d] out of bounds (range %d to %d)",
                        msg->GetDescriptor()->name(), field->name(), index,
                        -field_size, field_size - 1));
  }

  // Handle [-1]-style indexes indicating "from the end" indices
  if (index < 0) {
    index = field_size + index;
  }

  switch (field->type()) {
    case FieldDescriptor::TYPE_DOUBLE: {
      INTR_ASSIGN_OR_RETURN(double double_value,
                            CheckAndGetFloat<double>(msg, field, value));
      refl->SetRepeatedDouble(msg, field, index, double_value);
      break;
    }
    case FieldDescriptor::TYPE_FLOAT: {
      INTR_ASSIGN_OR_RETURN(float float_value,
                            CheckAndGetFloat<float>(msg, field, value));
      refl->SetRepeatedFloat(msg, field, index, float_value);
      break;
    }
    case FieldDescriptor::TYPE_SFIXED32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_INT32: {
      INTR_ASSIGN_OR_RETURN(int32_t int_value,
                            CheckAndGetInt<int32_t>(msg, field, value));
      refl->SetRepeatedInt32(msg, field, index, int_value);
      break;
    }
    case FieldDescriptor::TYPE_UINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED32: {
      INTR_ASSIGN_OR_RETURN(uint32_t int_value,
                            CheckAndGetInt<uint32_t>(msg, field, value));
      refl->SetRepeatedUInt32(msg, field, index, int_value);
      break;
    }
    case FieldDescriptor::TYPE_INT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT64: {
      INTR_ASSIGN_OR_RETURN(int64_t int_value,
                            CheckAndGetInt<uint32_t>(msg, field, value));
      refl->SetRepeatedInt64(msg, field, index, int_value);
      break;
    }
    case FieldDescriptor::TYPE_UINT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED64: {
      INTR_ASSIGN_OR_RETURN(uint64_t int_value,
                            CheckAndGetInt<uint64_t>(msg, field, value));
      refl->SetRepeatedUInt64(msg, field, index, int_value);
      break;
    }
    case FieldDescriptor::TYPE_BOOL: {
      if (value != Symbol::True() && value != Symbol::False()) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Trying to set %s::%s from non-symbol or non-bool",
                            msg->GetDescriptor()->name(), field->name()));
      }
      refl->SetRepeatedBool(msg, field, index, (value == Symbol::True()));
      break;
    }
    case FieldDescriptor::TYPE_STRING:
      if (value.GetValueType() != Value::Type::kString &&
          value.GetValueType() != Value::Type::kSymbol) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Trying to set %s::%s from non-symbol and non-string",
            msg->GetDescriptor()->name(), field->name()));
      }
      refl->SetRepeatedString(msg, field, index,
                              value.GetStringOrSymbol().value());
      break;
    case FieldDescriptor::TYPE_BYTES:
      if (value.GetValueType() != Value::Type::kString) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Trying to set %s::%s from non-symbol and non-string",
            msg->GetDescriptor()->name(), field->name()));
      }
      refl->SetRepeatedString(msg, field, index, value.GetString().value());
      break;

    case FieldDescriptor::TYPE_ENUM: {
      if (value.GetValueType() != Value::Type::kSymbol) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Trying to set enum %s::%s from non-symbol",
                            msg->GetDescriptor()->name(), field->name()));
      }
      const google::protobuf::EnumDescriptor* enum_desc = field->enum_type();
      const google::protobuf::EnumValueDescriptor* enum_val =
          enum_desc->FindValueByName(value.GetSymbolAsString().value());
      if (enum_val == nullptr) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Trying to set enum %s::%s from invalid value %s",
                            msg->GetDescriptor()->name(), field->name(),
                            value.GetSymbolAsString().value()));
      }
      refl->SetRepeatedEnum(msg, field, index, enum_val);
      break;
    }

    case FieldDescriptor::TYPE_MESSAGE: {
      INTR_ASSIGN_OR_RETURN(int64_t msg_id, value.GetInteger());
      INTR_ASSIGN_OR_RETURN(
          const google::protobuf::Message* msg_value,
          GetProto(ProtoMessageId(msg_id)),
          _ << absl::StrFormat(
              "while retrieving proto value to set field %s::%s",
              msg->GetDescriptor()->name(), field->name()));
      google::protobuf::Message* mutable_msg =
          refl->MutableRepeatedMessage(msg, field, index);
      if (mutable_msg->GetDescriptor()->full_name() ==
          msg_value->GetDescriptor()->full_name()) {
        static_cast<void>(mutable_msg->ParseFromString(
            msg_value->SerializeAsString()));  // TODO: Handle return value
      } else {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Trying to set message %s::%s from invalid type %s instead of %s",
            msg->GetDescriptor()->name(), field->name(),
            msg_value->GetDescriptor()->name(),
            mutable_msg->GetDescriptor()->name()));
      }
      break;
    }

    default:
      return absl::InternalError(
          absl::StrFormat("Unknown protobuf field %s::%s encountered",
                          msg->GetDescriptor()->name(), field->name()));
  }
  return absl::OkStatus();
}

// Set the value at 'index' in repeated field 'field'.
absl::Status ProtobufManager::InternalSetRepeatedFieldFromProto(
    google::protobuf::Message* msg,
    const google::protobuf::FieldDescriptor* field, int index,
    const google::protobuf::Message* value,
    absl::string_view assigned_source_expression, absl::string_view full_path,
    const google::protobuf::Descriptor* full_message_descriptor) {
  const Reflection* refl = msg->GetReflection();
  const int field_size = refl->FieldSize(*msg, field);

  if (index >= field_size) {
    return CreateStatus(
        15904,
        absl::StrFormat("Index %d out of bounds [0, %d] (field size: %d). "
                        "Failed to assign field at path '%s' in '%s' while "
                        "trying to assign expression '%s'",
                        index, field_size - 1, field_size, full_path,
                        full_message_descriptor->name(),
                        assigned_source_expression),
        absl::StatusCode::kOutOfRange,
        {.debug_message = GenerateAssignmentDebugMessage(
             nullptr, value, full_message_descriptor, full_path)});
  }
  if (index < -field_size) {
    return CreateStatus(
        15904,
        absl::StrFormat(
            "Reverse index %d out of bounds [%d, -1] (field size: %d). "
            "Failed to assign field at path '%s' in '%s' while trying to "
            "assign expression '%s'",
            index, -field_size, field_size, full_path,
            full_message_descriptor->name(), assigned_source_expression),
        absl::StatusCode::kOutOfRange,
        {.debug_message = GenerateAssignmentDebugMessage(
             nullptr, value, full_message_descriptor, full_path)});
  }

  // Handle [-1]-style indexes indicating "from the end" indices
  if (index < 0) {
    index = field_size + index;
  }

  switch (field->type()) {
    case FieldDescriptor::TYPE_DOUBLE: {
      INTR_ASSIGN_OR_RETURN(
          auto double_value,
          (CheckAndGetNumberFromProto<double, google::protobuf::DoubleValue,
                                      google::protobuf::FloatValue>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetRepeatedDouble(msg, field, index, double_value);
      break;
    }
    case FieldDescriptor::TYPE_FLOAT: {
      INTR_ASSIGN_OR_RETURN(
          auto float_value,
          (CheckAndGetNumberFromProto<float, google::protobuf::DoubleValue,
                                      google::protobuf::FloatValue>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetRepeatedFloat(msg, field, index, float_value);
      break;
    }
    case FieldDescriptor::TYPE_SFIXED32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_INT32: {
      INTR_ASSIGN_OR_RETURN(
          auto int32_value,
          (CheckAndGetNumberFromProto<int32_t, google::protobuf::UInt32Value,
                                      google::protobuf::Int32Value,
                                      google::protobuf::UInt64Value,
                                      google::protobuf::Int64Value>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetRepeatedInt32(msg, field, index, int32_value);
      break;
    }
    case FieldDescriptor::TYPE_UINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED32: {
      INTR_ASSIGN_OR_RETURN(
          auto uint32_value,
          (CheckAndGetNumberFromProto<uint32_t, google::protobuf::UInt32Value,
                                      google::protobuf::Int32Value,
                                      google::protobuf::UInt64Value,
                                      google::protobuf::Int64Value>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetRepeatedUInt32(msg, field, index, uint32_value);
      break;
    }
    case FieldDescriptor::TYPE_INT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT64: {
      INTR_ASSIGN_OR_RETURN(
          auto int64_value,
          (CheckAndGetNumberFromProto<int64_t, google::protobuf::UInt32Value,
                                      google::protobuf::Int32Value,
                                      google::protobuf::UInt64Value,
                                      google::protobuf::Int64Value>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetRepeatedInt64(msg, field, index, int64_value);
      break;
    }
    case FieldDescriptor::TYPE_UINT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED64: {
      INTR_ASSIGN_OR_RETURN(
          auto uint64_value,
          (CheckAndGetNumberFromProto<uint64_t, google::protobuf::UInt32Value,
                                      google::protobuf::Int32Value,
                                      google::protobuf::UInt64Value,
                                      google::protobuf::Int64Value>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetRepeatedUInt64(msg, field, index, uint64_value);
      break;
    }
    case FieldDescriptor::TYPE_BOOL: {
      INTR_ASSIGN_OR_RETURN(auto bool_value,
                            ConvertMessageValueTo<google::protobuf::BoolValue>(
                                value, full_message_descriptor, full_path,
                                assigned_source_expression));
      refl->SetRepeatedBool(msg, field, index, bool_value.value());
      break;
    }
    case FieldDescriptor::TYPE_STRING: {
      INTR_ASSIGN_OR_RETURN(
          auto string_value,
          ConvertMessageValueTo<google::protobuf::StringValue>(
              value, full_message_descriptor, full_path,
              assigned_source_expression));
      refl->SetRepeatedString(msg, field, index,
                              std::move(string_value.value()));
      break;
    }
    case FieldDescriptor::TYPE_BYTES: {
      INTR_ASSIGN_OR_RETURN(auto bytes_value,
                            ConvertMessageValueTo<google::protobuf::BytesValue>(
                                value, full_message_descriptor, full_path,
                                assigned_source_expression));
      refl->SetRepeatedString(msg, field, index, bytes_value.value());
      break;
    }

    case FieldDescriptor::TYPE_ENUM: {
      return CreateStatus(
          15900,
          absl::StrFormat(
              "Assignment to repeated enum field value is not "
              "supported. Failed to assign from type '%s' at field "
              "path '%s' in '%s' while trying to assign expression '%s'",
              ProtoTypeNameFromMessage(value->GetDescriptor()), full_path,
              full_message_descriptor->name(), assigned_source_expression),
          absl::StatusCode::kUnimplemented,
          {.debug_message = GenerateAssignmentDebugMessage(
               nullptr, value, full_message_descriptor, full_path)});
    }

    case FieldDescriptor::TYPE_MESSAGE: {
      google::protobuf::Message* mutable_msg =
          refl->MutableRepeatedMessage(msg, field, index);
      if (mutable_msg->GetDescriptor()->full_name() ==
          value->GetDescriptor()->full_name()) {
        if (!mutable_msg->ParseFromString(value->SerializeAsString())) {
          return GenerateAssignmentTypeError(
              15902, mutable_msg->GetDescriptor(), value,
              full_message_descriptor, full_path, assigned_source_expression);
        }
      } else {
        return GenerateAssignmentTypeError(
            15901, mutable_msg->GetDescriptor(), value, full_message_descriptor,
            full_path, assigned_source_expression);
      }
      break;
    }

    default:
      return CreateStatus(
          15900,
          absl::StrFormat("Encountered unknown repeated field of type '%s' "
                          "trying to assign from type '%s' at field path '%s' "
                          "in '%s' while trying to assign expression '%s'",
                          field->type_name(),
                          ProtoTypeNameFromMessage(value->GetDescriptor()),
                          full_path, full_message_descriptor->name(),
                          assigned_source_expression),
          absl::StatusCode::kInternal,
          {.debug_message = GenerateAssignmentDebugMessage(
               nullptr, value, full_message_descriptor, full_path)});
  }
  return absl::OkStatus();
}

absl::Status ProtobufManager::InternalSetRepeatedFieldValues(
    google::protobuf::Message* msg,
    const google::protobuf::FieldDescriptor* field, const Values& values,
    bool clear_existing) {
  if (!field->is_repeated()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("%s::%s is not a repeated field",
                        msg->GetDescriptor()->name(), field->name()));
  }

  const Reflection* refl = msg->GetReflection();
  if (clear_existing) {
    refl->ClearField(msg, field);
  }

  for (const clips::Value& value : values) {
    switch (field->type()) {
      case FieldDescriptor::TYPE_DOUBLE: {
        INTR_ASSIGN_OR_RETURN(double double_value,
                              CheckAndGetFloat<double>(msg, field, value));
        refl->AddDouble(msg, field, double_value);
        break;
      }
      case FieldDescriptor::TYPE_FLOAT: {
        INTR_ASSIGN_OR_RETURN(float float_value,
                              CheckAndGetFloat<float>(msg, field, value));
        refl->AddFloat(msg, field, float_value);
        break;
      }
      case FieldDescriptor::TYPE_SFIXED32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SINT32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_INT32: {
        INTR_ASSIGN_OR_RETURN(int32_t int_value,
                              CheckAndGetInt<int32_t>(msg, field, value));
        refl->AddInt32(msg, field, int_value);
        break;
      }
      case FieldDescriptor::TYPE_UINT32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_FIXED32: {
        INTR_ASSIGN_OR_RETURN(uint32_t int_value,
                              CheckAndGetInt<uint32_t>(msg, field, value));
        refl->AddUInt32(msg, field, int_value);
        break;
      }
      case FieldDescriptor::TYPE_INT64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SFIXED64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SINT64: {
        INTR_ASSIGN_OR_RETURN(int64_t int_value,
                              CheckAndGetInt<uint32_t>(msg, field, value));
        refl->AddInt64(msg, field, int_value);
        break;
      }
      case FieldDescriptor::TYPE_UINT64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_FIXED64: {
        INTR_ASSIGN_OR_RETURN(uint64_t int_value,
                              CheckAndGetInt<uint64_t>(msg, field, value));
        refl->AddUInt64(msg, field, int_value);
        break;
      }
      case FieldDescriptor::TYPE_BOOL: {
        if (value != Symbol::True() && value != Symbol::False()) {
          return absl::InvalidArgumentError(absl::StrFormat(
              "Trying to set %s::%s from non-symbol or non-bool",
              msg->GetDescriptor()->name(), field->name()));
        }
        refl->AddBool(msg, field, (value == Symbol::True()));
        break;
      }
      case FieldDescriptor::TYPE_STRING:
        if (value.GetValueType() != Value::Type::kString &&
            value.GetValueType() != Value::Type::kSymbol) {
          return absl::InvalidArgumentError(absl::StrFormat(
              "Trying to set %s::%s from non-symbol and non-string",
              msg->GetDescriptor()->name(), field->name()));
        }
        refl->AddString(msg, field, value.GetStringOrSymbol().value());
        break;
      case FieldDescriptor::TYPE_BYTES:
        if (value.GetValueType() != Value::Type::kString) {
          return absl::InvalidArgumentError(
              absl::StrFormat("Trying to set %s::%s from non-string",
                              msg->GetDescriptor()->name(), field->name()));
        }
        refl->AddString(msg, field, value.GetString().value());
        break;

      case FieldDescriptor::TYPE_ENUM: {
        if (value.GetValueType() != Value::Type::kSymbol) {
          return absl::InvalidArgumentError(
              absl::StrFormat("Trying to set enum %s::%s from non-symbol",
                              msg->GetDescriptor()->name(), field->name()));
        }
        const google::protobuf::EnumDescriptor* enum_desc = field->enum_type();
        const google::protobuf::EnumValueDescriptor* enum_val =
            enum_desc->FindValueByName(value.GetSymbolAsString().value());
        if (enum_val == nullptr) {
          return absl::InvalidArgumentError(
              absl::StrFormat("Trying to set enum %s::%s from invalid value %s",
                              msg->GetDescriptor()->name(), field->name(),
                              value.GetSymbolAsString().value()));
        }
        refl->AddEnum(msg, field, enum_val);
        break;
      }

      case FieldDescriptor::TYPE_MESSAGE: {
        INTR_ASSIGN_OR_RETURN(int64_t msg_id, value.GetInteger());
        INTR_ASSIGN_OR_RETURN(
            const google::protobuf::Message* msg_value,
            GetProto(ProtoMessageId(msg_id)),
            _ << absl::StrFormat(
                "while retrieving proto values to set field %s::%s",
                msg->GetDescriptor()->name(), field->name()));

        google::protobuf::Message* mutable_msg = refl->AddMessage(msg, field);
        if (mutable_msg->GetDescriptor()->full_name() ==
            msg_value->GetDescriptor()->full_name()) {
          static_cast<void>(mutable_msg->ParseFromString(
              msg_value->SerializeAsString()));  // TODO: Handle return value
        } else {
          return absl::InvalidArgumentError(
              absl::StrFormat("Trying to set message %s::%s from invalid "
                              "type %s instead of %s",
                              msg->GetDescriptor()->name(), field->name(),
                              msg_value->GetDescriptor()->name(),
                              mutable_msg->GetDescriptor()->name()));
        }
        break;
      }

      default:
        return absl::InternalError(
            absl::StrFormat("Unknown protobuf field %s::%s encountered",
                            msg->GetDescriptor()->name(), field->name()));
    }
  }

  return absl::OkStatus();
}

absl::Status ProtobufManager::InternalSetRepeatedFieldFromProtos(
    google::protobuf::Message* msg,
    const google::protobuf::FieldDescriptor* field,
    absl::Span<const std::unique_ptr<google::protobuf::Message>> values,
    bool clear_existing, absl::string_view assigned_source_expression,
    absl::string_view full_path,
    const google::protobuf::Descriptor* full_message_descriptor) {
  std::vector<const google::protobuf::Message*> messages;
  messages.reserve(values.size());
  for (const auto& msg_ptr : values) {
    messages.push_back(msg_ptr.get());
  }
  return InternalSetRepeatedFieldFromProtos(
      msg, field, messages, clear_existing, assigned_source_expression,
      full_path, full_message_descriptor);
}

// set the complete repeated field 'field' from the given messages in 'values'
absl::Status ProtobufManager::InternalSetRepeatedFieldFromProtos(
    google::protobuf::Message* msg,
    const google::protobuf::FieldDescriptor* field,
    const std::vector<const google::protobuf::Message*>& values,
    bool clear_existing, absl::string_view assigned_source_expression,
    absl::string_view full_path,
    const google::protobuf::Descriptor* full_message_descriptor) {
  if (!field->is_repeated()) {
    return CreateStatus(
        15900,
        absl::StrFormat(
            "Failed to assign singular field of type '%s' from list of values "
            "at field path '%s' in '%s' while trying to assign expression '%s'",
            field->type_name(), full_path, full_message_descriptor->name(),
            assigned_source_expression),
        absl::StatusCode::kInternal);
  }

  const Reflection* refl = msg->GetReflection();
  if (clear_existing) {
    refl->ClearField(msg, field);
  }

  for (const google::protobuf::Message* value : values) {
    switch (field->type()) {
      case FieldDescriptor::TYPE_DOUBLE: {
        INTR_ASSIGN_OR_RETURN(
            auto double_value,
            (CheckAndGetNumberFromProto<double, google::protobuf::DoubleValue,
                                        google::protobuf::FloatValue>(
                msg, field, value, full_message_descriptor, full_path,
                assigned_source_expression)));
        refl->AddDouble(msg, field, double_value);
        break;
      }
      case FieldDescriptor::TYPE_FLOAT: {
        INTR_ASSIGN_OR_RETURN(
            auto float_value,
            (CheckAndGetNumberFromProto<float, google::protobuf::DoubleValue,
                                        google::protobuf::FloatValue>(
                msg, field, value, full_message_descriptor, full_path,
                assigned_source_expression)));
        refl->AddFloat(msg, field, float_value);
        break;
      }
      case FieldDescriptor::TYPE_SFIXED32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SINT32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_INT32: {
        INTR_ASSIGN_OR_RETURN(
            auto int32_value,
            (CheckAndGetNumberFromProto<int32_t, google::protobuf::UInt32Value,
                                        google::protobuf::Int32Value,
                                        google::protobuf::UInt64Value,
                                        google::protobuf::Int64Value>(
                msg, field, value, full_message_descriptor, full_path,
                assigned_source_expression)));
        refl->AddInt32(msg, field, int32_value);
        break;
      }
      case FieldDescriptor::TYPE_UINT32:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_FIXED32: {
        INTR_ASSIGN_OR_RETURN(
            auto uint32_value,
            (CheckAndGetNumberFromProto<uint32_t, google::protobuf::UInt32Value,
                                        google::protobuf::Int32Value,
                                        google::protobuf::UInt64Value,
                                        google::protobuf::Int64Value>(
                msg, field, value, full_message_descriptor, full_path,
                assigned_source_expression)));
        refl->AddUInt32(msg, field, uint32_value);
        break;
      }
      case FieldDescriptor::TYPE_INT64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SFIXED64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_SINT64: {
        INTR_ASSIGN_OR_RETURN(
            auto int64_value,
            (CheckAndGetNumberFromProto<int64_t, google::protobuf::UInt32Value,
                                        google::protobuf::Int32Value,
                                        google::protobuf::UInt64Value,
                                        google::protobuf::Int64Value>(
                msg, field, value, full_message_descriptor, full_path,
                assigned_source_expression)));
        refl->AddInt64(msg, field, int64_value);
        break;
      }
      case FieldDescriptor::TYPE_UINT64:
        ABSL_FALLTHROUGH_INTENDED;
      case FieldDescriptor::TYPE_FIXED64: {
        INTR_ASSIGN_OR_RETURN(
            auto uint64_value,
            (CheckAndGetNumberFromProto<uint64_t, google::protobuf::UInt32Value,
                                        google::protobuf::Int32Value,
                                        google::protobuf::UInt64Value,
                                        google::protobuf::Int64Value>(
                msg, field, value, full_message_descriptor, full_path,
                assigned_source_expression)));
        refl->AddUInt64(msg, field, uint64_value);
        break;
      }
      case FieldDescriptor::TYPE_BOOL: {
        INTR_ASSIGN_OR_RETURN(
            auto bool_value, ConvertMessageValueTo<google::protobuf::BoolValue>(
                                 value, full_message_descriptor, full_path,
                                 assigned_source_expression));
        refl->AddBool(msg, field, bool_value.value());
        break;
      }
      case FieldDescriptor::TYPE_STRING: {
        INTR_ASSIGN_OR_RETURN(
            auto string_value,
            ConvertMessageValueTo<google::protobuf::StringValue>(
                value, full_message_descriptor, full_path,
                assigned_source_expression));
        refl->AddString(msg, field, std::move(string_value.value()));
        break;
      }
      case FieldDescriptor::TYPE_BYTES: {
        INTR_ASSIGN_OR_RETURN(
            auto bytes_value,
            ConvertMessageValueTo<google::protobuf::BytesValue>(
                value, full_message_descriptor, full_path,
                assigned_source_expression));
        refl->AddString(msg, field, bytes_value.value());
        break;
      }

      case FieldDescriptor::TYPE_ENUM:
        return CreateStatus(
            15900,
            absl::StrFormat(
                "Assignment to repeated enum field is not supported. Failed "
                "to assign from type '%s' at field path '%s' in '%s' "
                "while trying to assign expression '%s'",
                ProtoTypeNameFromMessage(value->GetDescriptor()), full_path,
                full_message_descriptor->name(), assigned_source_expression),
            absl::StatusCode::kUnimplemented,
            {.debug_message = GenerateAssignmentDebugMessage(
                 nullptr, value, full_message_descriptor, full_path)});
      case FieldDescriptor::TYPE_MESSAGE: {
        google::protobuf::Message* mutable_msg = refl->AddMessage(msg, field);
        if (mutable_msg->GetDescriptor()->full_name() ==
            value->GetDescriptor()->full_name()) {
          if (!mutable_msg->ParseFromString(value->SerializeAsString())) {
            return GenerateAssignmentTypeError(
                15902, mutable_msg->GetDescriptor(), value,
                full_message_descriptor, full_path, assigned_source_expression);
          }
        } else {
          return GenerateAssignmentTypeError(
              15901, mutable_msg->GetDescriptor(), value,
              full_message_descriptor, full_path, assigned_source_expression);
        }
        break;
      }

      default:
        return CreateStatus(
            15900,
            absl::StrFormat(
                "Encountered unknown repeated field of type '%s' trying to "
                "assign from type '%s' at field path '%s' in '%s' while trying "
                "to assign expression '%s'",
                field->type_name(),
                ProtoTypeNameFromMessage(value->GetDescriptor()), full_path,
                full_message_descriptor->name(), assigned_source_expression),
            absl::StatusCode::kInternal,
            {.debug_message = GenerateAssignmentDebugMessage(
                 nullptr, value, full_message_descriptor, full_path)});
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<bool> ProtobufManager::IsFieldValueInSameOneofOption(
    ProtoMessageId proto_id, absl::string_view field_path) {
  INTR_ASSIGN_OR_RETURN(
      const google::protobuf::Message* full_message, GetProto(proto_id),
      _ << "while getting field at path " << field_path << ".");
  return IsFieldValueInSameOneofOption(full_message, field_path);
}

absl::StatusOr<bool> ProtobufManager::IsFieldValueInSameOneofOption(
    const google::protobuf::Message* message, absl::string_view field_path) {
  INTR_RETURN_IF_ERROR(CheckIsValidFieldPath(field_path, ""));

  std::vector<std::string> fields =
      absl::StrSplit(field_path, kProtoPathSeparator, absl::SkipWhitespace());
  if (fields.empty()) {
    return absl::InvalidArgumentError("Field path has no segments");
  }
  absl::Span<const std::string> fields_span(fields);

  std::string cur_path = "";

  bool skipped_repeated_field = false;

  // Skipping the last field, as that would not be a different value in a oneof,
  // but a different oneof
  for (const std::string& field : fields_span.subspan(0, fields.size() - 1)) {
    absl::StrAppend(&cur_path, cur_path.empty() ? "" : kProtoPathSeparator,
                    field);

    // This indicates access of an item in a repeated field. As repeated fields
    // cannot be oneofs it is safe to skip this step.
    // This requires some special handling below, because not only the field at
    // cur_path could not be set but also the fields before, if these were
    // repeated fields.
    // However, one cannot evaluate FieldDescriptors for items in repeated
    // fields (these do no exist). Thus the only alternative to skipping these
    // would be to manually evaluate a path expression with '[' in it. As this
    // function only cares about oneof consistency, this is not necessary.
    if (absl::StrContains(field, kProtoPathArrayOpeningBracket)) {
      skipped_repeated_field = true;
      continue;
    }

    absl::StatusOr<FieldFromPath> field_lookup =
        intrinsic::executive::clips::GetFieldFromPath(message, cur_path,
                                                      /*allow_index=*/true);

    // At this point, cur_path has been checked up until the current field.
    // Looking up the path to the current field can have three outcomes:
    // - The field is not found. This should not happen as every field along the
    //   path is investigated and thus must exist. If the last field was set it
    //   is possible to get a FieldDescriptor even if the current field is not
    //   set. Thus this is an error.
    //   The only exception is if skipped_repeated_field is true as these were
    //   explicitly skipped, not looked up and thus could be unset at the given
    //   index. In this case all checks for previous fields passed - besides the
    //   repeated fields, which cannot be oneofs. There is no need to
    //   investigate further along the path as the current field is not set and
    //   thus cannot be changed.
    // - Some other error: This should not happen, usually a bad path
    //   specification -> report that error.
    // - The FieldDescriptor is found. Proceed checking for oneof consistency at
    //   this field and along the path.
    if (field_lookup.status().code() == absl::StatusCode::kOutOfRange) {
      if (skipped_repeated_field) {
        return true;
      }

      return absl::InvalidArgumentError(absl::StrFormat(
          "Could not determine field information for field %s::%s",
          message->GetDescriptor()->name(), cur_path));
    } else if (!field_lookup.ok()) {
      return field_lookup.status();
    }

    const google::protobuf::Reflection* cur_refl =
        field_lookup->msg->GetReflection();

    const OneofDescriptor* containing_oneof =
        field_lookup->descriptor->containing_oneof();
    if (containing_oneof != nullptr) {
      bool path_field_is_set =
          cur_refl->HasField(*field_lookup->msg, field_lookup->descriptor);
      // If the msg does not have the field set as in path, to change a oneof
      // option another option of the oneof must be set. Otherwise this is just
      // a set, but not a change.
      if (!path_field_is_set) {
        bool has_other_option_set = false;
        for (int i = 0; i < containing_oneof->field_count(); ++i) {
          const FieldDescriptor* other_option = containing_oneof->field(i);
          has_other_option_set |=
              cur_refl->HasField(*field_lookup->msg, other_option);
          if (has_other_option_set) break;
        }
        if (has_other_option_set) {
          return false;
        }
      }
    }

    if (field_lookup->descriptor->type() == FieldDescriptor::TYPE_MESSAGE &&
        // singular field and message is not set
        ((!field_lookup->descriptor->is_repeated() &&
          !cur_refl->HasField(*field_lookup->msg, field_lookup->descriptor)) ||
         // repeated field without elements
         (field_lookup->descriptor->is_repeated() &&
          cur_refl->FieldSize(*field_lookup->msg, field_lookup->descriptor) ==
              0))) {
      // The field is not set: Everything that was set in the message path until
      // this field has been verified and from here on the path just goes deeper
      // into the unset parts of the message. Therefore the remainder of the
      // path cannot change a oneof and it is safe to stop checking.
      return true;
    }

    // just looked at a non-repeated field
    skipped_repeated_field = false;
  }
  return true;
}

void ProtobufManager::WarnIfFieldValueNotInSameOneofOption(
    const google::protobuf::Message* message, absl::string_view field_path) {
  absl::StatusOr<bool> is_same_option =
      IsFieldValueInSameOneofOption(message, field_path);
  if (!is_same_option.ok()) {
    LOG(WARNING)
        << "Parameterization for path " << field_path
        << " could not determine if this changes a oneof field. Error: "
        << is_same_option.status();
  } else if (!is_same_option.value()) {
    LOG(WARNING) << "Parameterization for path " << field_path
                 << " changed a oneof field. This might be unintended. "
                    "Verify that the assigned parameters for oneof fields "
                    "are also set in the message.";
  }
}

absl::Status ProtobufManager::CopyBytesValue(ProtoMessageId src_msg_id,
                                             absl::string_view src_field_path,
                                             ProtoMessageId dst_msg_id,
                                             absl::string_view dst_field_path) {
  INTR_ASSIGN_OR_RETURN(FieldFromPath src_field,
                        GetFieldFromPath(src_msg_id, src_field_path));
  INTR_ASSIGN_OR_RETURN(MutableFieldFromPath dst_field,
                        GetMutableFieldFromPath(dst_msg_id, dst_field_path,
                                                /*allow_index=*/true));

  if (src_field.descriptor->type() != FieldDescriptor::TYPE_BYTES) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Source field %s is not of type BYTES", src_field_path));
  }
  if (dst_field.descriptor->type() != FieldDescriptor::TYPE_BYTES) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Destination field %s is not of type BYTES", dst_field_path));
  }

  const Reflection* src_refl = src_field.msg->GetReflection();
  const Reflection* dst_refl = dst_field.msg->GetReflection();

  if (src_field.index.IsPresent() && !src_field.descriptor->is_repeated()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Source field %s is not a repeated field, but an index was given",
        src_field_path));
  }
  if (dst_field.index.IsPresent() && !dst_field.descriptor->is_repeated()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Destination field %s is not a repeated field, but "
                        "an index was given",
                        dst_field_path));
  }

  std::string src_string =
      src_field.index.IsAbsent()
          ? src_refl->GetString(*src_field.msg, src_field.descriptor)
          : src_refl->GetRepeatedString(*src_field.msg, src_field.descriptor,
                                        *src_field.index);

  if (dst_field.index.IsPresent()) {
    // Destination is a repeated value
    dst_refl->SetRepeatedString(dst_field.msg, dst_field.descriptor,
                                *dst_field.index, std::move(src_string));
  } else {
    // Destination is NOT a repeated value
    dst_refl->SetString(dst_field.msg, dst_field.descriptor,
                        std::move(src_string));
  }

  return absl::OkStatus();
}

absl::StatusOr<int64_t> ProtobufManager::BytesSize(
    ProtoMessageId msg_id, absl::string_view field_path) {
  INTR_ASSIGN_OR_RETURN(FieldFromPath field,
                        GetFieldFromPath(msg_id, field_path));

  if (field.descriptor->type() != FieldDescriptor::TYPE_BYTES) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Source field %s is not of type BYTES", field_path));
  }

  const Reflection* refl = field.msg->GetReflection();
  size_t size =
      field.index.IsAbsent()
          ? refl->GetString(*field.msg, field.descriptor).size()
          : refl->GetRepeatedString(*field.msg, field.descriptor, *field.index)
                .size();
  if (size > std::numeric_limits<int64_t>::max()) {
    return absl::OutOfRangeError(absl::StrFormat(
        "BytesSize: Size of byte field was unexpectedly large: %d", size));
  }
  return static_cast<int64_t>(size);
}

absl::StatusOr<std::string> ProtobufManager::GetMessageTypeName(
    ProtoMessageId proto_id) {
  INTR_ASSIGN_OR_RETURN(const google::protobuf::Message* msg,
                        GetProto(proto_id));
  return std::string(msg->GetDescriptor()->full_name());
}

absl::StatusOr<std::string> ProtobufManager::GetAnyTypeName(
    ProtoMessageId proto_id) {
  INTR_ASSIGN_OR_RETURN(auto any_proto,
                        GetProtoAs<google::protobuf::Any>(proto_id));
  INTR_ASSIGN_OR_RETURN(std::string_view type_name,
                        GetAnyTypeNameFromMessage(*any_proto));
  return std::string(type_name);
}

absl::StatusOr<std::string> ProtobufManager::GetAnyFieldTypeName(
    ProtoMessageId proto_id, absl::string_view field_path) {
  INTR_ASSIGN_OR_RETURN(FieldFromPath field,
                        GetFieldFromPath(proto_id, field_path));

  if (field.descriptor->message_type() == nullptr ||
      field.descriptor->message_type()->full_name() !=
          google::protobuf::Any::descriptor()->full_name()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Field %s is of type %s, expected %s", field_path,
                        field.msg->GetTypeName(),
                        google::protobuf::Any::descriptor()->full_name()));
  }

  const Reflection* refl = field.msg->GetReflection();
  const google::protobuf::Message& any_msg =
      field.index.IsAbsent() ? refl->GetMessage(*field.msg, field.descriptor)
                             : refl->GetRepeatedMessage(
                                   *field.msg, field.descriptor, *field.index);
  const Descriptor* any_desc = any_msg.GetDescriptor();
  const Reflection* any_refl = any_msg.GetReflection();
  const FieldDescriptor* type_url_field = any_desc->FindFieldByName("type_url");
  if (type_url_field == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to retrieve type_url field from Any proto");
  }
  if (type_url_field->type() != FieldDescriptor::TYPE_STRING) {
    return absl::InvalidArgumentError("Field type_url is not of type string");
  }

  std::string type_url = any_refl->GetString(any_msg, type_url_field);
  INTR_ASSIGN_OR_RETURN(std::string_view type_name,
                        GetAnyTypeNameFromTypeUrl(type_url));
  return std::string(type_name);
}

absl::StatusOr<std::string> ProtobufManager::GetAssetIdFromTypeUrl(
    absl::string_view type_url) {
  INTR_ASSIGN_OR_RETURN(const ParsedUrl parsed_url, ParseTypeUrl(type_url));
  return GetAssetIdFromParsedUrl(parsed_url);
}

absl::StatusOr<std::string> ProtobufManager::GetAssetIdFromTypeUrlPrefix(
    absl::string_view type_url_prefix) {
  INTR_ASSIGN_OR_RETURN(const ParsedUrl parsed_url,
                        ParseTypeUrlPrefix(type_url_prefix));
  return GetAssetIdFromParsedUrl(parsed_url);
}

absl::StatusOr<std::string> ProtobufManager::GetAssetIdFromParsedUrl(
    const ParsedUrl& parsed_url) {
  if (parsed_url.area != intrinsic::kIntrinsicTypeUrlAreaSkills &&
      parsed_url.area != intrinsic::kIntrinsicTypeUrlAreaAssets) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Type URL %s is not a skill or assets type URL, but has area %s",
        parsed_url.type_url, parsed_url.area));
  }

  size_t next_slash = parsed_url.path.find('/');
  // The type URL can be type.intrinsic.ai/assets/<skill_id>/Message or
  //                     type.intrinsic.ai/assets/<skill_id>/<version>/Message
  // The path part between assets/ and Message thus either has a slash (with
  // version) or doesn't (without version).
  if (next_slash == std::string::npos) {
    if (parsed_url.path.empty()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("%s is not a skill type URL", parsed_url.type_url));
    }
    // Latter case: typeURL does have a single part without a version.
    return parsed_url.path;
  }

  std::string skill_id = parsed_url.path.substr(0, next_slash);
  if (skill_id.empty()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("%s is not a skill type URL", parsed_url.type_url));
  }
  return skill_id;
}

absl::StatusOr<ProtoMessageId> ProtobufManager::CastFromAnyWithPool(
    ProtoMessageId proto_id, DescriptorPoolId pool_id,
    absl::string_view expected_message_type) {
  if (proto_id == kInvalidId) {
    return CreateStatus(13702, "Proto does not exist (invalid id: 0)",
                        absl::StatusCode::kInternal);
  }

  INTR_ASSIGN_OR_RETURN(std::unique_ptr<google::protobuf::Any> any_proto,
                        GetProtoAs<google::protobuf::Any>(proto_id),
                        std::move(_).With(WrapExtendedStatus(
                            13703, "Could not retrieve the proto as an Any",
                            StatusBuilder::LEGACY_AS_DEBUG_REPORT)));
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<DescriptorPoolInfoInternal> pool_info,
      GetDescriptorPoolInternal(pool_id),
      std::move(_).With(WrapExtendedStatus(
          13703,
          absl::StrFormat(
              "Could not retrieve descriptor pool (internal id: %d)",
              pool_id.value()),
          StatusBuilder::LEGACY_AS_DEBUG_REPORT)));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> casted,
      CastFromAnyWithPool(*any_proto,
                          DescriptorPoolInfoFromInternal(*pool_info),
                          expected_message_type));

  absl::MutexLock lock(protos_mutex_);
  INTR_ASSIGN_OR_RETURN(clips::ProtoMessageId casted_id,
                        AddProto(std::move(casted), std::move(pool_info)),
                        std::move(_).LogError().With(WrapExtendedStatus(
                            13703, "Failed to stored cast result",
                            StatusBuilder::LEGACY_AS_DEBUG_REPORT)));
  return casted_id;
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
ProtobufManager::CastFromAnyWithPool(const google::protobuf::Any& any_proto,
                                     const DescriptorPoolInfo& pool_info,
                                     absl::string_view expected_message_type) {
  INTR_ASSIGN_OR_RETURN(
      std::string_view any_type_name, GetAnyTypeNameFromMessage(any_proto),
      std::move(_).With(AttachExtendedStatus(
          13703,
          absl::StrFormat("Could not get type name from Any type URL: '%s'",
                          any_proto.type_url()))));

  absl::StatusOr<std::unique_ptr<google::protobuf::Message>> casted =
      CreateProtoInstanceFromDescriptorPool(
          any_type_name, pool_info.descriptor_pool, pool_info.message_factory);
  if (casted.status().code() == absl::StatusCode::kNotFound) {
    return CreateStatus(
        13705,
        absl::StrFormat(
            "The message type '%s' does not exist in the descriptor pool '%s'",
            any_type_name, pool_info.display_name),
        absl::StatusCode::kNotFound);
  } else if (!casted.ok()) {
    return CreateStatus(
        13703,
        absl::StrFormat(
            "Failed to create proto from the given descriptor pool"),
        casted.status().code(), {.debug_message = casted.status().message()});
  }

  if (!any_proto.UnpackTo(casted->get())) {
    return CreateStatus(13703, "Unpacking the Any failed",
                        absl::StatusCode::kInternal);
  }

  if (!expected_message_type.empty() &&
      (*casted)->GetTypeName() != expected_message_type) {
    return CreateStatus(
        13704,
        absl::StrFormat("Casted proto has type %s, but type %s was expected.",
                        (*casted)->GetTypeName(), expected_message_type),
        absl::StatusCode::kInvalidArgument);
  }

  return std::move(*casted);
}

absl::StatusOr<ProtoMessageId> ProtobufManager::UnpackFromAnyField(
    ProtoMessageId proto_id, absl::string_view field_path,
    DescriptorPoolId pool_id) {
  INTR_ASSIGN_OR_RETURN(FieldFromPath field,
                        GetFieldFromPath(proto_id, field_path));

  const Reflection* refl = field.msg->GetReflection();
  const google::protobuf::Message& any_msg =
      field.index.IsAbsent() ? refl->GetMessage(*field.msg, field.descriptor)
                             : refl->GetRepeatedMessage(
                                   *field.msg, field.descriptor, *field.index);
  const Descriptor* any_desc = any_msg.GetDescriptor();
  const Reflection* any_refl = any_msg.GetReflection();

  if (any_msg.GetTypeName() !=
      google::protobuf::Any::descriptor()->full_name()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Field %s is of type %s, expected %s", field_path,
                        any_msg.GetTypeName(),
                        google::protobuf::Any::descriptor()->full_name()));
  }

  const FieldDescriptor* type_url_field = any_desc->FindFieldByName("type_url");
  if (type_url_field == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to retrieve type_url field from Any proto");
  }
  if (type_url_field->type() != FieldDescriptor::TYPE_STRING) {
    return absl::InvalidArgumentError("Field type_url is not of type string");
  }
  const FieldDescriptor* value_field = any_desc->FindFieldByName("value");
  if (value_field == nullptr) {
    return absl::InvalidArgumentError(
        "Failed to retrieve value field from Any proto");
  }
  if (value_field->type() != FieldDescriptor::TYPE_BYTES) {
    return absl::InvalidArgumentError("Field values is not of type bytes");
  }

  std::string type_url = any_refl->GetString(any_msg, type_url_field);
  INTR_ASSIGN_OR_RETURN(std::string_view any_type_name,
                        GetAnyTypeNameFromTypeUrl(type_url));

  INTR_ASSIGN_OR_RETURN(std::shared_ptr<DescriptorPoolInfoInternal> pool_info,
                        GetDescriptorPoolInternal(pool_id));
  INTR_ASSIGN_OR_RETURN(std::unique_ptr<google::protobuf::Message> casted,
                        CreateProtoInstanceFromDescriptorPool(
                            any_type_name, pool_info->descriptor_pool,
                            pool_info->message_factory));
  if (!casted->ParseFromString(any_refl->GetString(any_msg, value_field))) {
    return absl::InvalidArgumentError("Failed to parse proto from Any value");
  }

  absl::MutexLock lock(protos_mutex_);
  return AddProto(std::move(casted), std::move(pool_info));
}

absl::StatusOr<google::protobuf::Any> ProtobufManager::CastToAny(
    ProtoMessageId proto_id) {
  google::protobuf::Any any_proto;
  {
    absl::MutexLock lock(protos_mutex_);
    INTR_ASSIGN_OR_RETURN(const ManagedProto& proto, GetManagedProto(proto_id));

    if (proto.pool_info == nullptr) {
      return absl::InternalError(absl::StrFormat(
          "Proto message to cast had no pool information attached. Message: %s",
          proto->DebugString()));
    }
    if (!any_proto.PackFrom(*proto, proto.pool_info->type_url_prefix)) {
      return absl::InternalError("Failed to pack proto into Any.");
    }
  }
  return any_proto;
}

absl::StatusOr<ProtoMessageId> ProtobufManager::CastToAnyClips(
    ProtoMessageId proto_id) {
  INTR_ASSIGN_OR_RETURN(google::protobuf::Any any_proto, CastToAny(proto_id));
  return AddGeneratedProto(any_proto);
}

absl::Status ProtobufManager::PackToAnyField(ProtoMessageId proto_id,
                                             absl::string_view field_path,
                                             ProtoMessageId value_proto_id) {
  INTR_ASSIGN_OR_RETURN(MutableFieldFromPath field,
                        GetMutableFieldFromPath(proto_id, field_path,
                                                /*allow_index=*/true));
  const google::protobuf::Descriptor* field_message_desc =
      field.descriptor->message_type();
  if (field_message_desc == nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Field at path '%s' is not a message type", field_path));
  }
  if (field_message_desc->full_name() !=
      google::protobuf::Any::descriptor()->full_name()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Field at path '%s' is of type %s, expected %s",
                        field_path, field_message_desc->full_name(),
                        google::protobuf::Any::descriptor()->full_name()));
  }

  const Reflection* refl = field.msg->GetReflection();
  google::protobuf::Message* mutable_msg =
      field.index.IsAbsent() ? refl->MutableMessage(field.msg, field.descriptor)
                             : refl->MutableRepeatedMessage(
                                   field.msg, field.descriptor, *field.index);
  const Reflection* mutable_refl = mutable_msg->GetReflection();
  const google::protobuf::FieldDescriptor* type_field =
      mutable_msg->GetDescriptor()->FindFieldByName("type_url");
  if (type_field == nullptr) {
    // User-supplied invalid Any proto, shouldn't happen...
    return absl::InvalidArgumentError("Any proto lacks type_url field");
  }
  const google::protobuf::FieldDescriptor* value_field =
      mutable_msg->GetDescriptor()->FindFieldByName("value");
  if (value_field == nullptr) {
    // User-supplied invalid Any proto, shouldn't happen...
    return absl::InvalidArgumentError("Any proto lacks value field");
  }

  {
    absl::MutexLock lock(protos_mutex_);
    INTR_ASSIGN_OR_RETURN(const ManagedProto& value_msg,
                          GetManagedProto(value_proto_id));

    std::string type_url;
    if (value_msg.pool_info != nullptr) {
      type_url = AddTypeUrlPrefix(value_msg.get(),
                                  value_msg.pool_info->type_url_prefix);
    } else {
      type_url = AddTypeUrlPrefix(value_msg.get());
    }

    mutable_refl->SetString(mutable_msg, type_field, type_url);
    mutable_refl->SetString(mutable_msg, value_field,
                            value_msg->SerializeAsString());
  }
  return absl::OkStatus();
}

absl::StatusOr<ProtoMessageId> ProtobufManager::CreateUpdatedProtoFromAny(
    ProtoMessageId proto_to_create_from,
    const google::protobuf::Any& value_proto)
    ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  if (proto_to_create_from == kInvalidId) {
    return absl::NotFoundError(
        absl::StrFormat("Proto to update not found (internal id: %d)",
                        proto_to_create_from.value()));
  }

  absl::MutexLock lock(protos_mutex_);
  INTR_ASSIGN_OR_RETURN(const ManagedProto& prototype_proto,
                        GetManagedProto(proto_to_create_from));
  std::string_view prototype_type_name =
      prototype_proto->GetDescriptor()->full_name();

  INTR_ASSIGN_OR_RETURN(std::string_view value_type_name,
                        GetAnyTypeNameFromMessage(value_proto));

  if (prototype_type_name != value_type_name) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Proto to update is of type '%s', and cannot be "
                        "updated with value of type '%s'",
                        prototype_type_name, value_type_name));
  }

  google::protobuf::Message* new_message = prototype_proto->New();
  auto new_message_ptr = absl::WrapUnique(new_message);
  if (!new_message->ParseFromString(value_proto.value())) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Failed to update proto, cannot deserialize from input "
                        "value of type '%s'",
                        value_type_name));
  }

  INTR_ASSIGN_OR_RETURN(
      clips::ProtoMessageId new_message_id,
      AddProto(std::move(new_message_ptr), prototype_proto.pool_info),
      _.LogError());

  return new_message_id;
}

absl::Status ProtobufManager::SetMapFieldValue(ProtoMessageId id,
                                               absl::string_view field_path,
                                               const Value& key,
                                               const Value& value) {
  INTR_ASSIGN_OR_RETURN(
      MutableFieldFromPath field,
      GetMutableFieldFromPath(id, field_path, /*allow_index=*/false));

  if (!field.descriptor->is_map()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Field %s::%s is not a map",
                        field.msg->GetDescriptor()->name(), field_path));
  }

  const Descriptor* map_desc = field.descriptor->message_type();
  const FieldDescriptor* key_field = map_desc->FindFieldByName("key");
  const FieldDescriptor* value_field = map_desc->FindFieldByName("value");
  if (key_field == nullptr || value_field == nullptr) {
    return absl::InternalError("Map entry missing field 'key' or 'value'");
  }

  const Reflection* msg_refl = field.msg->GetReflection();
  const int field_size = msg_refl->FieldSize(*field.msg, field.descriptor);
  for (int i = 0; i < field_size; ++i) {
    google::protobuf::Message* map_entry =
        msg_refl->MutableRepeatedMessage(field.msg, field.descriptor, i);
    const Reflection* refl = map_entry->GetReflection();

    switch (key.GetValueType()) {
      case Value::Type::kInteger: {
        int64_t search_key = key.GetInteger().value();
        switch (key_field->type()) {
          case FieldDescriptor::TYPE_UINT32:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_FIXED32:
            if (search_key == refl->GetUInt32(*map_entry, key_field)) {
              return InternalSetFieldValue(map_entry, value_field, value);
            }
            break;
          case FieldDescriptor::TYPE_INT32:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_SFIXED32:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_SINT32:
            if (search_key == refl->GetInt32(*map_entry, key_field)) {
              return InternalSetFieldValue(map_entry, value_field, value);
            }
            break;
          case FieldDescriptor::TYPE_INT64:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_SFIXED64:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_SINT64:
            if (search_key == refl->GetInt64(*map_entry, key_field)) {
              return InternalSetFieldValue(map_entry, value_field, value);
            }
            break;
          case FieldDescriptor::TYPE_UINT64:
            ABSL_FALLTHROUGH_INTENDED;
          case FieldDescriptor::TYPE_FIXED64: {
            uint64_t key_value = refl->GetUInt64(*map_entry, key_field);
            if (key_value <= std::numeric_limits<int64_t>::max() &&
                key_value == search_key) {
              return InternalSetFieldValue(map_entry, value_field, value);
            }
            break;
          }

          default:
            return absl::InvalidArgumentError(absl::StrFormat(
                "Map %s::%s key type is not an integer type",
                field.msg->GetDescriptor()->name(), field_path));
        }
        break;
      }
      // enum not supported as map key, hence treat symbol as string
      case Value::Type::kSymbol:
        ABSL_FALLTHROUGH_INTENDED;
      case Value::Type::kString:
        if (key_field->type() != FieldDescriptor::TYPE_STRING) {
          return absl::InvalidArgumentError(
              absl::StrFormat("Map %s::%s key type is not STRING",
                              field.msg->GetDescriptor()->name(), field_path));
        }
        if (refl->GetString(*map_entry, key_field) ==
            key.GetStringOrSymbol().value()) {
          return InternalSetFieldValue(map_entry, value_field, value);
        }
        break;
      default:
        return absl::InvalidArgumentError(
            absl::StrFormat("Map %s::%s key type must be STRING or INTEGER",
                            field.msg->GetDescriptor()->name(), field_path));
    }
  }

  google::protobuf::Message* map_entry =
      msg_refl->AddMessage(field.msg, field.descriptor);
  INTR_RETURN_IF_ERROR(InternalSetFieldValue(map_entry, key_field, key));
  INTR_RETURN_IF_ERROR(InternalSetFieldValue(map_entry, value_field, value));

  return absl::OkStatus();
}

absl::StatusOr<FieldFromPath> ProtobufManager::GetFieldFromPath(
    ProtoMessageId id, absl::string_view field_path, bool allow_index) {
  INTR_ASSIGN_OR_RETURN(const google::protobuf::Message* msg, GetProto(id));
  return intrinsic::executive::clips::GetFieldFromPath(msg, field_path,
                                                       allow_index);
}

absl::StatusOr<MutableFieldFromPath> ProtobufManager::GetMutableFieldFromPath(
    ProtoMessageId id, absl::string_view field_path, bool allow_index) {
  INTR_ASSIGN_OR_RETURN(google::protobuf::Message * msg, GetMutableProto(id));
  return intrinsic::executive::clips::GetMutableFieldFromPath(msg, field_path,
                                                              allow_index);
}

absl::StatusOr<Value> ProtobufManager::InternalGetFieldValue(
    const google::protobuf::Message& msg, const FieldDescriptor* field,
    const std::shared_ptr<DescriptorPoolInfoInternal>& pool_info)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_) {
  const Reflection* refl = msg.GetReflection();
  switch (field->type()) {
    case FieldDescriptor::TYPE_DOUBLE:
      return Value(refl->GetDouble(msg, field));
    case FieldDescriptor::TYPE_FLOAT:
      return Value(refl->GetFloat(msg, field));
    case FieldDescriptor::TYPE_UINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED32:
      return Value(refl->GetUInt32(msg, field));
    case FieldDescriptor::TYPE_INT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT32:
      return Value(refl->GetInt32(msg, field));
    case FieldDescriptor::TYPE_INT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT64:
      return Value(refl->GetInt64(msg, field));
    case FieldDescriptor::TYPE_UINT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED64: {
      uint64_t value = refl->GetUInt64(msg, field);
      if (value > std::numeric_limits<int64_t>::max()) {
        return absl::OutOfRangeError(absl::StrFormat(
            "Value of %s::%s too large: %d > %d", msg.GetDescriptor()->name(),
            field->name(), value, std::numeric_limits<int64_t>::max()));
      }
      return Value(static_cast<int64_t>(value));
    }
    case FieldDescriptor::TYPE_BOOL:
      return refl->GetBool(msg, field) ? Symbol::True() : Symbol::False();
    case FieldDescriptor::TYPE_STRING:
      return Value(refl->GetString(msg, field));
    case FieldDescriptor::TYPE_BYTES:
      return Value(refl->GetString(msg, field));
    case FieldDescriptor::TYPE_ENUM:
      return Symbol(refl->GetEnum(msg, field)->name());
    case FieldDescriptor::TYPE_MESSAGE: {
      INTR_ASSIGN_OR_RETURN(clips::ProtoMessageId field_msg_copy,
                            AddProto(refl->GetMessage(msg, field), pool_info));
      return Value(field_msg_copy.value());
    }
    default:
      return absl::InternalError("Unknown protobuf field encountered");
  }
}

absl::StatusOr<Value> ProtobufManager::InternalGetRepeatedFieldValue(
    const google::protobuf::Message& msg,
    const google::protobuf::FieldDescriptor* field, int index,
    const std::shared_ptr<DescriptorPoolInfoInternal>& pool_info) {
  const Reflection* refl = msg.GetReflection();
  if (!field->is_repeated()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("%s::%s[%d] is not a repeated field",
                        msg.GetDescriptor()->name(), field->name(), index));
  }

  const int field_size = refl->FieldSize(msg, field);
  if (index >= field_size) {
    return absl::OutOfRangeError(absl::StrFormat(
        "%s::%s[%d] out of bounds (size %d)", msg.GetDescriptor()->name(),
        field->name(), index, field_size));
  }
  if (index < -field_size) {
    return absl::OutOfRangeError(
        absl::StrFormat("%s::%s[%d] out of bounds (range %d to %d)",
                        msg.GetDescriptor()->name(), field->name(), index,
                        -field_size, field_size - 1));
  }

  // Handle [-1]-style indexes indicating "from the end" indices
  if (index < 0) {
    index = field_size + index;
  }

  switch (field->type()) {
    case FieldDescriptor::TYPE_DOUBLE:
      return Value(refl->GetRepeatedDouble(msg, field, index));
    case FieldDescriptor::TYPE_FLOAT:
      return Value(refl->GetRepeatedFloat(msg, field, index));
    case FieldDescriptor::TYPE_SFIXED32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_INT32:
      return Value(refl->GetRepeatedInt32(msg, field, index));
    case FieldDescriptor::TYPE_UINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED32:
      return Value(refl->GetRepeatedUInt32(msg, field, index));
    case FieldDescriptor::TYPE_INT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT64:
      return Value(refl->GetRepeatedInt64(msg, field, index));
    case FieldDescriptor::TYPE_UINT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED64:
      return Value(
          static_cast<int64_t>(refl->GetRepeatedUInt64(msg, field, index)));
    case FieldDescriptor::TYPE_BOOL:
      return refl->GetRepeatedBool(msg, field, index) ? Symbol::True()
                                                      : Symbol::False();
    case FieldDescriptor::TYPE_STRING:
      return Value(refl->GetRepeatedString(msg, field, index));
    case FieldDescriptor::TYPE_BYTES:
      return Value(refl->GetRepeatedString(msg, field, index));
    case FieldDescriptor::TYPE_ENUM:
      return Symbol(refl->GetRepeatedEnum(msg, field, index)->name());
    case FieldDescriptor::TYPE_MESSAGE: {
      INTR_ASSIGN_OR_RETURN(
          clips::ProtoMessageId msg_field_copy,
          AddProto(refl->GetRepeatedMessage(msg, field, index), pool_info));
      return Value(msg_field_copy.value());
    }
    default:
      return absl::InternalError(
          absl::StrFormat("Unknown protobuf field %s::%s[%d] encountered",
                          msg.GetDescriptor()->name(), field->name(), index));
  }
}

absl::Status ProtobufManager::InternalSetFieldValue(
    google::protobuf::Message* msg,
    const google::protobuf::FieldDescriptor* field, const Value& value) {
  const Reflection* refl = msg->GetReflection();
  switch (field->type()) {
    case FieldDescriptor::TYPE_DOUBLE: {
      INTR_ASSIGN_OR_RETURN(double double_value,
                            CheckAndGetFloat<double>(msg, field, value));
      refl->SetDouble(msg, field, double_value);
      break;
    }
    case FieldDescriptor::TYPE_FLOAT: {
      INTR_ASSIGN_OR_RETURN(float float_value,
                            CheckAndGetFloat<float>(msg, field, value));
      refl->SetFloat(msg, field, float_value);
      break;
    }
    case FieldDescriptor::TYPE_UINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED32: {
      INTR_ASSIGN_OR_RETURN(uint32_t int_value,
                            CheckAndGetInt<uint32_t>(msg, field, value));
      refl->SetUInt32(msg, field, int_value);
      break;
    }
    case FieldDescriptor::TYPE_INT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT32: {
      INTR_ASSIGN_OR_RETURN(int32_t int_value,
                            CheckAndGetInt<int32_t>(msg, field, value));
      refl->SetInt32(msg, field, int_value);
      break;
    }
    case FieldDescriptor::TYPE_INT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT64: {
      INTR_ASSIGN_OR_RETURN(int64_t int_value,
                            CheckAndGetInt<int64_t>(msg, field, value));
      refl->SetInt64(msg, field, int_value);
      break;
    }

    case FieldDescriptor::TYPE_UINT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED64: {
      INTR_ASSIGN_OR_RETURN(uint64_t int_value,
                            CheckAndGetInt<uint64_t>(msg, field, value));
      refl->SetUInt64(msg, field, int_value);
      break;
    }
    case FieldDescriptor::TYPE_BOOL:
      if (value != Symbol::True() && value != Symbol::False()) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Trying to set %s::%s from non-symbol or non-bool",
                            msg->GetDescriptor()->name(), field->name()));
      }
      refl->SetBool(msg, field, (value == Symbol::True()));
      break;
    case FieldDescriptor::TYPE_STRING:
      if (value.GetValueType() != Value::Type::kString &&
          value.GetValueType() != Value::Type::kSymbol) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Trying to set %s::%s from non-symbol and non-string",
            msg->GetDescriptor()->name(), field->name()));
      }
      refl->SetString(msg, field, value.GetStringOrSymbol().value());
      break;

    case FieldDescriptor::TYPE_BYTES:
      if (value.GetValueType() != Value::Type::kString) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Trying to set bytes %s::%s from non-string",
                            msg->GetDescriptor()->name(), field->name()));
      }
      refl->SetString(msg, field, value.GetString().value());
      break;

    case FieldDescriptor::TYPE_ENUM: {
      if (value.GetValueType() != Value::Type::kSymbol) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Trying to set enum %s::%s from non-symbol",
                            msg->GetDescriptor()->name(), field->name()));
      }
      const google::protobuf::EnumDescriptor* enum_desc = field->enum_type();
      const google::protobuf::EnumValueDescriptor* enum_val =
          enum_desc->FindValueByName(value.GetSymbol().value().ToString());
      if (enum_val == nullptr) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Trying to set enum %s::%s from invalid value %s",
                            msg->GetDescriptor()->name(), field->name(),
                            value.GetSymbol().value().ToString()));
      }
      refl->SetEnum(msg, field, enum_val);
      break;
    }
    case FieldDescriptor::TYPE_MESSAGE: {
      INTR_ASSIGN_OR_RETURN(int64_t msg_id, value.GetInteger());
      INTR_ASSIGN_OR_RETURN(
          const google::protobuf::Message* msg_value,
          GetProto(ProtoMessageId(msg_id)),
          _ << absl::StrFormat(
              "while retrieving proto value to set field %s::%s",
              field->containing_type()->name(), field->name()));
      google::protobuf::Message* mutable_msg = refl->MutableMessage(msg, field);
      if (mutable_msg->GetDescriptor()->full_name() ==
          msg_value->GetDescriptor()->full_name()) {
        static_cast<void>(mutable_msg->ParseFromString(
            msg_value->SerializeAsString()));  // TODO: Handle return value
      } else {
        return absl::InvalidArgumentError(absl::StrFormat(
            "Trying to set message %s::%s from invalid type %s instead of %s",
            msg->GetDescriptor()->name(), field->name(),
            msg_value->GetDescriptor()->name(),
            mutable_msg->GetDescriptor()->name()));
      }
      break;
    }
    default:
      return absl::InternalError("Unknown protobuf field encountered");
  }

  return absl::OkStatus();
}

absl::Status ProtobufManager::InternalSetFieldFromProto(
    google::protobuf::Message* msg,
    const google::protobuf::FieldDescriptor* field,
    const google::protobuf::Message* value,
    absl::string_view assigned_source_expression, absl::string_view full_path,
    const google::protobuf::Descriptor* full_message_descriptor) {
  const Reflection* refl = msg->GetReflection();
  switch (field->type()) {
    case FieldDescriptor::TYPE_DOUBLE: {
      INTR_ASSIGN_OR_RETURN(
          auto double_value,
          (CheckAndGetNumberFromProto<double, google::protobuf::DoubleValue,
                                      google::protobuf::FloatValue>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetDouble(msg, field, double_value);
      break;
    }
    case FieldDescriptor::TYPE_FLOAT: {
      INTR_ASSIGN_OR_RETURN(
          auto float_value,
          (CheckAndGetNumberFromProto<float, google::protobuf::FloatValue,
                                      google::protobuf::DoubleValue>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetFloat(msg, field, float_value);
      break;
    }
    case FieldDescriptor::TYPE_UINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED32: {
      INTR_ASSIGN_OR_RETURN(
          auto uint32_value,
          (CheckAndGetNumberFromProto<uint32_t, google::protobuf::UInt32Value,
                                      google::protobuf::Int32Value,
                                      google::protobuf::UInt64Value,
                                      google::protobuf::Int64Value>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetUInt32(msg, field, uint32_value);
      break;
    }
    case FieldDescriptor::TYPE_INT32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED32:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT32: {
      INTR_ASSIGN_OR_RETURN(
          auto int32_value,
          (CheckAndGetNumberFromProto<int32_t, google::protobuf::UInt32Value,
                                      google::protobuf::Int32Value,
                                      google::protobuf::UInt64Value,
                                      google::protobuf::Int64Value>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetInt32(msg, field, int32_value);
      break;
    }
    case FieldDescriptor::TYPE_INT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SFIXED64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_SINT64: {
      INTR_ASSIGN_OR_RETURN(
          auto int64_value,
          (CheckAndGetNumberFromProto<int64_t, google::protobuf::UInt32Value,
                                      google::protobuf::Int32Value,
                                      google::protobuf::UInt64Value,
                                      google::protobuf::Int64Value>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetInt64(msg, field, int64_value);
      break;
    }

    case FieldDescriptor::TYPE_UINT64:
      ABSL_FALLTHROUGH_INTENDED;
    case FieldDescriptor::TYPE_FIXED64: {
      INTR_ASSIGN_OR_RETURN(
          auto uint64_value,
          (CheckAndGetNumberFromProto<uint64_t, google::protobuf::UInt32Value,
                                      google::protobuf::Int32Value,
                                      google::protobuf::UInt64Value,
                                      google::protobuf::Int64Value>(
              msg, field, value, full_message_descriptor, full_path,
              assigned_source_expression)));
      refl->SetUInt64(msg, field, uint64_value);
      break;
    }
    case FieldDescriptor::TYPE_BOOL: {
      INTR_ASSIGN_OR_RETURN(auto bool_value,
                            ConvertMessageValueTo<google::protobuf::BoolValue>(
                                value, full_message_descriptor, full_path,
                                assigned_source_expression));
      refl->SetBool(msg, field, bool_value.value());
      break;
    }
    case FieldDescriptor::TYPE_STRING: {
      INTR_ASSIGN_OR_RETURN(
          auto string_value,
          ConvertMessageValueTo<google::protobuf::StringValue>(
              value, full_message_descriptor, full_path,
              assigned_source_expression));
      refl->SetString(msg, field, std::move(string_value.value()));
      break;
    }
    case FieldDescriptor::TYPE_BYTES: {
      INTR_ASSIGN_OR_RETURN(auto bytes_value,
                            ConvertMessageValueTo<google::protobuf::BytesValue>(
                                value, full_message_descriptor, full_path,
                                assigned_source_expression));
      refl->SetString(msg, field, bytes_value.value());
      break;
    }
    case FieldDescriptor::TYPE_ENUM: {
      return CreateStatus(
          15900,
          absl::StrFormat("Assignment to enum field is not supported. Failed "
                          "to assign from type '%s' at field path '%s' in '%s' "
                          "while trying to assign expression '%s'",
                          ProtoTypeNameFromMessage(value->GetDescriptor()),
                          full_path, full_message_descriptor->name(),
                          assigned_source_expression),
          absl::StatusCode::kUnimplemented,
          {.debug_message = GenerateAssignmentDebugMessage(
               nullptr, value, full_message_descriptor, full_path)});
    }
    case FieldDescriptor::TYPE_MESSAGE: {
      google::protobuf::Message* mutable_msg = refl->MutableMessage(msg, field);
      if (mutable_msg->GetDescriptor()->full_name() ==
          value->GetDescriptor()->full_name()) {
        if (!mutable_msg->ParseFromString(value->SerializeAsString())) {
          return GenerateAssignmentTypeError(
              15902, mutable_msg->GetDescriptor(), value,
              full_message_descriptor, full_path, assigned_source_expression);
        }
      } else {
        return GenerateAssignmentTypeError(
            15901, mutable_msg->GetDescriptor(), value, full_message_descriptor,
            full_path, assigned_source_expression);
      }
      break;
    }
    default:
      return CreateStatus(
          15900,
          absl::StrFormat("Encountered unknown field of type '%s' trying to "
                          "assign from type '%s' at field path '%s' in '%s' "
                          "while trying to assign expression '%s'",
                          field->type_name(),
                          ProtoTypeNameFromMessage(value->GetDescriptor()),
                          full_path, full_message_descriptor->name(),
                          assigned_source_expression),
          absl::StatusCode::kInternal,
          {.debug_message = GenerateAssignmentDebugMessage(
               nullptr, value, full_message_descriptor, full_path)});
  }

  return absl::OkStatus();
}

absl::StatusOr<Symbol> ProtobufManager::IsRepeated(
    ProtoMessageId id, absl::string_view field_path) {
  INTR_ASSIGN_OR_RETURN(FieldFromPath field, GetFieldFromPath(id, field_path));
  if (field.index.IsPresent()) {
    return InvalidArgumentErrorWithSymbol("Cannot use leaf index",
                                          "INVALID-INDEX");
  }

  return field.descriptor->is_repeated() ? Symbol::True() : Symbol::False();
}

absl::StatusOr<Symbol> ProtobufManager::HasField(ProtoMessageId id,
                                                 absl::string_view field_path) {
  absl::StatusOr<FieldFromPath> field = GetFieldFromPath(id, field_path);
  if (!field.ok()) {
    std::optional<intrinsic_proto::status::ExtendedStatus> es =
        GetExtendedStatus(field);
    if (es.has_value()) {
      uint32_t code = es->status_code().code();
      // GetFieldFromPath failed as the field was not in the path
      // => Just return False in the context of this function.
      // codes below 15800 and above 15809 are generic failures (e.g., invalid
      // field path) that should be reported.
      if (code > 15800 && code < 15809) {
        return Symbol::False();
      }
    }
    return field.status();
  }
  if (field->descriptor->is_repeated()) {
    return InvalidArgumentErrorWithSymbol(
        "Cannot call HasField on repeated field", "INVALID-REPEATED");
  }
  if (field->index.IsPresent()) {
    return InvalidArgumentErrorWithSymbol("Cannot use leaf index for HasField",
                                          "INVALID-INDEX");
  }
  if (field->descriptor->type() == FieldDescriptor::TYPE_MESSAGE ||
      field->descriptor->has_presence()) {
    const Reflection* refl = field->msg->GetReflection();
    return refl->HasField(*field->msg, field->descriptor) ? Symbol::True()
                                                          : Symbol::False();
  }
  return Symbol::True();
}

absl::StatusOr<Symbol> ProtobufManager::WhichOneof(
    ProtoMessageId id, absl::string_view field_path) {
  auto [stem, fieldname] = SplitPathStemAndField(field_path);
  const google::protobuf::Message* msg = nullptr;
  if (stem.empty()) {
    INTR_ASSIGN_OR_RETURN(msg, GetProto(id));
  } else {
    INTR_ASSIGN_OR_RETURN(FieldFromPath lfield,
                          GetFieldFromPath(id, stem, /*allow_index=*/false));
    if (lfield.descriptor->type() !=
        google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      return Symbol("INVALID-PATH");
    }
    const Reflection* lrefl = lfield.msg->GetReflection();
    if (lfield.index.IsAbsent()) {
      msg = &lrefl->GetMessage(*lfield.msg, lfield.descriptor);
    } else {
      msg = &lrefl->GetRepeatedMessage(*lfield.msg, lfield.descriptor,
                                       *lfield.index);
    }
  }
  const Descriptor* desc = msg->GetDescriptor();
  const OneofDescriptor* oneof = desc->FindOneofByName(fieldname);
  if (oneof == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Field %s::%s does not exist or is not a oneof",
                        desc->name(), field_path));
  }
  const Reflection* refl = msg->GetReflection();
  const FieldDescriptor* field = refl->GetOneofFieldDescriptor(*msg, oneof);
  if (field == nullptr) {
    return Symbol("NOT-SET");
  }
  return Symbol(field->name());
}

absl::StatusOr<std::string> ProtobufManager::ToString(ProtoMessageId id) {
  INTR_ASSIGN_OR_RETURN(const google::protobuf::Message* msg, GetProto(id),
                        _ << "while converting to string");
  return MessageToString(*msg, 0, 0, nullptr);
}

absl::StatusOr<std::string> ProtobufManager::ToStringWithPool(
    ProtoMessageId id, DescriptorPoolId pool_id, int64_t max_lines) {
  INTR_ASSIGN_OR_RETURN(const google::protobuf::Message* msg, GetProto(id),
                        _ << "while converting to string with pool");

  INTR_ASSIGN_OR_RETURN(DescriptorPoolInfo pool_info,
                        GetDescriptorPool(pool_id),
                        _ << "while converting to string with pool");

  AnyTypeDescriptorFinder any_type_finder(pool_info.descriptor_pool);
  return MessageToString(*msg, max_lines, 0, &any_type_finder);
}

absl::flat_hash_map<ProtoMessageId, std::string>
ProtobufManager::DebugGetAllProtosAsStrings() const
    ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  absl::flat_hash_map<ProtoMessageId, std::string> ret;
  for (const auto& [id, proto] : protos_) {
    ret[id] =
        absl::StrFormat("(type: %s):\n%v\n", proto->GetTypeName(), *proto);
  }
  return ret;
}

void ProtobufManager::DebugPrintAllProtos(size_t max_lines_per_proto,
                                          std::ostream* stream) const
    ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  for (const auto& [id, proto] : protos_) {
    *stream << "Proto " << id << " (type: " << proto->GetTypeName()
            << "):" << std::endl
            << ([max_lines_per_proto](const std::string& s) -> std::string {
                 if (max_lines_per_proto <= 0) return s;
                 std::vector<absl::string_view> l = absl::StrSplit(s, '\n');
                 return absl::StrJoin(
                     absl::MakeSpan(l).subspan(
                         0, std::min(l.size(), max_lines_per_proto)),
                     "\n");
               })(absl::StrCat(*proto))
            << std::endl
            << "==============================================================="
            << std::endl;
  }
}

void ProtobufManager::DebugPrintPools(
    absl::flat_hash_map<clips::DescriptorPoolId, std::string> pool_names,
    std::ostream* stream) {
  absl::MutexLock lock(protos_mutex_);
  *stream << "Current Pools:" << std::endl;
  for (const auto& [pool_id, pool_info] : pools_) {
    std::string pool_name;
    auto pool_find = pool_names.find(pool_id);
    if (pool_find != pool_names.end()) {
      pool_name = absl::StrFormat(" (%s)", pool_find->second);
    } else {
      pool_name = absl::StrFormat(" (%s)", pool_info->display_name);
    }

    *stream << "\nPool id: " << pool_id << pool_name << " ["
            << pool_info->type_url_prefix << "]["
            << pool_info->operation_name.value_or("<no_operation>") << "]"
            << std::endl;
    for (auto proto_iter = protos_.begin(); proto_iter != protos_.end();
         ++proto_iter) {
      const Descriptor* pool_descriptor =
          pool_info->descriptor_pool->FindMessageTypeByName(
              proto_iter->second->GetDescriptor()->full_name());
      if (pool_descriptor == nullptr) continue;
      if (proto_iter->second->GetDescriptor() == pool_descriptor) {
        *stream << "  Proto " << proto_iter->first << " of type "
                << proto_iter->second->GetDescriptor()->full_name()
                << " is in descriptor pool " << pool_id << std::endl
                << std::endl;
        if (proto_iter->second->GetTypeName() ==
            "google.protobuf.FileDescriptorSet") {
          *stream << "      FileDescriptorSet with:" << std::endl;
          const google::protobuf::Message* to_print = proto_iter->second.get();
          const google::protobuf::FileDescriptorSet* typed_proto =
              google::protobuf::DynamicCastMessage<
                  google::protobuf::FileDescriptorSet>(to_print);
          auto typed_proto_ptr =
              std::make_unique<google::protobuf::FileDescriptorSet>();
          if (typed_proto == nullptr) {
            std::string serialized_proto;
            if (to_print->SerializeToString(&serialized_proto)) {
              static_cast<void>(typed_proto_ptr->ParseFromString(
                  serialized_proto));  // TODO: Handle return value
            }
            typed_proto = typed_proto_ptr.get();
          }
          for (const auto& f : typed_proto->file()) {
            *stream << "          FileDescriptor for " << f.name() << std::endl;
          }
        } else {
          *stream << absl::StrCat(*proto_iter->second) << std::endl;
        }
      }
    }
  }
}

void ProtobufManager::RemoveProtosForPool(DescriptorPoolId pool_id)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_) {
  auto find_it = pools_.find(pool_id);
  if (find_it == pools_.end()) return;

  const std::shared_ptr<DescriptorPoolInfoInternal>& pool_info =
      find_it->second;

  absl::erase_if(
      protos_,
      [&pool_info, pool_id](
          const std::pair<const ProtoMessageId, ManagedProto>& proto_item) {
        if (proto_item.second.pool_info == pool_info) {
          LOG(WARNING) << "Removing proto " << proto_item.first << " of type "
                       << proto_item.second->GetDescriptor()->full_name()
                       << " because the associated descriptor pool " << pool_id
                       << " [" << pool_info->display_name << "]"
                       << " is being removed";
          return true;
        }
        return false;
      });
}

void ProtobufManager::RemoveOperationProtosAndPools(
    absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  if (operation_name.empty()) {
    LOG(ERROR) << "RemoveOperationProtosAndPools called with empty operation.";
  }
  absl::MutexLock lock(protos_mutex_);
  absl::flat_hash_set<DescriptorPoolId> pools_to_remove;
  for (const auto& [pool_id, pool_info] : pools_) {
    if (pool_info->operation_name != operation_name) {
      continue;
    }
    pools_to_remove.insert(pool_id);
  }
  for (const DescriptorPoolId pool_id : pools_to_remove) {
    RemoveDescriptorPoolNoLock(pool_id);
  }
}

absl::Status ProtobufManager::AddStandardMessageTypes(
    google::protobuf::FileDescriptorSet& file_descriptor_set) {
  // Generate a lookup_db from a copy of the FDS as file_descriptor_set will
  // be modified as part of this function, which would invalidate a database
  // when constructed from file_descriptor_set. Thus lookup_db will always
  // represent the original file_descriptor_set.
  google::protobuf::FileDescriptorSet lookup_fds = file_descriptor_set;
  auto lookup_db =
      std::make_unique<google::protobuf::SimpleDescriptorDatabase>();
  INTR_RETURN_IF_ERROR(PopulateDescriptorDatabase(lookup_db.get(), lookup_fds));

  // CEL required built-in types (e.g. wrappers.proto). No user should have
  // incompatible versions of this.
  google::protobuf::FileDescriptorSet standard_messages_fds =
      google::api::expr::runtime::GetStandardMessageTypesFileDescriptorSet();
  google::protobuf::FileDescriptorProto* any_list_file_descriptor =
      standard_messages_fds.add_file();
  // AnyList is an internal type required to store arbitrary lists as protos.
  // No user should have a different version of this.
  intrinsic_proto::executive::AnyList::descriptor()->file()->CopyTo(
      any_list_file_descriptor);

  // Special case for Any as AnyList imports an Any and AnyList is required
  // for CEL expression handling: The Any must be compatible and come from the
  // same file path. This is usually the case (in "google/protobuf/any.proto")
  // and thus should never happen.
  google::protobuf::FileDescriptorProto any_file_descriptor;
  if (lookup_db->FindFileContainingSymbol(
          std::string(google::protobuf::Any::descriptor()->full_name()),
          &any_file_descriptor)) {
    if (any_file_descriptor.name() !=
        google::protobuf::Any::descriptor()->file()->name()) {
      return absl::InvalidArgumentError(
          absl::StrFormat("The file descriptor set provides %s in an "
                          "unexpected file: %s (expected %s)",
                          google::protobuf::Any::descriptor()->full_name(),
                          any_file_descriptor.name(),
                          google::protobuf::Any::descriptor()->file()->name()));
    }
  }
  auto standard_messages_db =
      std::make_unique<google::protobuf::SimpleDescriptorDatabase>();
  INTR_RETURN_IF_ERROR(PopulateDescriptorDatabase(standard_messages_db.get(),
                                                  standard_messages_fds));

  // standard_messages_fds is now the FileDescriptorSet to add to
  // file_descriptor_set. Now check if these are compatible and then remove
  // duplicates from file_descriptor_set before merging in
  // standard_messages_fds.

  // Final invariant for this function to succeed:
  // The original file_descriptor_set represented by lookup_db must contain
  // either the same set of messages as the standard messages for each
  // matching file descriptor or not contain a matching file descriptor.
  //
  // A matching file descriptor is found when it contains the same symbol (not
  // just the same file name). This is done to be resilient for file path
  // changes with regard to g3 vs. insrc builds. Once it is guaranteed that
  // all file paths between the executive build and the 3P SDK (in particular
  // also the AnyList), file descriptors can be matched by name and the
  // matching just needs to ensure that the same messages are contained if a
  // file descriptors with a name in standard_messages_fds is already in the
  // lookup_db.
  //
  // Given that standard_messages_fds only contains standard messages there
  // are the following assumptions
  // * If a symbol is defined in standard_messages_fds and lookup_db then it
  // is compatible
  // * If lookup_db contains a symbol from standard_messages_fds then the
  // matching file descriptors must contain the same symbols.
  // * File paths of a file descriptor in standard_messages_fds vs. lookup_db
  // containing the same symbols can be different
  //
  // Here: Conservatively fail if the lookup_db is inconsistent with the well
  // known file descriptors in standard_messages_fds. These are well
  // established protos that should never change.

  // Record, which file descriptors will be removed by checking if they
  // contain a symbol from standard_messages_fds.
  absl::flat_hash_map<std::string, google::protobuf::FileDescriptorProto>
      fds_to_remove;
  for (const google::protobuf::FileDescriptorProto& file :
       standard_messages_fds.file()) {
    for (const google::protobuf::DescriptorProto& message :
         file.message_type()) {
      const std::string symbol_name =
          absl::StrCat(file.package(), ".", message.name());
      google::protobuf::FileDescriptorProto already_exists;
      if (lookup_db->FindFileContainingSymbol(symbol_name, &already_exists)) {
        fds_to_remove[already_exists.name()] = already_exists;
      }
    }
    for (const google::protobuf::EnumDescriptorProto& enum_type :
         file.enum_type()) {
      const std::string symbol_name =
          absl::StrCat(file.package(), ".", enum_type.name());
      google::protobuf::FileDescriptorProto already_exists;
      if (lookup_db->FindFileContainingSymbol(symbol_name, &already_exists)) {
        fds_to_remove[already_exists.name()] = already_exists;
      }
    }

    google::protobuf::FileDescriptorProto already_exists;
    // This literally is the same file descriptor in lookup_db, so we should
    // have found that in fds_to_remove from a matching symbol.
    if (lookup_db->FindFileByName(file.name(), &already_exists) &&
        !fds_to_remove.contains(file.name())) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "File descriptor %s is not to be removed, as it doesn't share any "
          "symbols with the standard messages, but a file descriptor exists "
          "in "
          "the standard messages descriptor set under the same name.",
          file.name()));
    }
  }

  // Verify that if a fd is to be removed due to having *a* symbol in
  // lookup_db, then all of its symbols must be in standard_messages_fds.
  // Otherwise a file descriptor in lookup_db provides extra descriptors not
  // in standard_messages_fds, which would be removed.
  for (const auto& [file_name, file] : fds_to_remove) {
    for (const google::protobuf::DescriptorProto& message :
         file.message_type()) {
      const std::string symbol_name =
          absl::StrCat(file.package(), ".", message.name());
      google::protobuf::FileDescriptorProto already_exists;
      if (!standard_messages_db->FindFileContainingSymbol(symbol_name,
                                                          &already_exists)) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "File descriptor %s has duplicate descriptors with standard "
            "messages, but it cannot be removed. "
            "It provides message '%s', which is not in the standard messages "
            "file descriptor set for the matching file descriptor.",
            file.name(), symbol_name));
      }
    }
    for (const google::protobuf::EnumDescriptorProto& enum_type :
         file.enum_type()) {
      const std::string symbol_name =
          absl::StrCat(file.package(), ".", enum_type.name());
      google::protobuf::FileDescriptorProto already_exists;
      if (!standard_messages_db->FindFileContainingSymbol(symbol_name,
                                                          &already_exists)) {
        return absl::InvalidArgumentError(absl::StrFormat(
            "File descriptor %s has duplicate descriptors with standard "
            "messages, but it cannot be removed. "
            "It provides enum '%s', which is not in the standard messages "
            "file descriptor set for the matching file descriptor.",
            file.name(), symbol_name));
      }
    }
  }

  // Now remove the fds_to_remove from file_descriptor_set.
  for (const auto& [file_name, fd] : fds_to_remove) {
    google::protobuf::RepeatedPtrField<google::protobuf::FileDescriptorProto>*
        files = file_descriptor_set.mutable_file();
    for (auto it = files->begin(); it != files->end();) {
      if (it->name() == file_name) {
        it = files->erase(it);
      } else {
        ++it;
      }
    }
  }

  // Finally add the standard messages file descriptors.
  for (const google::protobuf::FileDescriptorProto& standard_fd :
       standard_messages_fds.file()) {
    *file_descriptor_set.add_file() = standard_fd;
  }
  return absl::OkStatus();
}

absl::StatusOr<DescriptorPoolId> ProtobufManager::AddDescriptorPool(
    google::protobuf::FileDescriptorSet file_descriptor_set,
    std::string_view display_name, std::string_view type_url_prefix,
    std::string_view operation_name) {
  const DescriptorPoolId next_pool_id = GenerateNextDescriptorPoolId();

  INTR_RETURN_IF_ERROR(AddStandardMessageTypes(file_descriptor_set));

  auto db = std::make_unique<google::protobuf::SimpleDescriptorDatabase>();
  INTR_RETURN_IF_ERROR(
      PopulateDescriptorDatabase(db.get(), file_descriptor_set));
  auto pool = std::make_unique<google::protobuf::DescriptorPool>(db.get());
  auto msg_factory =
      std::make_unique<google::protobuf::DynamicMessageFactory>(pool.get());
  auto pool_info = std::make_shared<DescriptorPoolInfoInternal>(
      std::move(file_descriptor_set), std::move(db), std::move(pool),
      std::move(msg_factory), display_name, type_url_prefix, operation_name);
  pools_.emplace(next_pool_id, std::move(pool_info));

  if (type_url_prefix.empty()) {
    LOG(ERROR) << "Added descriptor pool '" << display_name
               << "' with empty type_url_prefix.";
  }

  VLOG(1) << "Added descriptor pool '" << display_name << "' with type URL: '"
          << type_url_prefix << "' under id: " << next_pool_id;

  return next_pool_id;
}

absl::Status ProtobufManager::AddGeneratedDescriptorPool() {
  auto pool_info = std::make_shared<DescriptorPoolInfoInternal>(
      google::protobuf::DescriptorPool::generated_pool(),
      google::protobuf::MessageFactory::generated_factory(), "Generated Pool",
      // TODO(b/438412095) If available switch to area known to proto
      // registry.
      kTypeUrlPrefix);
  generated_pool_info_ = pool_info;
  pools_.emplace(kGeneratedDescriptorPoolId, std::move(pool_info));
  return absl::OkStatus();
}

absl::Status ProtobufManager::AddStandardMessagesDescriptorPool() {
  google::protobuf::FileDescriptorSet fds;
  INTR_RETURN_IF_ERROR(AddStandardMessageTypes(fds));
  auto db = std::make_unique<google::protobuf::SimpleDescriptorDatabase>();
  INTR_RETURN_IF_ERROR(PopulateDescriptorDatabase(db.get(), fds));
  auto pool = std::make_unique<google::protobuf::DescriptorPool>(db.get());
  auto msg_factory =
      std::make_unique<google::protobuf::DynamicMessageFactory>(pool.get());
  auto pool_info = std::make_shared<DescriptorPoolInfoInternal>(
      std::move(fds), std::move(db), std::move(pool), std::move(msg_factory),
      "Built-in Standard Messages",
      // TODO(b/438412095) If available switch to area known to proto
      // registry.
      kTypeUrlPrefix, std::nullopt);
  pools_.emplace(kStandardMessagesDescriptorPoolId, std::move(pool_info));
  return absl::OkStatus();
}

absl::StatusOr<DescriptorPoolId> ProtobufManager::AddDescriptorPool(
    std::string_view file_descriptor_set_proto_file_path,
    std::string_view display_name, std::string_view type_url_prefix,
    std::string_view operation_name) {
  google::protobuf::FileDescriptorSet file_descriptor_set;
  INTR_RETURN_IF_ERROR(file::GetBinaryProto(file_descriptor_set_proto_file_path,
                                            &file_descriptor_set,
                                            file::Defaults()));
  return AddDescriptorPool(std::move(file_descriptor_set), display_name,
                           type_url_prefix, operation_name);
}

absl::StatusOr<DescriptorPoolId> ProtobufManager::AddDescriptorPool(
    ProtoMessageId file_descriptor_set_proto_id, std::string_view display_name,
    std::string_view type_url_prefix, std::string_view operation_name) {
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::FileDescriptorSet> file_descriptor_set,
      GetProtoAs<google::protobuf::FileDescriptorSet>(
          file_descriptor_set_proto_id));
  return AddDescriptorPool(std::move(*file_descriptor_set), display_name,
                           type_url_prefix, operation_name);
}

void ProtobufManager::RemoveDescriptorPoolNoLock(DescriptorPoolId id)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(protos_mutex_) {
  if (id == kInvalidDescriptorPoolId || id == kGeneratedDescriptorPoolId ||
      id == kStandardMessagesDescriptorPoolId) {
    std::string pool_name;
    switch (id.value()) {
      case kInvalidDescriptorPoolId.value():
        pool_name = "invalid";
        break;
      case kGeneratedDescriptorPoolId.value():
        pool_name = "generated";
        break;
      case kStandardMessagesDescriptorPoolId.value():
        pool_name = "standard messages";
        break;
    }
    LOG(ERROR) << "The " << pool_name << " descriptor pool cannot be removed";
    return;
  }
  RemoveProtosForPool(id);
  pools_.erase(id);
}

void ProtobufManager::RemoveDescriptorPool(DescriptorPoolId id)
    ABSL_LOCKS_EXCLUDED(protos_mutex_) {
  absl::MutexLock lock(protos_mutex_);
  RemoveDescriptorPoolNoLock(id);
}

ProtobufManager::DescriptorPoolInfo
ProtobufManager::DescriptorPoolInfoFromInternal(
    const DescriptorPoolInfoInternal& pool_info) {
  return DescriptorPoolInfo{.descriptor_pool = pool_info.descriptor_pool,
                            .message_factory = pool_info.message_factory,
                            .descriptor_db = pool_info.descriptor_db.get(),
                            .display_name = pool_info.display_name,
                            .type_url_prefix = pool_info.type_url_prefix};
}

absl::StatusOr<ProtobufManager::DescriptorPoolInfo>
ProtobufManager::GetDescriptorPool(DescriptorPoolId id) {
  auto find_it = pools_.find(id);
  if (find_it == pools_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("DescriptorPool %d unknown", id.value()));
  }
  return DescriptorPoolInfoFromInternal(*find_it->second);
}

absl::StatusOr<std::optional<google::protobuf::FileDescriptorSet>>
ProtobufManager::GetDescriptorPoolFileDescriptorSet(DescriptorPoolId id) {
  auto find_it = pools_.find(id);
  if (find_it == pools_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("DescriptorPool %d unknown", id.value()));
  }
  return find_it->second->file_descriptor_set;
}

absl::StatusOr<std::shared_ptr<ProtobufManager::DescriptorPoolInfoInternal>>
ProtobufManager::GetDescriptorPoolInternal(DescriptorPoolId id) {
  auto find_it = pools_.find(id);
  if (find_it == pools_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("DescriptorPool %d unknown", id.value()));
  }
  return find_it->second;
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
