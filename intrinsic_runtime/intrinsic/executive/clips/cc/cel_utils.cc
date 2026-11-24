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

#include "intrinsic/executive/clips/cc/cel_utils.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "cel/expr/syntax.pb.h"
#include "common/operators.h"
#include "eval/public/builtin_func_registrar.h"
#include "eval/public/cel_expr_builder_factory.h"
#include "eval/public/cel_expression.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/descriptor_database.h"
#include "internal/strings.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::executive::clips {
namespace {

using ::google::api::expr::common::CelOperator;
using ::google::api::expr::common::IsOperatorLowerPrecedence;
using ::google::api::expr::common::IsOperatorSamePrecedence;
using ::google::api::expr::common::LookupBinaryOperator;

constexpr absl::string_view kParenLeft = "(";
constexpr absl::string_view kParenRight = ")";
constexpr absl::string_view kBracketLeft = "[";
constexpr absl::string_view kBracketRight = "]";
constexpr absl::string_view kDot = ".";
constexpr absl::string_view kCallSep = ", ";
constexpr absl::string_view kQuestionMarkSep = " ? ";
constexpr absl::string_view kColonSep = " : ";

void RecursivelyAddEnumDescriptors(
    const google::protobuf::Descriptor* descriptor,
    google::api::expr::runtime::CelExpressionBuilder& expr_builder,
    absl::flat_hash_set<std::string>& already_known_types) {
  if (already_known_types.contains(descriptor->full_name())) return;
  already_known_types.emplace(descriptor->full_name());

  for (int i = 0; i < descriptor->enum_type_count(); ++i) {
    expr_builder.GetTypeRegistry()->Register(descriptor->enum_type(i));
  }
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const google::protobuf::FieldDescriptor* field_desc = descriptor->field(i);
    if (field_desc->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
      expr_builder.GetTypeRegistry()->Register(field_desc->enum_type());
    } else if (field_desc->type() ==
               google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
      RecursivelyAddEnumDescriptors(field_desc->message_type(), expr_builder,
                                    already_known_types);
    }
  }
  for (int i = 0; i < descriptor->nested_type_count(); ++i) {
    RecursivelyAddEnumDescriptors(descriptor->nested_type(i), expr_builder,
                                  already_known_types);
  }
}

}  // namespace

absl::StatusOr<
    std::unique_ptr<google::api::expr::runtime::CelExpressionBuilder>>
CreateCelExpressionBuilder(
    const ProtobufManager::DescriptorPoolInfo& pool_info) {
  std::unique_ptr<google::api::expr::runtime::CelExpressionBuilder>
      expr_builder = google::api::expr::runtime::CreateCelExpressionBuilder(
          pool_info.descriptor_pool, pool_info.message_factory);
  if (!expr_builder) {
    return absl::FailedPreconditionError("Failed to create CEL Builder");
  }
  INTR_RETURN_IF_ERROR(google::api::expr::runtime::RegisterBuiltinFunctions(
      expr_builder->GetRegistry()));

  if (pool_info.descriptor_db != nullptr) {
    std::vector<std::string> proto_files;
    if (pool_info.descriptor_db->FindAllFileNames(&proto_files)) {
      absl::flat_hash_set<std::string> imported_types;
      for (const std::string& proto_file : proto_files) {
        const google::protobuf::FileDescriptor* file_descriptor =
            pool_info.descriptor_pool->FindFileByName(proto_file);
        if (file_descriptor == nullptr) continue;
        for (int i = 0; i < file_descriptor->message_type_count(); ++i) {
          const google::protobuf::Descriptor* descriptor =
              file_descriptor->message_type(i);
          if (descriptor == nullptr) continue;
          RecursivelyAddEnumDescriptors(descriptor, *expr_builder,
                                        imported_types);
        }
      }
    }
  }

  return std::move(expr_builder);
}

