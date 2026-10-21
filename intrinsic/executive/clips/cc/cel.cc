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

#include "intrinsic/executive/clips/cc/cel.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/distributions.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "cel/expr/syntax.pb.h"
#include "eval/public/activation.h"
#include "eval/public/cel_attribute.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"
#include "eval/public/structs/cel_proto_wrapper.h"
#include "eval/public/unknown_function_result_set.h"
#include "eval/public/unknown_set.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/arena.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor.pb.h"
#include "google/protobuf/duration.pb.h"
#include "google/protobuf/message.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "intrinsic/executive/clips/cc/cel_utils.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/any_list.pb.h"
#include "intrinsic/executive/proto/any_with_assignments.pb.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/get_extended_status.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "parser/parser.h"

namespace intrinsic::executive::clips {

constexpr char kCelCreate[] = "cel-create";
constexpr char kCelCreateFromProto[] = "cel-create-from-proto";
constexpr char kCelGetExpression[] = "cel-get-expression";
constexpr char kCelRemove[] = "cel-remove";
constexpr char kCelEvalCondition[] = "cel-eval-condition";
constexpr char kCelEvalToProtoWithType[] = "cel-eval-to-proto-with-type";
constexpr char kCelEvalToProtoList[] = "cel-eval-to-proto-list";
constexpr char kCelProtoAssign[] = "cel-proto-assign";

using intrinsic_proto::status::ExtendedStatus;

using ::google::api::expr::runtime::CelValue;

void FindIdentifiersInCelExpressionAsSet(
    const cel::expr::Expr& expr, absl::flat_hash_set<std::string>& identifiers,
    absl::flat_hash_set<std::string>& struct_messages,
    const absl::flat_hash_set<std::string>& bound_variables = {}) {
  switch (expr.expr_kind_case()) {
    case cel::expr::Expr::kIdentExpr:
      if (!bound_variables.contains(expr.ident_expr().name())) {
        identifiers.insert(expr.ident_expr().name());
      }
      break;
    case cel::expr::Expr::kSelectExpr:
      FindIdentifiersInCelExpressionAsSet(expr.select_expr().operand(),
                                          identifiers, struct_messages,
                                          bound_variables);
      break;
    case cel::expr::Expr::kCallExpr:
      FindIdentifiersInCelExpressionAsSet(expr.call_expr().target(),
                                          identifiers, struct_messages,
                                          bound_variables);
      for (int i = 0; i < expr.call_expr().args_size(); ++i) {
        FindIdentifiersInCelExpressionAsSet(expr.call_expr().args(i),
                                            identifiers, struct_messages,
                                            bound_variables);
      }
      break;
    case cel::expr::Expr::kListExpr:
      for (int i = 0; i < expr.list_expr().elements_size(); ++i) {
        FindIdentifiersInCelExpressionAsSet(expr.list_expr().elements(i),
                                            identifiers, struct_messages,
                                            bound_variables);
      }
      break;
    case cel::expr::Expr::kStructExpr:
      if (!expr.struct_expr().message_name().empty()) {
        struct_messages.insert(expr.struct_expr().message_name());
      }
      for (int i = 0; i < expr.struct_expr().entries_size(); ++i) {
        const cel::expr::Expr::CreateStruct::Entry& entry =
            expr.struct_expr().entries(i);
        if (entry.has_map_key()) {
          FindIdentifiersInCelExpressionAsSet(entry.map_key(), identifiers,
                                              struct_messages, bound_variables);
        }
        FindIdentifiersInCelExpressionAsSet(entry.value(), identifiers,
                                            struct_messages, bound_variables);
      }
      break;

    case cel::expr::Expr::kComprehensionExpr: {
      const cel::expr::Expr::Comprehension& compr = expr.comprehension_expr();
      // Bound variables within a comprehension expression are the incoming
      // bound variables + its iteration and result variables
      absl::flat_hash_set<std::string> comprehension_bound_variables(
          bound_variables);
      if (!compr.iter_var().empty()) {
        comprehension_bound_variables.insert(compr.iter_var());
      }
      if (!compr.iter_var2().empty()) {
        comprehension_bound_variables.insert(compr.iter_var2());
      }
      if (!compr.accu_var().empty()) {
        comprehension_bound_variables.insert(compr.accu_var());
      }
      FindIdentifiersInCelExpressionAsSet(compr.iter_range(), identifiers,
                                          struct_messages,
                                          comprehension_bound_variables);
      FindIdentifiersInCelExpressionAsSet(compr.accu_init(), identifiers,
                                          struct_messages,
                                          comprehension_bound_variables);
      FindIdentifiersInCelExpressionAsSet(compr.loop_condition(), identifiers,
                                          struct_messages,
                                          comprehension_bound_variables);
      FindIdentifiersInCelExpressionAsSet(compr.loop_step(), identifiers,
                                          struct_messages,
                                          comprehension_bound_variables);
      FindIdentifiersInCelExpressionAsSet(compr.result(), identifiers,
                                          struct_messages,
                                          comprehension_bound_variables);
    } break;

    default:
      break;
  }
}

absl::flat_hash_set<std::string> FindIdentifiersInCelExpression(
    const cel::expr::Expr& expr) {
  absl::flat_hash_set<std::string> ret;
  absl::flat_hash_set<std::string> unused;
  FindIdentifiersInCelExpressionAsSet(expr, ret, unused);
  return ret;
}

namespace {

absl::StatusOr<std::unique_ptr<google::protobuf::Message>> CreateMessage(
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    std::string_view message_name, absl::string_view expression,
    const CelValue& cel_value) {
  const google::protobuf::Descriptor* descriptor =
      pool_info.descriptor_pool->FindMessageTypeByName(message_name);
  if (descriptor == nullptr) {
    return CreateStatus(
        15102,
        absl::StrFormat(
            "The expression '%s' resulted in the message type '%s', "
            "which does not exist in the descriptor pool '%s'",
            expression, message_name, pool_info.display_name),
        absl::StatusCode::kNotFound,
        {.debug_message =
             absl::StrFormat("Full CEL value: %s", cel_value.DebugString())});
  }
  const google::protobuf::Message* message_prototype =
      pool_info.message_factory->GetPrototype(descriptor);
  if (message_prototype == nullptr) {
    return CreateStatus(
        15005,
        absl::StrFormat(
            "Failed to create a proto from the expression '%s' resulting in "
            "message type '%s' (using descriptor pool '%s')",
            expression, message_name, pool_info.display_name),
        absl::StatusCode::kInternal,
        {.debug_message =
             absl::StrFormat("Full CEL value: %s", cel_value.DebugString())});
  }
  return absl::WrapUnique(message_prototype->New());
}

// Convert a CelValue into a new google::protobuf::Message. The message will
// always be created from the given pool_info independent from the CelValue and
// its arena. Note: the CelValue may have data associated with some arena, which
// may go out of scope immediately after this function returns. Therefore, any
// data retrieved from the CelValue must be persisted by other means, i.e., no
// pointer (like one to a message) can be used beyond the scope of this
// function.
absl::StatusOr<std::unique_ptr<google::protobuf::Message>> CelValueToProto(
    const CelValue& cel_value, ProtobufManager* proto_mgr,
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    google::protobuf::Arena* arena, const std::string& expression) {
  switch (cel_value.type()) {
    case CelValue::Type::kNullType: {
      INTR_ASSIGN_OR_RETURN(std::unique_ptr<google::protobuf::Message> message,
                            CreateMessage(pool_info, "google.protobuf.Value",
                                          expression, cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field =
          message->GetDescriptor()->FindFieldByName("null_value");
      reflection->SetEnum(message.get(), field, field->default_value_enum());
      return message;
    }
    case CelValue::Type::kBool: {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, "google.protobuf.BoolValue", expression,
                        cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field =
          message->GetDescriptor()->FindFieldByName("value");
      reflection->SetBool(message.get(), field, cel_value.BoolOrDie());
      return message;
    }
    case CelValue::Type::kInt64: {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, "google.protobuf.Int64Value", expression,
                        cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field =
          message->GetDescriptor()->FindFieldByName("value");
      reflection->SetInt64(message.get(), field, cel_value.Int64OrDie());
      return message;
    }
    case CelValue::Type::kUint64: {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, "google.protobuf.UInt64Value", expression,
                        cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field =
          message->GetDescriptor()->FindFieldByName("value");
      reflection->SetUInt64(message.get(), field, cel_value.Uint64OrDie());
      return message;
    }
    case CelValue::Type::kDouble: {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, "google.protobuf.DoubleValue", expression,
                        cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field =
          message->GetDescriptor()->FindFieldByName("value");
      reflection->SetDouble(message.get(), field, cel_value.DoubleOrDie());
      return message;
    }
    case CelValue::Type::kString: {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, "google.protobuf.StringValue", expression,
                        cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field =
          message->GetDescriptor()->FindFieldByName("value");
      reflection->SetString(message.get(), field,
                            std::string(cel_value.StringOrDie().value()));
      return message;
    }
    case CelValue::Type::kBytes: {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, "google.protobuf.BytesValue", expression,
                        cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field =
          message->GetDescriptor()->FindFieldByName("value");
      reflection->SetString(message.get(), field,
                            std::string(cel_value.BytesOrDie().value()));
      return message;
    }
    case CelValue::Type::kMessage: {
      const google::protobuf::Message* cel_message = cel_value.MessageOrDie();
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, cel_message->GetDescriptor()->full_name(),
                        expression, cel_value));
      if (!message->ParseFromString(cel_message->SerializeAsString())) {
        return CreateStatus(
            15103,
            absl::StrFormat("The message of type '%s' created from the "
                            "expression '%s' could not be parsed into a "
                            "message from the  descriptor pool '%s'",
                            cel_message->GetDescriptor()->full_name(),
                            expression, pool_info.display_name),
            absl::StatusCode::kInvalidArgument,
            {.debug_message = absl::StrFormat("Full CEL value: %s",
                                              cel_value.DebugString())});
      }
      return message;
    }
    case CelValue::Type::kDuration: {
      INTR_ASSIGN_OR_RETURN(std::unique_ptr<google::protobuf::Message> message,
                            CreateMessage(pool_info, "google.protobuf.Duration",
                                          expression, cel_value));
      absl::Duration duration = cel_value.DurationOrDie();
      const int64_t s =
          absl::IDivDuration(duration, absl::Seconds(1), &duration);
      const int64_t n =
          absl::IDivDuration(duration, absl::Nanoseconds(1), &duration);
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field_seconds =
          message->GetDescriptor()->FindFieldByName("seconds");
      const google::protobuf::FieldDescriptor* field_nanos =
          message->GetDescriptor()->FindFieldByName("nanos");
      reflection->SetInt64(message.get(), field_seconds, s);
      reflection->SetInt32(message.get(), field_nanos, n);
      return message;
    }
    case CelValue::Type::kTimestamp: {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, "google.protobuf.Timestamp", expression,
                        cel_value));
      absl::Time t = cel_value.TimestampOrDie();
      const int64_t s = absl::ToUnixSeconds(t);
      const int64_t n = ((t - absl::FromUnixSeconds(s)) / absl::Nanoseconds(1));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field_seconds =
          message->GetDescriptor()->FindFieldByName("seconds");
      const google::protobuf::FieldDescriptor* field_nanos =
          message->GetDescriptor()->FindFieldByName("nanos");
      reflection->SetInt64(message.get(), field_seconds, s);
      reflection->SetInt32(message.get(), field_nanos, n);
      return message;
    }
    case CelValue::Type::kList: {
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> message,
          CreateMessage(pool_info, "intrinsic_proto.executive.AnyList",
                        expression, cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* items_field =
          message->GetDescriptor()->FindFieldByName("items");
      const google::api::expr::runtime::CelList& list = *cel_value.ListOrDie();
      for (int i = 0; i < list.size(); ++i) {
        CelValue sub_value = list.Get(arena, i);
        INTR_ASSIGN_OR_RETURN(
            std::unique_ptr<google::protobuf::Message> sub_message,
            CelValueToProto(sub_value, proto_mgr, pool_info, arena, expression),
            _.AttachExtendedStatus(CreateExtendedStatus(
                15104,
                absl::StrFormat("Failed to convert list value with "
                                "index %d from expression '%s'",
                                i, expression),
                {.debug_message =
                     absl::StrFormat("Full list item CEL value: %s",
                                     sub_value.DebugString())})));
        google::protobuf::Any local_any_proto;
        if (!local_any_proto.PackFrom(*sub_message)) {
          return absl::InternalError(
              "Failed to pack sub_message into local_any_proto.");
        }
        google::protobuf::Message* any_proto =
            reflection->AddMessage(message.get(), items_field);
        const google::protobuf::Reflection* any_reflection =
            any_proto->GetReflection();
        const google::protobuf::FieldDescriptor* type_url_field =
            any_proto->GetDescriptor()->FindFieldByName("type_url");
        const google::protobuf::FieldDescriptor* value_field =
            any_proto->GetDescriptor()->FindFieldByName("value");
        any_reflection->SetString(any_proto, type_url_field,
                                  AddTypeUrlPrefix(local_any_proto.type_url()));
        any_reflection->SetString(any_proto, value_field,
                                  local_any_proto.value());
      }
      return message;
    }
    case CelValue::Type::kMap: {
      INTR_ASSIGN_OR_RETURN(std::unique_ptr<google::protobuf::Message> message,
                            CreateMessage(pool_info, "google.protobuf.Struct",
                                          expression, cel_value));
      const google::protobuf::Reflection* reflection = message->GetReflection();
      const google::protobuf::FieldDescriptor* field =
          message->GetDescriptor()->FindFieldByName("fields");
      const google::api::expr::runtime::CelMap& cel_map = *cel_value.MapOrDie();
      INTR_ASSIGN_OR_RETURN(
          const auto* list_keys, cel_map.ListKeys(arena),
          _.AttachExtendedStatus(CreateExtendedStatus(
              15106,
              absl::StrFormat("Could not get keys from map for expression %s",
                              expression),
              {.debug_message = absl::StrFormat("Full CEL value: %s",
                                                cel_value.DebugString())})));
      const auto& keys = *list_keys;
      for (int i = 0; i < keys.size(); ++i) {
        std::string map_key;
        CelValue key_cel_value = keys.Get(arena, i);
        switch (key_cel_value.type()) {
          case CelValue::Type::kInt64:
            map_key = std::to_string(key_cel_value.Int64OrDie());
            break;
          case CelValue::Type::kUint64:
            map_key = std::to_string(key_cel_value.Uint64OrDie());
            break;
          case CelValue::Type::kString:
            map_key = key_cel_value.StringOrDie().value();
            break;
          default:
            return CreateStatus(
                15106,
                absl::StrFormat(
                    "The map key for expression '%s' had an unexpected type "
                    "'%s'. Must be int64, uint64, or string",
                    expression, CelValue::TypeName(key_cel_value.type())),
                absl::StatusCode::kInvalidArgument,
                {.debug_message = absl::StrFormat(
                     "Full key CEL value: %s", key_cel_value.DebugString())});
        }
        std::optional<CelValue> value = cel_map.Get(arena, key_cel_value);
        if (value.has_value()) {
          INTR_ASSIGN_OR_RETURN(
              std::unique_ptr<google::protobuf::Message> sub_message,
              CelValueToProto(*value, proto_mgr, pool_info, arena, expression),
              _.AttachExtendedStatus(CreateExtendedStatus(
                  15106,
                  absl::StrFormat("Failed to convert map value from "
                                  "expression '%s' for key: '%s'",
                                  expression, map_key),
                  {.debug_message = absl::StrFormat(
                       "Full map item CEL value: %s", value->DebugString())})));
          google::protobuf::Message* map_entry = reflection->AddMessage(
              message.get(), field, pool_info.message_factory);
          const google::protobuf::Reflection* map_entry_reflection =
              map_entry->GetReflection();
          const google::protobuf::FieldDescriptor* map_key_field =
              map_entry->GetDescriptor()->FindFieldByName("key");
          const google::protobuf::FieldDescriptor* map_value_field =
              map_entry->GetDescriptor()->FindFieldByName("value");
          map_entry_reflection->SetString(map_entry, map_key_field, map_key);
          map_entry_reflection->SetAllocatedMessage(
              map_entry, sub_message.release(), map_value_field);
        }
      }
      return message;
    }

    case CelValue::Type::kUnknownSet:
      ABSL_FALLTHROUGH_INTENDED;
    case CelValue::Type::kError:
      ABSL_FALLTHROUGH_INTENDED;
    default:
      return CreateStatus(
          15105,
          absl::StrFormat(
              "The expression '%s' resulted in an unexpected type: '%s'",
              expression, CelValue::TypeName(cel_value.type())),
          absl::StatusCode::kInvalidArgument,
          {.debug_message =
               absl::StrFormat("Full CEL value: %s", cel_value.DebugString())});
  }
}

absl::Status CheckErrorAndAppendUserMessage(
    const absl::Status& status, absl::string_view user_message_suffix) {
  if (status.ok()) {
    return absl::OkStatus();
  }
  std::optional<intrinsic_proto::status::ExtendedStatus> es =
      GetExtendedStatus(status);
  if (!es.has_value()) {
    LOG(WARNING) << "CheckErrorAndAppendUserMessage did not find an "
                    "ExtendedStatus to patch. Status: "
                 << status;
    return status;
  }
  absl::StrAppend(es->mutable_user_report()->mutable_message(),
                  user_message_suffix);
  absl::Status patched_status = status;
  patched_status.SetPayload(
      AddTypeUrlPrefix<intrinsic_proto::status::ExtendedStatus>(),
      es->SerializeAsCord());
  return patched_status;
}

}  // namespace

