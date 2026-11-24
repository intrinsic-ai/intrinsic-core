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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CC_CEL_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CC_CEL_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "cel/expr/syntax.pb.h"
#include "eval/public/activation.h"
#include "eval/public/cel_expression.h"
#include "eval/public/cel_value.h"
#include "google/protobuf/arena.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/any_with_assignments.pb.h"
#include "intrinsic/executive/proto/behavior_call.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/strong_int.h"

namespace intrinsic::executive::clips {

DEFINE_STRONG_INT_TYPE(CelExpressionId, int64_t);

// Recursively looks into the given Expr proto and finds all identifiers (Ident
// expressions) used within the given expression.
absl::flat_hash_set<std::string> FindIdentifiersInCelExpression(
    const cel::expr::Expr& expr);

class CelManager {
 public:
  // ID representing a non-existing expression. Particularly relevant on the
  // CLIPS side where we cannot deal with StatusOr.
  static constexpr CelExpressionId kInvalidCelExpressionId = CelExpressionId(0);

  static absl::StatusOr<std::unique_ptr<CelManager>> Create(
      Environment* environment, ProtobufManager* protobuf_manager)
      ABSL_LOCKS_EXCLUDED(environment->mutex());
  ~CelManager() ABSL_LOCKS_EXCLUDED(env_->mutex());

  absl::Mutex* GetClipsMutex() const ABSL_LOCK_RETURNED(env_->mutex()) {
    return env_->mutex();
  }

  absl::StatusOr<CelExpressionId> CreateExpressionFromProto(
      const cel::expr::Expr& expression_proto,
      DescriptorPoolId descriptor_pool_id);
  absl::StatusOr<CelExpressionId> CreateExpressionFromString(
      std::string_view expression, DescriptorPoolId descriptor_pool_id);

  void RemoveExpression(CelExpressionId id);

  absl::StatusOr<std::string> GetExpressionAsString(CelExpressionId id) const;

  // Evaluate the given condition expression on the blackboard given by its
  // scope and operation name. The expression must result in a value of type
  // bool.
  absl::StatusOr<bool> EvaluateCondition(CelExpressionId id,
                                         std::string_view blackboard_scope,
                                         std::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsMutex());

  // Evaluate the given expression to a proto of the expected_message_type. The
  // proto will be generated from descriptor_pool_id.
  absl::StatusOr<clips::ProtoMessageId> EvaluateToProtoWithType(
      const std::string& expression, const std::string& expected_message_type,
      clips::DescriptorPoolId descriptor_pool_id,
      const std::string& blackboard_scope, const std::string& operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsMutex());
  absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
  EvaluateToProtoWithType(const std::string& expression,
                          absl::string_view expected_message_type,
                          const ProtobufManager::DescriptorPoolInfo& pool_info,
                          absl::string_view blackboard_scope,
                          absl::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsMutex());

  // Evaluate the given expression in the given scope. The expression must
  // result either in a list of protos or in an AnyList proto that is then
  // converted to a list of protos. In both cases all protos must be available
  // from the given descriptor pool.
  absl::StatusOr<std::vector<clips::ProtoMessageId>> EvaluateToProtoList(
      CelExpressionId expression_id, clips::DescriptorPoolId descriptor_pool_id,
      const std::string& blackboard_scope, const std::string& operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsMutex());