namespace {

bool IsBinaryOperator(const cel::expr::Expr& expr) {
  return LookupBinaryOperator(expr.call_expr().function()).has_value();
}

bool IsTernaryOperator(const cel::expr::Expr& expr) {
  return IsOperatorSamePrecedence(CelOperator::CONDITIONAL, expr);
}

bool IsCallWithMoreThanOneArg(const cel::expr::Expr& expr) {
  return expr.has_call_expr() && expr.call_expr().args_size() > 1;
}

// Returns true if the given expression is a call with a binary or ternary
// operator and thus needs to be surrounded by parens in many contexts. This
// includes, e.g., "_+_" and "_?_:_" but excludes "_[_]".
bool SurroundWithParens(const cel::expr::Expr& expr) {
  return IsCallWithMoreThanOneArg(expr) &&
         (IsBinaryOperator(expr) || IsTernaryOperator(expr));
}

// Internal helper class for UnparseCelExpression() below which prevents us,
// e.g., from having to pass around a ref to the output string.
class CelUnparser {
 public:
  static absl::StatusOr<std::string> Unparse(const cel::expr::Expr& expr) {
    CelUnparser unparser;
    INTR_RETURN_IF_ERROR(unparser.UnparseExpr(expr));
    return std::move(unparser.output_);
  }

 private:
  CelUnparser() = default;

  absl::Status UnparseConst(const cel::expr::Constant& constant);
  absl::Status UnparseIdent(const cel::expr::Expr::Ident& ident);
  absl::Status UnparseSelect(const cel::expr::Expr::Select& select);

  absl::StatusOr<bool> TryUnparseUnaryOperatorCall(
      const cel::expr::Expr::Call& call);
  absl::StatusOr<bool> TryUnparseBinaryOperatorCall(
      const cel::expr::Expr::Call& call);
  absl::StatusOr<bool> TryUnparseIndexCall(const cel::expr::Expr::Call& call);
  absl::StatusOr<bool> TryUnparseOptIndexCall(
      const cel::expr::Expr::Call& call);
  absl::StatusOr<bool> TryUnparseOptSelectCall(
      const cel::expr::Expr::Call& call);
  absl::StatusOr<bool> TryUnparseConditionalCall(
      const cel::expr::Expr::Call& call);
  absl::Status UnparseCall(const cel::expr::Expr::Call& call);

  absl::Status UnparseExpr(const cel::expr::Expr& expr,
                           bool surround_with_parens = false);

  std::string output_;
};

absl::Status CelUnparser::UnparseConst(const cel::expr::Constant& constant) {
  switch (constant.constant_kind_case()) {
    case cel::expr::Constant::kNullValue:
      absl::StrAppend(&output_, "null");
      break;
    case cel::expr::Constant::kBoolValue:
      absl::StrAppend(&output_, constant.bool_value() ? "true" : "false");
      break;
    case cel::expr::Constant::kInt64Value:
      absl::StrAppend(&output_, constant.int64_value());
      break;
    case cel::expr::Constant::kUint64Value:
      absl::StrAppend(&output_, constant.uint64_value(), "u");
      break;
    case cel::expr::Constant::kDoubleValue:
      absl::StrAppend(&output_, constant.double_value());
      break;
    case cel::expr::Constant::kStringValue: {
      absl::StrAppend(&output_, cel::internal::FormatDoubleQuotedStringLiteral(
                                    constant.string_value()));
      break;
    }
    case cel::expr::Constant::kBytesValue: {
      absl::StrAppend(&output_, cel::internal::FormatDoubleQuotedBytesLiteral(
                                    constant.bytes_value()));
      break;
    }
    default:
      return absl::InvalidArgumentError(absl::StrCat(
          "Unsupported Constant kind: ", constant.constant_kind_case()));
  }
  return absl::OkStatus();
}

absl::Status CelUnparser::UnparseIdent(const cel::expr::Expr::Ident& ident) {
  absl::StrAppend(&output_, ident.name());
  return absl::OkStatus();
}

absl::Status CelUnparser::UnparseSelect(const cel::expr::Expr::Select& select) {
  if (select.test_only()) {
    absl::StrAppend(&output_, CelOperator::HAS, kParenLeft);
  }
  INTR_RETURN_IF_ERROR(
      UnparseExpr(select.operand(), SurroundWithParens(select.operand())));
  absl::StrAppend(&output_, kDot, select.field());
  if (select.test_only()) {
    absl::StrAppend(&output_, kParenRight);
  }
  return absl::OkStatus();
}

absl::StatusOr<bool> CelUnparser::TryUnparseUnaryOperatorCall(
    const cel::expr::Expr::Call& call) {
  std::optional<std::string> op =
      google::api::expr::common::LookupUnaryOperator(call.function());
  if (!op.has_value()) {
    return false;  // Not a unary operator, Call expression was not handled
  }

  if (call.args_size() != 1) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Call with unary operator and !=1 args: ", call.ShortDebugString()));
  }