absl::StatusOr<std::unique_ptr<CelManager>> CelManager::Create(
    Environment* environment, ProtobufManager* protobuf_manager)
    ABSL_LOCKS_EXCLUDED(environment->mutex()) {
  auto cel_manager =
      absl::WrapUnique(new CelManager(environment, protobuf_manager));
  absl::MutexLock lock(cel_manager->env_->mutex());
  INTR_RETURN_IF_ERROR(cel_manager->RegisterFunctions());
  return cel_manager;
}

CelManager::CelManager(Environment* environment,
                       ProtobufManager* protobuf_manager)
    : env_(environment), proto_mgr_(protobuf_manager) {}

CelManager::~CelManager() {
  if (env_ != nullptr) {
    absl::MutexLock lock(env_->mutex());
    UnregisterFunctions();
  }
}

absl::Status CelManager::RegisterFunctions() {
  auto error_to_clips_result = [this](const absl::Status& status) -> Values {
    std::optional<ExtendedStatus> es = GetExtendedStatus(status);
    if (!es.has_value()) {
      es = CreateExtendedStatus(15000, status.ToString());
    }
    if (es->severity() == ExtendedStatus::DEFAULT) {
      // As this happens from an error handle set the severity accordingly if it
      // hasn't been labeled differently
      es->set_severity(ExtendedStatus::ERROR);
    }
    ProtoMessageId es_proto_id = proto_mgr_->AddGeneratedProto(std::move(*es));
    return {Symbol::False(), Value(es_proto_id.value())};
  };

  INTR_RETURN_IF_ERROR(RegisterFunction(
      kCelCreate, std::function([this](const std::string& expression,
                                       int64_t descriptor_pool_id) -> int64_t {
        INTR_ASSIGN_OR_RETURN(
            CelExpressionId expression_id,
            CreateExpressionFromString(expression,
                                       DescriptorPoolId(descriptor_pool_id)),
            _.LogError().With(Return(kInvalidCelExpressionId.value())));
        return expression_id.value();
      })));

  INTR_RETURN_IF_ERROR(RegisterFunction(
      kCelCreateFromProto,
      std::function([this](int64_t expr_id,
                           int64_t descriptor_pool_id) -> int64_t {
        INTR_ASSIGN_OR_RETURN(
            std::unique_ptr<cel::expr::Expr> expr,
            proto_mgr_->GetProtoAs<cel::expr::Expr>(ProtoMessageId(expr_id)),
            _.LogError().With(Return(kInvalidCelExpressionId.value())));

        INTR_ASSIGN_OR_RETURN(
            CelExpressionId expression_id,
            CreateExpressionFromProto(*expr,
                                      DescriptorPoolId(descriptor_pool_id)),
            _.LogError().With(Return(kInvalidCelExpressionId.value())));
        return expression_id.value();
      })));

  INTR_RETURN_IF_ERROR(RegisterFunction(
      kCelGetExpression, std::function([this](int64_t expression_id) {
        INTR_ASSIGN_OR_RETURN(
            std::string expression,
            GetExpressionAsString(CelExpressionId(expression_id)),
            _.LogError().With(
                Return(Values{Symbol("ERROR"), Value("Unknown expression")})));
        return Values{Value{expression}};
      })));

  INTR_RETURN_IF_ERROR(
      RegisterFunction(kCelRemove, std::function([this](int64_t expression_id) {
                         RemoveExpression(CelExpressionId(expression_id));
                       })));

  INTR_RETURN_IF_ERROR(RegisterFunction(
      kCelEvalCondition,
      std::function([this, error_to_clips_result](
                        int64_t expression_id,
                        const std::string& blackboard_scope,
                        const std::string& operation_name) -> clips::Values {
        // Callback from CLIPS must hold the mutex
        env_->mutex()->AssertHeld();
        INTR_ASSIGN_OR_RETURN(
            bool condition_outcome,
            EvaluateCondition(CelExpressionId(expression_id), blackboard_scope,
                              operation_name),
            _.LogWarning().With(error_to_clips_result));
        return {clips::Symbol::True(), condition_outcome
                                           ? clips::Symbol::True()
                                           : clips::Symbol::False()};
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kCelEvalToProtoWithType,
      std::function([this, error_to_clips_result](
                        const std::string& expression,
                        const std::string& expected_message_type,
                        int64_t descriptor_pool_id,
                        const std::string& blackboard_scope,
                        const std::string& operation_name) -> clips::Values {
        // Callback from CLIPS must hold the mutex
        env_->mutex()->AssertHeld();
        INTR_ASSIGN_OR_RETURN(
            clips::ProtoMessageId proto_id,
            EvaluateToProtoWithType(expression, expected_message_type,
                                    clips::DescriptorPoolId(descriptor_pool_id),
                                    blackboard_scope, operation_name),
            _.LogWarning().With(error_to_clips_result));
        return {clips::Symbol::True(), clips::Value(proto_id.value())};
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kCelEvalToProtoList,
      std::function([this, error_to_clips_result](
                        int64_t expression_id, int64_t descriptor_pool_id,
                        const std::string& blackboard_scope,
                        const std::string& operation_name) -> clips::Values {
        // Callback from CLIPS must hold the mutex
        env_->mutex()->AssertHeld();
        INTR_ASSIGN_OR_RETURN(
            std::vector<clips::ProtoMessageId> proto_list,
            EvaluateToProtoList(CelExpressionId(expression_id),
                                clips::DescriptorPoolId(descriptor_pool_id),
                                blackboard_scope, operation_name),
            _.LogWarning().With(error_to_clips_result));
        clips::Values result = {clips::Symbol::True()};
        for (const clips::ProtoMessageId& msg_id : proto_list) {
          result.push_back(clips::Value(msg_id.value()));
        }
        return result;
      })));
  INTR_RETURN_IF_ERROR(RegisterFunction(
      kCelProtoAssign,
      std::function([this, error_to_clips_result](
                        int64_t proto_id, int64_t descriptor_pool_id,
                        const std::string& parameter_message_name,
                        const std::string& blackboard_scope,
                        const std::string& operation_name) -> clips::Values {
        // Callback from CLIPS must hold the mutex
        env_->mutex()->AssertHeld();
        INTR_RETURN_IF_ERROR(
            AssignProto(clips::ProtoMessageId(proto_id),
                        clips::DescriptorPoolId(descriptor_pool_id),
                        parameter_message_name, blackboard_scope,
                        operation_name))
            .LogWarning()
            .With(error_to_clips_result);
        return {clips::Symbol::True(), clips::Value("")};
      })));

  return absl::OkStatus();
}

void CelManager::UnregisterFunctions() {
  for (const std::string& f : functions_) {
    if (absl::Status s(env_->RemoveFunction(f)); !s.ok()) {
      LOG(ERROR) << "Failed to remove " << f << ": " << s;
    }
  }
  functions_.clear();
}

CelExpressionId CelManager::GenerateNextCelExpressionId() {
  CelExpressionId new_id = kInvalidCelExpressionId;
  do {
    // shift to fit into int64
    new_id =
        CelExpressionId(absl::Uniform<uint64_t>(id_random_generator_) >> 1);
  } while (new_id == kInvalidCelExpressionId || expressions_.contains(new_id));
  return new_id;
}

absl::StatusOr<CelExpressionId> CelManager::CreateExpressionFromProto(
    const cel::expr::Expr& expression_proto,
    DescriptorPoolId descriptor_pool_id) {
  cel::expr::ParsedExpr pe;
  *pe.mutable_expr() = expression_proto;
  // Note that this can fail (intentionally) because only a subset of the CEL
  // syntax is supported by the unparser for reasons of simplicity and security.
  // The full CEL syntax is available via CreateExpressionFromString().
  INTR_ASSIGN_OR_RETURN(
      std::string expression,
      intrinsic::executive::clips::UnparseCelExpression(expression_proto));
  return CreateExpression(expression, std::move(pe), descriptor_pool_id);
}

absl::StatusOr<cel::expr::ParsedExpr> CelManager::ParseExpression(
    std::string_view expression) {
  if (expression.empty()) {
    return CreateStatus(
        15150, "Failed to parse expression as the expression is empty.",
        absl::StatusCode::kInvalidArgument);
  }
  absl::StatusOr<cel::expr::ParsedExpr> parsed_expr =
      google::api::expr::parser::Parse(expression);
  if (!parsed_expr.ok()) {
    // Show the parser error in a code block as CEL highlights where
    // the error is in the expression.
    return CreateStatus(15150,
                        absl::StrFormat(
                            R"(Failed to parse expression '%s'. Error:
```
%s
```
)",
                            expression, parsed_expr.status().message()),
                        absl::StatusCode::kInvalidArgument);
  }
  return *parsed_expr;
}

absl::StatusOr<CelExpressionId> CelManager::CreateExpressionFromString(
    std::string_view expression, DescriptorPoolId descriptor_pool_id) {
  INTR_ASSIGN_OR_RETURN(cel::expr::ParsedExpr parsed_expr,
                        ParseExpression(expression));
  return CreateExpression(expression, std::move(parsed_expr),
                          descriptor_pool_id);
}

absl::StatusOr<CelManager::ExpressionData>
CelManager::CreateExpressionFromString(
    std::string_view expression,
    const ProtobufManager::DescriptorPoolInfo& pool_info) {
  INTR_ASSIGN_OR_RETURN(cel::expr::ParsedExpr parsed_expr,
                        ParseExpression(expression));
  return CreateExpression(expression, std::move(parsed_expr), pool_info);
}

absl::StatusOr<CelExpressionId> CelManager::CreateExpression(
    std::string_view expression, cel::expr::ParsedExpr&& parsed_expr,
    DescriptorPoolId descriptor_pool_id) {
  INTR_ASSIGN_OR_RETURN(
      ProtobufManager::DescriptorPoolInfo pool_info,
      proto_mgr_->GetDescriptorPool(descriptor_pool_id),
      _.AttachExtendedStatus(CreateExtendedStatus(
          15004, absl::StrFormat("Could not retrieve descriptor pool to create "
                                 "expression '%s' (internal id: %d)",
                                 expression, descriptor_pool_id.value()))));

  INTR_ASSIGN_OR_RETURN(
      ExpressionData expr_data,
      CreateExpression(expression, std::move(parsed_expr), pool_info));

  CelExpressionId new_id = GenerateNextCelExpressionId();
  expressions_[new_id] = std::move(expr_data);
  return new_id;
}

absl::StatusOr<CelManager::ExpressionData> CelManager::CreateExpression(
    std::string_view expression, cel::expr::ParsedExpr&& parsed_expr,
    const ProtobufManager::DescriptorPoolInfo& pool_info) {
  ExpressionData expr_data{
      .expression = std::string(expression),
      .parsed_expr = parsed_expr,
  };
  FindIdentifiersInCelExpressionAsSet(expr_data.parsed_expr.expr(),
                                      expr_data.identifiers_set,
                                      expr_data.struct_messages_set);

  // Verify this prior to creating the CEL expression objects as these will fail
  // to be able to create a distinct error message.
  for (const std::string& struct_message_name : expr_data.struct_messages_set) {
    const google::protobuf::Descriptor* descriptor =
        pool_info.descriptor_pool->FindMessageTypeByName(struct_message_name);
    if (descriptor == nullptr) {
      return CreateStatus(
          15102,
          absl::StrFormat("The expression '%s' creates message type '%s', "
                          "which does not exist in the descriptor pool '%s'",
                          expression, struct_message_name,
                          pool_info.display_name),
          absl::StatusCode::kNotFound);
    }
  }

  INTR_ASSIGN_OR_RETURN(
      expr_data.cel_expr_builder, CreateCelExpressionBuilder(pool_info),
      _.WrapExtendedStatus(
          CreateExtendedStatus(
              15006,
              absl::StrFormat(
                  "Failed to create expression builder for expression '%s'",
                  expression)),
          intrinsic::StatusBuilder::LEGACY_AS_DEBUG_REPORT));

  absl::StatusOr<std::unique_ptr<google::api::expr::runtime::CelExpression>>
      cel_expression = expr_data.cel_expr_builder->CreateExpression(
          &expr_data.parsed_expr.expr(), &expr_data.parsed_expr.source_info());
  if (!cel_expression.ok()) {
    return CreateStatus(
        15007,
        absl::StrFormat("Failed to create expression '%s'. Error: %s",
                        expression, cel_expression.status().message()),
        absl::StatusCode::kInvalidArgument);
  }
  expr_data.cel_expression = std::move(*cel_expression);

  return expr_data;
}

void CelManager::RemoveExpression(CelExpressionId id) {
  expressions_.erase(id);
}

absl::StatusOr<std::reference_wrapper<const CelManager::ExpressionData>>
CelManager::GetExpressionData(CelExpressionId id) const {
  auto data_it = expressions_.find(id);
  if (data_it == expressions_.end()) {
    return CreateStatus(
        15001,
        absl::StrFormat(
            "No expression data was found for the expression. Internal id: %d",
            id.value()),
        absl::StatusCode::kInternal);
  }
  return data_it->second;
}

absl::StatusOr<std::string> CelManager::GetExpressionAsString(
    CelExpressionId id) const {
  INTR_ASSIGN_OR_RETURN(const ExpressionData& data, GetExpressionData(id));
  return data.expression;
}

absl::StatusOr<absl::flat_hash_map<std::string, ProtoMessageId>>
CelManager::CreateProtoMapFromBlackboard(
    const absl::flat_hash_set<std::string>& identifiers,
    std::string_view blackboard_scope, std::string_view operation_name,
    std::string_view expression) ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  absl::flat_hash_map<std::string, ProtoMessageId> proto_map;
  proto_map.reserve(identifiers.size());

  std::string scope_context;
  if (blackboard_scope != "PROCESS_TREE") {
    scope_context = absl::StrCat(" in scope '", blackboard_scope, "'");
  }
  // Do no exit early with an error, but collect all first and build an error
  // out of that.
  // This allows to report multiple missing values in a single error.
  absl::flat_hash_set<std::string> missing_blackboard_items;
  std::vector<std::string> blackboard_item_errors;
  for (const std::string& identifier : identifiers) {
    absl::StatusOr<clips::Fact> blackboard_item = env_->GetUniqueFact(
        "blackboard-item", {{"key", identifier},
                            {"scope", blackboard_scope},
                            {"operation-name", operation_name}});
    if (!blackboard_item.ok()) {
      missing_blackboard_items.insert(identifier);
      blackboard_item_errors.push_back(
          absl::StrFormat("The blackboard variable '%s' does not exist%s.",
                          identifier, scope_context));
      continue;
    }
    absl::StatusOr<clips::Value> proto_id_val =
        blackboard_item->GetSlotValue("proto-id");
    if (!proto_id_val.ok()) {
      blackboard_item_errors.push_back(
          absl::StrFormat("The blackboard item for %s is invalid: %s",
                          identifier, proto_id_val.status().message()));
      continue;
    }
    absl::StatusOr<int64_t> proto_id = proto_id_val->GetInteger();
    if (!proto_id.ok()) {
      blackboard_item_errors.push_back(absl::StrFormat(
          "The blackboard item for %s does not have a valid proto: %s",
          identifier, proto_id.status().message()));
      continue;
    }
    if (*proto_id == clips::ProtobufManager::kInvalidId.value()) {
      blackboard_item_errors.push_back(absl::StrFormat(
          "The blackboard item for %s does not have a valid proto as the id "
          "was invalid (0).",
          identifier));
      continue;
    }
    proto_map[identifier] = ProtoMessageId(*proto_id);
  }

  // proto_map has been built. Report errors if present.
  if (!missing_blackboard_items.empty() &&
      missing_blackboard_items.size() == blackboard_item_errors.size()) {
    // The only errors are missing blackboard items.
    // This is the usual error case for user errors, i.e., the blackboard item
    // wasn't written from the process.
    return CreateStatus(
        15101,
        absl::StrFormat("The expression '%s' refers to the following "
                        "blackboard variables that do not exist%s: %s",
                        expression, scope_context,
                        absl::StrJoin(missing_blackboard_items, ", ")),
        absl::StatusCode::kFailedPrecondition);
  }
  if (!blackboard_item_errors.empty() &&
      missing_blackboard_items.size() != blackboard_item_errors.size()) {
    // There are errors besides missing_blackboard_items
    return CreateStatus(
        15003,
        absl::StrFormat(
            "Failed to determine the variables for expression '%s'. The "
            "following errors have been reported: %s",
            expression, absl::StrJoin(blackboard_item_errors, ", ")),
        absl::StatusCode::kInternal);
  }
  return proto_map;
}

absl::StatusOr<google::api::expr::runtime::Activation>
CelManager::CreateActivationFromBlackboard(
    const absl::flat_hash_set<std::string>& identifiers,
    std::string_view blackboard_scope, std::string_view operation_name,
    std::string_view expression, google::protobuf::Arena* arena)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      (absl::flat_hash_map<std::string, ProtoMessageId> proto_map),
      CreateProtoMapFromBlackboard(identifiers, blackboard_scope,
                                   operation_name, expression));

  google::api::expr::runtime::Activation act;
  for (const auto& [identifier, proto_id] : proto_map) {
    INTR_ASSIGN_OR_RETURN(
        const google::protobuf::Message* message,
        proto_mgr_->GetProto(proto_id),
        _
            .AttachExtendedStatus(CreateExtendedStatus(
                15002,
                absl::StrFormat(
                    "While preparing to evaluate '%s' the proto message for "
                    "the value '%s' was not found under its internal id: %d",
                    expression, identifier, proto_id.value())))
            .LogError());
    act.InsertValue(identifier,
                    google::api::expr::runtime::CelProtoWrapper::CreateMessage(
                        message, arena));
  }
  return act;
}