  absl::Status AssignProto(clips::ProtoMessageId proto_id,
                           clips::DescriptorPoolId descriptor_pool_id,
                           const std::string& parameter_message_name,
                           const std::string& blackboard_scope,
                           const std::string& operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsMutex());
  absl::Status AssignProto(intrinsic_proto::executive::BehaviorCall& proto,
                           const ProtobufManager::DescriptorPoolInfo& pool_info,
                           const std::string& parameter_message_name,
                           const std::string& blackboard_scope,
                           const std::string& operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsMutex());
  absl::Status AssignProto(
      intrinsic_proto::executive::AnyWithAssignments& proto,
      const ProtobufManager::DescriptorPoolInfo& pool_info,
      const std::string& parameter_message_name,
      const std::string& blackboard_scope, const std::string& operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(GetClipsMutex());

  size_t GetNumExpressions() const { return expressions_.size(); }

 private:
  explicit CelManager(Environment* environment,
                      ProtobufManager* protobuf_manager);

  struct ExpressionData {
    std::string expression;
    cel::expr::ParsedExpr parsed_expr;
    std::unique_ptr<google::api::expr::runtime::CelExpressionBuilder>
        cel_expr_builder;
    std::unique_ptr<google::api::expr::runtime::CelExpression> cel_expression;
    absl::flat_hash_set<std::string> identifiers_set;
    absl::flat_hash_set<std::string> struct_messages_set;
  };

  absl::StatusOr<CelExpressionId> CreateExpression(
      std::string_view expression, cel::expr::ParsedExpr&& parsed_expr,
      DescriptorPoolId descriptor_pool_id);
  absl::StatusOr<ExpressionData> CreateExpression(
      std::string_view expression, cel::expr::ParsedExpr&& parsed_expr,
      const ProtobufManager::DescriptorPoolInfo& pool_info);

  // Calls the CEL parser and wraps failures in ExtendedStatus.
  absl::StatusOr<cel::expr::ParsedExpr> ParseExpression(
      std::string_view expression);
  absl::StatusOr<CelManager::ExpressionData> CreateExpressionFromString(
      std::string_view expression,
      const ProtobufManager::DescriptorPoolInfo& pool_info);

  // Creates a map identifier -> proto id for the passed in identifiers by
  // looking for blackboard items in the given scope + operation.
  absl::StatusOr<absl::flat_hash_map<std::string, ProtoMessageId>>
  CreateProtoMapFromBlackboard(
      const absl::flat_hash_set<std::string>& identifiers,
      std::string_view blackboard_scope, std::string_view operation_name,
      std::string_view expression) ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());

  // Creates a CEL Activation representing the given identifiers stored in the
  // passed in arena.
  absl::StatusOr<google::api::expr::runtime::Activation>
  CreateActivationFromBlackboard(
      const absl::flat_hash_set<std::string>& identifiers,
      std::string_view blackboard_scope, std::string_view operation_name,
      std::string_view expression, google::protobuf::Arena* arena)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());

  // Will report an OK Status if the passed in cel_value is OK and the
  // CelValue does not have internal errors. Thus when this function passes
  // one can access *cel_value and assume that it is not of type error or
  // unknown set, i.e., it contains an actual value (e.g., bool, int, proto
  // message). Otherwise an ExtendedStatus error is issued.
  absl::Status EnsureErrorFreeCelValue(
      const absl::StatusOr<::google::api::expr::runtime::CelValue>& cel_value,
      std::string_view expression);

  // Evaluates the given expression on the blackboard identified by its scope
  // and operation. If successful the CelValue contains a valid type, i.e., it
  // is not of type error or unknown and can be used directly.
  // Detailed errors are reported via ExtendedStatus.
  absl::StatusOr<::google::api::expr::runtime::CelValue>
  EvaluateExpressionOnBlackboard(CelExpressionId id,
                                 std::string_view blackboard_scope,
                                 std::string_view operation_name,
                                 google::protobuf::Arena* arena)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());
  absl::StatusOr<::google::api::expr::runtime::CelValue>
  EvaluateExpressionOnBlackboard(const ExpressionData& expr_data,
                                 std::string_view blackboard_scope,
                                 std::string_view operation_name,
                                 google::protobuf::Arena* arena)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());

  absl::StatusOr<std::vector<std::unique_ptr<google::protobuf::Message>>>
  EvaluateToProtoList(const ExpressionData& expression_data,
                      const ProtobufManager::DescriptorPoolInfo& pool_info,
                      absl::string_view blackboard_scope,
                      absl::string_view operation_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(env_->mutex());

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

  CelExpressionId GenerateNextCelExpressionId();

  Environment* env_;            // owned externally
  ProtobufManager* proto_mgr_;  // owned externally
  absl::BitGen id_random_generator_;

  absl::StatusOr<std::reference_wrapper<const ExpressionData>>
  GetExpressionData(CelExpressionId id) const;

  absl::flat_hash_map<CelExpressionId, ExpressionData> expressions_;
  std::vector<std::string> functions_;
};

}  // namespace intrinsic::executive::clips

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CC_CEL_H_