  absl::StrAppend(&output_, *op);
  // Add parens conservatively. This adds parens to any operator expression
  // with >1 arg (e.g., "-(a+b)") but also to regular call expressions with >1
  // arg (e.g. "-(f(a,b))").
  bool surround_with_parens = IsCallWithMoreThanOneArg(call.args(0));
  INTR_RETURN_IF_ERROR(UnparseExpr(call.args(0), surround_with_parens));

  return true;  // Call expression was handled
}

absl::StatusOr<bool> CelUnparser::TryUnparseBinaryOperatorCall(
    const cel::expr::Expr::Call& call) {
  std::optional<std::string> op = LookupBinaryOperator(call.function());
  if (!op.has_value()) {
    return false;  // Not a binary operator, Call expression was not handled
  }

  if (call.args_size() != 2) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Call with binary operator and !=2 args: ", call.ShortDebugString()));
  }
  const cel::expr::Expr& lhs = call.args(0);
  const cel::expr::Expr& rhs = call.args(1);
  const std::string& function = call.function();

  bool surround_lhs_with_parens = IsOperatorLowerPrecedence(function, lhs);
  bool surround_rhs_with_parens = IsOperatorLowerPrecedence(function, rhs) ||
                                  IsOperatorSamePrecedence(function, rhs);

  INTR_RETURN_IF_ERROR(UnparseExpr(lhs, surround_lhs_with_parens));
  absl::StrAppend(&output_, " ", *op, " ");
  INTR_RETURN_IF_ERROR(UnparseExpr(rhs, surround_rhs_with_parens));

  return true;  // Call expression was handled
}

absl::StatusOr<bool> CelUnparser::TryUnparseIndexCall(
    const cel::expr::Expr::Call& call) {
  if (call.function() != CelOperator::INDEX) {
    // Not an index operator, Call expression was not handled
    return false;
  }

  if (call.args_size() != 2) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Call with index operator and !=2 args: ", call.ShortDebugString()));
  }

  INTR_RETURN_IF_ERROR(
      UnparseExpr(call.args(0), SurroundWithParens(call.args(0))));
  absl::StrAppend(&output_, kBracketLeft);
  INTR_RETURN_IF_ERROR(UnparseExpr(call.args(1)));
  absl::StrAppend(&output_, kBracketRight);

  return true;
}

absl::StatusOr<bool> CelUnparser::TryUnparseOptIndexCall(
    const cel::expr::Expr::Call& call) {
  if (call.function() != CelOperator::OPT_INDEX) {
    // Not an opt index operator, Call expression was not handled
    return false;
  }

  return absl::InvalidArgumentError("Opt index operator not supported");
}

absl::StatusOr<bool> CelUnparser::TryUnparseOptSelectCall(
    const cel::expr::Expr::Call& call) {
  if (call.function() != CelOperator::OPT_SELECT) {
    // Not an opt select operator, Call expression was not handled
    return false;
  }

  return absl::InvalidArgumentError("Opt select operator not supported");
}