absl::Status CelManager::EnsureErrorFreeCelValue(
    const absl::StatusOr<CelValue>& cel_value, std::string_view expression) {
  if (!cel_value.ok()) {
    return CreateStatus(
        15100,
        absl::StrFormat("Failure encountered during evaluations of expression "
                        "'%s'. Error: %s",
                        expression, cel_value.status().message()),
        absl::StatusCode::kAborted);
  }

  if (cel_value->IsError()) {
    return CreateStatus(
        15100,
        absl::StrFormat("Evaluating expression '%s' reported an error: %s",
                        expression, cel_value->ErrorOrDie()->message()),
        absl::StatusCode::kAborted,
        {.debug_message =
             absl::StrFormat("Full CEL value: %s", cel_value->DebugString())});
  } else if (cel_value->IsUnknownSet()) {
    return CreateStatus(
        15100,
        absl::StrFormat(
            "Evaluating expression '%s' reported unknowns set. Unknown "
            "attributes: [%s], unknown function results: [%s]",
            expression,
            absl::StrJoin(
                cel_value->UnknownSetOrDie()->unknown_attributes(), ", ",
                [](std::string* out,
                   const google::api::expr::runtime::CelAttribute& attr) {
                  if (auto s = attr.AsString(); s.ok()) {
                    absl::StrAppend(out, s.value());
                  }
                  absl::StrAppend(out, "Unknown Attribute");
                }),
            absl::StrJoin(
                cel_value->UnknownSetOrDie()->unknown_function_results(), ", ",
                [](std::string* out,
                   const google::api::expr::runtime::UnknownFunctionResult&
                       ur) {
                  absl::StrAppend(
                      out, absl::StrFormat("%s(...)", ur.descriptor().name()));
                })),
        absl::StatusCode::kAborted,
        {.debug_message =
             absl::StrFormat("Full CEL value: %s", cel_value->DebugString())});
  }

  return absl::OkStatus();
}