absl::StatusOr<bool> CelUnparser::TryUnparseConditionalCall(
    const cel::expr::Expr::Call& call) {
  if (call.function() != CelOperator::CONDITIONAL) {
    // Not a conditional operator, Call expression was not handled
    return false;
  }

  if (call.args_size() != 3) {
    return absl::InvalidArgumentError(
        absl::StrCat("Call with conditional operator and !=3 args: ",
                     call.ShortDebugString()));
  }

  // Add parens conservatively. This adds parens to any operator expression
  // with >1 arg (e.g., "(a+b)") but also to regular call expressions with >1
  // arg (e.g. "(f(a,b))").
  bool surround_with_parens =
      IsOperatorSamePrecedence(CelOperator::CONDITIONAL, call.args(0)) ||
      IsCallWithMoreThanOneArg(call.args(0));
  INTR_RETURN_IF_ERROR(UnparseExpr(call.args(0), surround_with_parens));

  absl::StrAppend(&output_, kQuestionMarkSep);

  surround_with_parens =
      IsOperatorSamePrecedence(CelOperator::CONDITIONAL, call.args(1)) ||
      IsCallWithMoreThanOneArg(call.args(1));
  INTR_RETURN_IF_ERROR(UnparseExpr(call.args(1), surround_with_parens));

  absl::StrAppend(&output_, kColonSep);

  surround_with_parens =
      IsOperatorSamePrecedence(CelOperator::CONDITIONAL, call.args(2)) ||
      IsCallWithMoreThanOneArg(call.args(2));
  INTR_RETURN_IF_ERROR(UnparseExpr(call.args(2), surround_with_parens));

  return true;  // Call expression was handled
}

absl::Status CelUnparser::UnparseCall(const cel::expr::Expr::Call& call) {
  // Special handling for operators. E.g., if call.function() == "_+_", print
  // "a + b" and not "_+_(a, b)".
  for (auto try_unparse_call : {
           &CelUnparser::TryUnparseUnaryOperatorCall,
           &CelUnparser::TryUnparseBinaryOperatorCall,
           &CelUnparser::TryUnparseIndexCall,
           &CelUnparser::TryUnparseOptIndexCall,
           &CelUnparser::TryUnparseOptSelectCall,
           &CelUnparser::TryUnparseConditionalCall,
       }) {
    INTR_ASSIGN_OR_RETURN(bool handled, (this->*try_unparse_call)(call));
    if (handled) {
      return absl::OkStatus();
    }
  }

  // Default case: print "target.function(arg0, arg1, ...)".
  if (call.has_target()) {
    INTR_RETURN_IF_ERROR(
        UnparseExpr(call.target(), SurroundWithParens(call.target())));
    absl::StrAppend(&output_, kDot);
  }
  absl::StrAppend(&output_, call.function(), kParenLeft);
  for (int i = 0; i < call.args_size(); i++) {
    if (i > 0) {
      absl::StrAppend(&output_, kCallSep);
    }
    INTR_RETURN_IF_ERROR(UnparseExpr(call.args(i)));
  }
  absl::StrAppend(&output_, kParenRight);
  return absl::OkStatus();
}

absl::Status CelUnparser::UnparseExpr(const cel::expr::Expr& expr,
                                      bool surround_with_parens) {
  if (surround_with_parens) {
    absl::StrAppend(&output_, kParenLeft);
  }

  std::string unsupported_kind;
  switch (expr.expr_kind_case()) {
    case cel::expr::Expr::kConstExpr:
      INTR_RETURN_IF_ERROR(UnparseConst(expr.const_expr()));
      break;
    case cel::expr::Expr::kIdentExpr:
      INTR_RETURN_IF_ERROR(UnparseIdent(expr.ident_expr()));
      break;
    case cel::expr::Expr::kSelectExpr:
      INTR_RETURN_IF_ERROR(UnparseSelect(expr.select_expr()));
      break;
    case cel::expr::Expr::kCallExpr:
      INTR_RETURN_IF_ERROR(UnparseCall(expr.call_expr()));
      break;
    case cel::expr::Expr::kListExpr:
      unsupported_kind = "CreateList";
      break;
    case cel::expr::Expr::kStructExpr:
      unsupported_kind = "CreateStruct";
      break;
    case cel::expr::Expr::kComprehensionExpr:
      unsupported_kind = "Comprehension";
      break;
    default:
      unsupported_kind = expr.expr_kind_case();
      break;
  }

  if (!unsupported_kind.empty()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Unsupported kind of CEL expression: ", unsupported_kind));
  }

  if (surround_with_parens) {
    absl::StrAppend(&output_, kParenRight);
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::string> UnparseCelExpression(const cel::expr::Expr& expr) {
  return CelUnparser::Unparse(expr);
}

}  // namespace intrinsic::executive::clips