absl::StatusOr<CelValue> CelManager::EvaluateExpressionOnBlackboard(
    CelExpressionId id, std::string_view blackboard_scope,
    std::string_view operation_name, google::protobuf::Arena* arena)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  INTR_ASSIGN_OR_RETURN(const ExpressionData& data, GetExpressionData(id));
  return EvaluateExpressionOnBlackboard(data, blackboard_scope, operation_name,
                                        arena);
}

absl::StatusOr<CelValue> CelManager::EvaluateExpressionOnBlackboard(
    const ExpressionData& expr_data, std::string_view blackboard_scope,
    std::string_view operation_name, google::protobuf::Arena* arena)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  INTR_ASSIGN_OR_RETURN(google::api::expr::runtime::Activation act,
                        CreateActivationFromBlackboard(
                            expr_data.identifiers_set, blackboard_scope,
                            operation_name, expr_data.expression, arena));
  absl::StatusOr<CelValue> cel_result =
      expr_data.cel_expression->Evaluate(act, arena);

  INTR_RETURN_IF_ERROR(
      EnsureErrorFreeCelValue(cel_result, expr_data.expression));

  return cel_result;
}

absl::StatusOr<bool> CelManager::EvaluateCondition(
    CelExpressionId id, std::string_view blackboard_scope,
    std::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  google::protobuf::Arena arena;
  INTR_ASSIGN_OR_RETURN(CelValue cel_value,
                        EvaluateExpressionOnBlackboard(id, blackboard_scope,
                                                       operation_name, &arena));

  if (cel_value.type() == CelValue::Type::kBool) {
    return cel_value.BoolOrDie();
  }

  INTR_ASSIGN_OR_RETURN(const ExpressionData& data, GetExpressionData(id));
  return CreateStatus(
      15700,
      absl::StrFormat(
          "Condition expression '%s' returned type '%s' instead of 'bool'",
          data.expression, CelValue::TypeName(cel_value.type())),
      absl::StatusCode::kInvalidArgument,
      {.debug_message =
           absl::StrFormat("Full CEL value: %s", cel_value.DebugString())});
}

absl::StatusOr<clips::ProtoMessageId> CelManager::EvaluateToProtoWithType(
    const std::string& expression, const std::string& expected_message_type,
    clips::DescriptorPoolId descriptor_pool_id,
    const std::string& blackboard_scope, const std::string& operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      ProtobufManager::DescriptorPoolInfo pool_info,
      proto_mgr_->GetDescriptorPool(descriptor_pool_id),
      _.AttachExtendedStatus(CreateExtendedStatus(
          15004, absl::StrFormat("Could not retrieve descriptor pool for "
                                 "expression '%s' (internal id: %d)",
                                 expression, descriptor_pool_id.value()))));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> message,
      EvaluateToProtoWithType(expression, expected_message_type, pool_info,
                              blackboard_scope, operation_name));

  INTR_ASSIGN_OR_RETURN(
      clips::ProtoMessageId evaluate_result,
      proto_mgr_->AddProto(std::move(message), descriptor_pool_id),
      _.WrapExtendedStatus(
          CreateExtendedStatus(
              15008, absl::StrFormat("Failed to store result of expression "
                                     "'%s' in pool '%s' (internal id: %d)",
                                     expression, pool_info.display_name,
                                     descriptor_pool_id.value())),
          intrinsic::StatusBuilder::LEGACY_AS_DEBUG_REPORT));
  return evaluate_result;
}

absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
CelManager::EvaluateToProtoWithType(
    const std::string& expression, absl::string_view expected_message_type,
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    absl::string_view blackboard_scope, absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  INTR_ASSIGN_OR_RETURN(ExpressionData expr_data,
                        CreateExpressionFromString(expression, pool_info));

  google::protobuf::Arena arena;
  INTR_ASSIGN_OR_RETURN(CelValue cel_value, EvaluateExpressionOnBlackboard(
                                                expr_data, blackboard_scope,
                                                operation_name, &arena));

  // The following resolves any data that could have been contained in the arena
  // to a copy owned by the DescriptorPoolInfo. Therefore, deleting the arena at
  // the end of the scope is safe.
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> message,
      CelValueToProto(cel_value, proto_mgr_, pool_info, &arena, expression));

  if (!expected_message_type.empty() &&
      message->GetDescriptor()->full_name() != expected_message_type) {
    return CreateStatus(
        15750,
        absl::StrFormat(
            "Expression '%s' returned type '%s' instead of the expected '%s'",
            expression, message->GetDescriptor()->full_name(),
            expected_message_type),
        absl::StatusCode::kInvalidArgument,
        {.debug_message =
             absl::StrFormat("Full CEL value: %s", cel_value.DebugString())});
  }

  return std::move(message);
}

absl::StatusOr<std::vector<clips::ProtoMessageId>>
CelManager::EvaluateToProtoList(CelExpressionId expression_id,
                                clips::DescriptorPoolId descriptor_pool_id,
                                const std::string& blackboard_scope,
                                const std::string& operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  INTR_ASSIGN_OR_RETURN(const ExpressionData& data,
                        GetExpressionData(expression_id));
  INTR_ASSIGN_OR_RETURN(
      ProtobufManager::DescriptorPoolInfo pool_info,
      proto_mgr_->GetDescriptorPool(descriptor_pool_id),
      _.AttachExtendedStatus(CreateExtendedStatus(
          15004,
          absl::StrFormat("Could not retrieve descriptor pool for "
                          "expression '%s' (internal id: %d)",
                          data.expression, descriptor_pool_id.value()))));

  INTR_ASSIGN_OR_RETURN(
      std::vector<std::unique_ptr<google::protobuf::Message>> proto_messages,
      EvaluateToProtoList(data, pool_info, blackboard_scope, operation_name));

  std::vector<ProtoMessageId> added_proto_messages;
  for (int i = 0; i < proto_messages.size(); ++i) {
    INTR_ASSIGN_OR_RETURN(
        clips::ProtoMessageId added_proto,
        proto_mgr_->AddProto(std::move(proto_messages[i]), descriptor_pool_id),
        _.WrapExtendedStatus(
            CreateExtendedStatus(
                15008, absl::StrFormat("Failed to store result of expression "
                                       "'%s' in pool '%s' (internal id: %d)",
                                       data.expression, pool_info.display_name,
                                       descriptor_pool_id.value())),
            intrinsic::StatusBuilder::LEGACY_AS_DEBUG_REPORT));
    if (added_proto == clips::ProtobufManager::kInvalidId) {
      return CreateStatus(
          13720,
          absl::StrFormat(
              "Failed to add list proto at index %d from expression '%s'", i,
              data.expression),
          absl::StatusCode::kInternal);
    }
    added_proto_messages.push_back(added_proto);
  }

  return added_proto_messages;
}

absl::StatusOr<std::vector<std::unique_ptr<google::protobuf::Message>>>
CelManager::EvaluateToProtoList(
    const ExpressionData& expression_data,
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    absl::string_view blackboard_scope, absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  google::protobuf::Arena arena;
  INTR_ASSIGN_OR_RETURN(
      CelValue cel_value,
      EvaluateExpressionOnBlackboard(expression_data, blackboard_scope,
                                     operation_name, &arena));
  std::vector<std::unique_ptr<google::protobuf::Message>> proto_messages;
  if (cel_value.IsList()) {
    const google::api::expr::runtime::CelList& list = *cel_value.ListOrDie();
    proto_messages.reserve(list.size());
    for (int i = 0; i < list.size(); ++i) {
      CelValue sub_value = list.Get(&arena, i);
      INTR_ASSIGN_OR_RETURN(
          std::unique_ptr<google::protobuf::Message> sub_message,
          CelValueToProto(sub_value, proto_mgr_, pool_info, &arena,
                          expression_data.expression),
          _.AttachExtendedStatus(CreateExtendedStatus(
              15104,
              absl::StrFormat("Failed to convert list value with "
                              "index %d from expression '%s'",
                              i, expression_data.expression),
              {.debug_message = absl::StrFormat("Full list item CEL value: %s",
                                                sub_value.DebugString())})));
      proto_messages.push_back(std::move(sub_message));
    }
  } else if (cel_value.IsMessage()) {
    const google::protobuf::Message* cel_message = cel_value.MessageOrDie();
    if (cel_message->GetDescriptor()->full_name() !=
        intrinsic_proto::executive::AnyList::descriptor()->full_name()) {
      return CreateStatus(
          15751,
          absl::StrFormat(
              "Proto list expression resulted in type '%s' instead "
              "of an intrinsic_proto.executive.AnyList created "
              "from a list of protos while evaluating expression '%s'",
              cel_message->GetDescriptor()->full_name(),
              expression_data.expression),
          absl::StatusCode::kInvalidArgument,
          {.debug_message = absl::StrFormat("Full CEL value: %s.",
                                            cel_value.DebugString())});
    }

    INTR_ASSIGN_OR_RETURN(proto_messages, proto_mgr_->ConvertAnyListToMessages(
                                              cel_message, pool_info,
                                              expression_data.expression));
  } else {
    return CreateStatus(15751,
                        absl::StrFormat("Proto list expression '%s' returned "
                                        "type '%s' instead of a list of protos",
                                        expression_data.expression,
                                        CelValue::TypeName(cel_value.type())),
                        absl::StatusCode::kInvalidArgument,
                        {.debug_message = absl::StrFormat(
                             "Full CEL value: %s", cel_value.DebugString())});
  }

  return proto_messages;
}

absl::Status CelManager::AssignProto(clips::ProtoMessageId proto_id,
                                     clips::DescriptorPoolId descriptor_pool_id,
                                     const std::string& parameter_message_name,
                                     const std::string& blackboard_scope,
                                     const std::string& operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      ProtobufManager::DescriptorPoolInfo pool_info,
      proto_mgr_->GetDescriptorPool(descriptor_pool_id),
      _.AttachExtendedStatus(CreateExtendedStatus(
          15014, absl::StrFormat("Could not retrieve descriptor pool to "
                                 "compute assignment (internal pool id: %d)",
                                 descriptor_pool_id.value()))));

  absl::StatusOr<intrinsic_proto::executive::BehaviorCall*> bc_proto =
      proto_mgr_->GetMutableProtoAs<intrinsic_proto::executive::BehaviorCall>(
          proto_id);
  if (bc_proto.ok()) {
    absl::Status assign_status =
        AssignProto(**bc_proto, pool_info, parameter_message_name,
                    blackboard_scope, operation_name);
    if (!assign_status.ok() && !GetExtendedStatus(assign_status).has_value()) {
      // Assignment failed without ExtendedStatus. Shouldn't happen, but if it
      // does provide a generic ExtendedStatus.
      return CreateStatus(
          15900,
          absl::StrFormat("Failed to assign proto with error: %s",
                          assign_status.message()),
          absl::StatusCode::kUnknown);
    }
    return assign_status;
  }

  absl::StatusOr<intrinsic_proto::executive::AnyWithAssignments*> awa_proto =
      proto_mgr_
          ->GetMutableProtoAs<intrinsic_proto::executive::AnyWithAssignments>(
              proto_id);
  if (awa_proto.ok()) {
    absl::Status assign_status =
        AssignProto(**awa_proto, pool_info, parameter_message_name,
                    blackboard_scope, operation_name);
    if (!assign_status.ok() && !GetExtendedStatus(assign_status).has_value()) {
      return CreateStatus(
          15900,
          absl::StrFormat("Failed to assign proto with error: %s",
                          assign_status.message()),
          absl::StatusCode::kUnknown);
    }
    return assign_status;
  }

  INTR_ASSIGN_OR_RETURN(
      std::string type_name, proto_mgr_->GetMessageTypeName(proto_id),
      _.AttachExtendedStatus(CreateExtendedStatus(
          15952,
          absl::StrFormat(
              "The proto to assign could not be retrieved (internal id: %d)",
              proto_id.value()))));
  return CreateStatus(
      15952,
      absl::StrFormat("Protos of type '%s' cannot be assigned", type_name),
      absl::StatusCode::kInternal,
      {.debug_message = absl::StrFormat("internal id: %d", proto_id.value())});
}

absl::Status CelManager::AssignProto(
    intrinsic_proto::executive::BehaviorCall& proto,
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    const std::string& parameter_message_name,
    const std::string& blackboard_scope, const std::string& operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (proto.assignments().empty()) {
    return absl::OkStatus();
  }
  if (!proto.has_parameters()) {
    return CreateStatus(
        15950, "The 'parameters' field for the BehaviorCall proto is empty",
        absl::StatusCode::kInvalidArgument);
  }

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<google::protobuf::Message> casted,
      proto_mgr_->CastFromAnyWithPool(proto.parameters(), pool_info,
                                      parameter_message_name));

  for (const intrinsic_proto::executive::BehaviorCall::ParameterAssignment&
           assignment : proto.assignments()) {
    proto_mgr_->WarnIfFieldValueNotInSameOneofOption(
        casted.get(), assignment.parameter_path());

    // Type is irrelevant for the expression itself. The assignment in
    // SetFieldFromProto handles type mismatches.
    absl::StatusOr<std::unique_ptr<google::protobuf::Message>> proto_to_assign =
        EvaluateToProtoWithType(assignment.cel_expression(),
                                /*expected_message_type=*/"", pool_info,
                                blackboard_scope, operation_name);
    INTR_RETURN_IF_ERROR(CheckErrorAndAppendUserMessage(
        proto_to_assign.status(),
        absl::StrFormat(" (while assigning to path '%s')",
                        assignment.parameter_path())));

    INTR_RETURN_IF_ERROR(proto_mgr_->SetFieldFromProto(
        casted.get(), assignment.parameter_path(), pool_info,
        proto_to_assign->get(), assignment.cel_expression()));
  }

  if (!proto.mutable_parameters()->PackFrom(*casted)) {
    return CreateStatus(
        15951,
        absl::StrFormat(
            "The 'parameters' field of type '%s' could not be packed",
            proto.parameters().GetDescriptor()->full_name()),
        absl::StatusCode::kUnknown);
  }
  return absl::OkStatus();
}

absl::Status CelManager::AssignProto(
    intrinsic_proto::executive::AnyWithAssignments& proto,
    const ProtobufManager::DescriptorPoolInfo& pool_info,
    const std::string& parameter_message_name,
    const std::string& blackboard_scope, const std::string& operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex()) {
  if (proto.assign().empty()) {
    return absl::OkStatus();
  }
  if (!proto.has_proto()) {
    return CreateStatus(
        15950, "The 'proto' field for the AnyWithAssignments proto is empty",
        absl::StatusCode::kInvalidArgument);
  }

  INTR_ASSIGN_OR_RETURN(std::unique_ptr<google::protobuf::Message> casted,
                        proto_mgr_->CastFromAnyWithPool(
                            proto.proto(), pool_info, parameter_message_name));

  for (const intrinsic_proto::executive::AnyWithAssignments::Assignment&
           assignment : proto.assign()) {
    proto_mgr_->WarnIfFieldValueNotInSameOneofOption(casted.get(),
                                                     assignment.path());

    // Type is irrelevant for the expression itself. The assignment in
    // SetFieldFromProto handles type mismatches.
    absl::StatusOr<std::unique_ptr<google::protobuf::Message>> proto_to_assign =
        EvaluateToProtoWithType(assignment.cel_expression(),
                                /*expected_message_type=*/"", pool_info,
                                blackboard_scope, operation_name);
    INTR_RETURN_IF_ERROR(CheckErrorAndAppendUserMessage(
        proto_to_assign.status(),
        absl::StrFormat(" (while assigning to path '%s')", assignment.path())));

    INTR_RETURN_IF_ERROR(proto_mgr_->SetFieldFromProto(
        casted.get(), assignment.path(), pool_info, proto_to_assign->get(),
        assignment.cel_expression()));
  }

  if (!proto.mutable_proto()->PackFrom(*casted)) {
    return CreateStatus(
        15951,
        absl::StrFormat("The 'proto' field of type '%s' could not be packed",
                        proto.proto().GetDescriptor()->full_name()),
        absl::StatusCode::kUnknown);
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::executive::clips
