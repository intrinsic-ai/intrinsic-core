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

#include "intrinsic/icon/cc_client/condition.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "absl/hash/hash_testing.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/text_format.h"
#include "internal/testing.h"
#include "intrinsic/icon/proto/v1/condition_types.pb.h"
#include "intrinsic/util/full_precision.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::protobuf_matchers::EquivToProto;
using ::testing::Property;
using ::testing::UnorderedElementsAre;
using ::testing::VariantWith;

TEST(Comparison, ConstructBoolCondition) {
  constexpr char kStateVariableName[] = "var";
  constexpr Comparison::BooleanOperator kOperator =
      Comparison::BooleanOperator::kEqual;
  constexpr bool kValue = true;

  Comparison cond(kStateVariableName, kOperator, kValue);
  EXPECT_EQ(cond.state_variable_name(), kStateVariableName);
  EXPECT_EQ(
      cond.operation(),
      static_cast<intrinsic_proto::icon::v1::Comparison::OpEnum>(kOperator));
  EXPECT_EQ(std::get<bool>(cond.value()), kValue);
}

TEST(Comparison, ConstructFloatCondition) {
  constexpr char kStateVariableName[] = "var";
  constexpr Comparison::FloatOperator kOperator =
      Comparison::FloatOperator::kApproxEqual;
  constexpr double kValue = 1.0;
  constexpr double kMaxAbsError = 10.0;

  Comparison cond(kStateVariableName, kOperator, kValue, kMaxAbsError);
  EXPECT_EQ(cond.state_variable_name(), kStateVariableName);
  EXPECT_EQ(
      cond.operation(),
      static_cast<intrinsic_proto::icon::v1::Comparison::OpEnum>(kOperator));
  EXPECT_EQ(std::get<double>(cond.value()), kValue);
  EXPECT_EQ(cond.max_abs_error(), kMaxAbsError);
}

TEST(Comparison, ConstructFloatConditionDefaultMaxAbsError) {
  constexpr char kStateVariableName[] = "var";
  constexpr Comparison::FloatOperator kOperator =
      Comparison::FloatOperator::kApproxEqual;
  constexpr double kValue = 1.0;

  Comparison cond(kStateVariableName, kOperator, kValue);
  EXPECT_EQ(cond.state_variable_name(), kStateVariableName);
  EXPECT_EQ(
      cond.operation(),
      static_cast<intrinsic_proto::icon::v1::Comparison::OpEnum>(kOperator));
  EXPECT_EQ(std::get<double>(cond.value()), kValue);
  EXPECT_EQ(cond.max_abs_error(), kDefaultMaxAbsError);
}

TEST(Comparison, CreateSucceeds) {
  ASSERT_OK_AND_ASSIGN(
      Comparison cond,
      Comparison::Create(
          "var", intrinsic_proto::icon::v1::Comparison::GREATER_THAN, 2.0));
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN);
  ASSERT_TRUE(std::holds_alternative<double>(cond.value()));
  EXPECT_EQ(std::get<double>(cond.value()), 2.0);
  EXPECT_EQ(cond.max_abs_error(), kDefaultMaxAbsError);
}

TEST(Comparison, CreateRejectsWrongCombination) {
  EXPECT_THAT(Comparison::Create(
                  "var", intrinsic_proto::icon::v1::Comparison::APPROX_EQUAL,
                  int64_t{0}),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      Comparison::Create(
          "var", intrinsic_proto::icon::v1::Comparison::APPROX_NOT_EQUAL,
          int64_t{0}),
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Comparison::Create(
                  "var", intrinsic_proto::icon::v1::Comparison::EQUAL, 0.0),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(Comparison::Create(
                  "var", intrinsic_proto::icon::v1::Comparison::NOT_EQUAL, 0.0),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(Comparison, CreateFailsBadBoolOperation) {
  EXPECT_THAT(
      Comparison::Create(
          "var", intrinsic_proto::icon::v1::Comparison::LESS_THAN_OR_EQUAL,
          false),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(Comparison, IsTrue) {
  Comparison cond = IsTrue("var");
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(), intrinsic_proto::icon::v1::Comparison::EQUAL);
  ASSERT_TRUE(std::holds_alternative<bool>(cond.value()));
  EXPECT_EQ(std::get<bool>(cond.value()), true);
}

TEST(Comparison, IsFalse) {
  Comparison cond = IsFalse("var");
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(), intrinsic_proto::icon::v1::Comparison::EQUAL);
  ASSERT_TRUE(std::holds_alternative<bool>(cond.value()));
  EXPECT_EQ(std::get<bool>(cond.value()), false);
}

TEST(Comparison, IsApprox) {
  Comparison cond = IsApprox("var", 1.0, 0.5);
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::APPROX_EQUAL);
  ASSERT_TRUE(std::holds_alternative<double>(cond.value()));
  EXPECT_EQ(std::get<double>(cond.value()), 1.0);
  EXPECT_EQ(cond.max_abs_error(), 0.5);
}

TEST(Comparison, IsNotApprox) {
  Comparison cond = IsNotApprox("var", 1.0, 0.5);
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::APPROX_NOT_EQUAL);
  ASSERT_TRUE(std::holds_alternative<double>(cond.value()));
  EXPECT_EQ(std::get<double>(cond.value()), 1.0);
  EXPECT_EQ(cond.max_abs_error(), 0.5);
}

TEST(Comparison, IsEqual) {
  Comparison cond = IsEqual("var", 1);
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(), intrinsic_proto::icon::v1::Comparison::EQUAL);
  EXPECT_THAT(cond.value(), ::testing::VariantWith<int64_t>(1));
}

TEST(Comparison, IsNotEqual) {
  Comparison cond = IsNotEqual("var", 1);
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(), intrinsic_proto::icon::v1::Comparison::NOT_EQUAL);
  EXPECT_THAT(cond.value(), ::testing::VariantWith<int64_t>(1));
}

TEST(Comparison, FloatIsLessThanOrEqual) {
  Comparison cond = IsLessThanOrEqual("var", 1.1);
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::LESS_THAN_OR_EQUAL);
  ASSERT_TRUE(std::holds_alternative<double>(cond.value()));
  EXPECT_EQ(std::get<double>(cond.value()), 1.1);
}

TEST(Comparison, IntIsLessThanOrEqual) {
  Comparison cond = IsLessThanOrEqual("var", int64_t{1});
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::LESS_THAN_OR_EQUAL);
  ASSERT_TRUE(std::holds_alternative<int64_t>(cond.value()));
  EXPECT_EQ(std::get<int64_t>(cond.value()), 1);
}

TEST(Comparison, FloatIsLessThan) {
  Comparison cond = IsLessThan("var", 1.1);
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(), intrinsic_proto::icon::v1::Comparison::LESS_THAN);
  ASSERT_TRUE(std::holds_alternative<double>(cond.value()));
  EXPECT_EQ(std::get<double>(cond.value()), 1.1);
}

TEST(Comparison, IntIsLessThan) {
  Comparison cond = IsLessThan("var", int64_t{1});
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(), intrinsic_proto::icon::v1::Comparison::LESS_THAN);
  ASSERT_TRUE(std::holds_alternative<int64_t>(cond.value()));
  EXPECT_EQ(std::get<int64_t>(cond.value()), 1);
}

TEST(Comparison, FloatIsGreaterThanOrEqual) {
  Comparison cond = IsGreaterThanOrEqual("var", 1.1);
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN_OR_EQUAL);
  ASSERT_TRUE(std::holds_alternative<double>(cond.value()));
  EXPECT_EQ(std::get<double>(cond.value()), 1.1);
}

TEST(Comparison, IntIsGreaterThanOrEqual) {
  Comparison cond = IsGreaterThanOrEqual("var", int64_t{1});
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN_OR_EQUAL);
  ASSERT_TRUE(std::holds_alternative<int64_t>(cond.value()));
  EXPECT_EQ(std::get<int64_t>(cond.value()), 1);
}

TEST(Comparison, FloatIsGreaterThan) {
  Comparison cond = IsGreaterThan("var", 1.1);
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN);
  ASSERT_TRUE(std::holds_alternative<double>(cond.value()));
  EXPECT_EQ(std::get<double>(cond.value()), 1.1);
}

TEST(Comparison, IntIsGreaterThan) {
  Comparison cond = IsGreaterThan("var", int64_t{1});
  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN);
  ASSERT_TRUE(std::holds_alternative<int64_t>(cond.value()));
  EXPECT_EQ(std::get<int64_t>(cond.value()), 1);
}

TEST(Comparison, FromProtoBoolValueSucceeds) {
  intrinsic_proto::icon::v1::Comparison proto;
  proto.set_state_variable_name("var");
  proto.set_operation(intrinsic_proto::icon::v1::Comparison::EQUAL);
  proto.set_bool_value(true);

  ASSERT_OK_AND_ASSIGN(Comparison cond, FromProto(proto));

  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(), intrinsic_proto::icon::v1::Comparison::EQUAL);
  ASSERT_TRUE(std::holds_alternative<bool>(cond.value()));
  EXPECT_EQ(std::get<bool>(cond.value()), true);
}

TEST(Comparison, FromProtoDoubleValueSucceeds) {
  intrinsic_proto::icon::v1::Comparison proto;
  proto.set_state_variable_name("var");
  proto.set_operation(intrinsic_proto::icon::v1::Comparison::APPROX_EQUAL);
  proto.set_double_value(10.1);
  proto.set_max_abs_error(1.0);

  ASSERT_OK_AND_ASSIGN(Comparison cond, FromProto(proto));

  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(),
            intrinsic_proto::icon::v1::Comparison::APPROX_EQUAL);
  ASSERT_TRUE(std::holds_alternative<double>(cond.value()));
  EXPECT_EQ(std::get<double>(cond.value()), 10.1);
  EXPECT_EQ(cond.max_abs_error(), 1.0);
}

TEST(Comparison, FromProtoIntValueSucceeds) {
  intrinsic_proto::icon::v1::Comparison proto;
  proto.set_state_variable_name("var");
  proto.set_operation(intrinsic_proto::icon::v1::Comparison::EQUAL);
  proto.set_int64_value(10);

  ASSERT_OK_AND_ASSIGN(Comparison cond, FromProto(proto));

  EXPECT_EQ(cond.state_variable_name(), "var");
  EXPECT_EQ(cond.operation(), intrinsic_proto::icon::v1::Comparison::EQUAL);
  ASSERT_TRUE(std::holds_alternative<int64_t>(cond.value()));
  EXPECT_EQ(std::get<int64_t>(cond.value()), 10);
}

TEST(Comparison, FromProtoDoubleFailsWhenValueNotSet) {
  intrinsic_proto::icon::v1::Comparison proto;
  proto.set_state_variable_name("var");
  proto.set_operation(intrinsic_proto::icon::v1::Comparison::APPROX_EQUAL);

  EXPECT_THAT(FromProto(proto), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(Comparison, ToProtoBoolValue) {
  intrinsic_proto::icon::v1::Comparison expected_proto;
  expected_proto.set_state_variable_name("var");
  expected_proto.set_operation(intrinsic_proto::icon::v1::Comparison::EQUAL);
  expected_proto.set_bool_value(true);
  expected_proto.set_max_abs_error(kDefaultMaxAbsError);

  ASSERT_OK_AND_ASSIGN(
      Comparison cond,
      Comparison::Create("var", intrinsic_proto::icon::v1::Comparison::EQUAL,
                         true));

  EXPECT_THAT(ToProto(cond), EqualsProto(expected_proto));
}

TEST(Comparison, ToProtoDoubleValue) {
  intrinsic_proto::icon::v1::Comparison expected_proto;
  expected_proto.set_state_variable_name("var");
  expected_proto.set_operation(
      intrinsic_proto::icon::v1::Comparison::APPROX_EQUAL);
  expected_proto.set_double_value(4.2);
  expected_proto.set_max_abs_error(kDefaultMaxAbsError);

  ASSERT_OK_AND_ASSIGN(
      Comparison cond,
      Comparison::Create(
          "var", intrinsic_proto::icon::v1::Comparison::APPROX_EQUAL, 4.2));

  EXPECT_THAT(ToProto(cond), EqualsProto(expected_proto));
}

TEST(Comparison, ToProtoIntValue) {
  intrinsic_proto::icon::v1::Comparison expected_proto;
  expected_proto.set_state_variable_name("var");
  expected_proto.set_operation(intrinsic_proto::icon::v1::Comparison::EQUAL);
  expected_proto.set_int64_value(4);
  expected_proto.set_max_abs_error(kDefaultMaxAbsError);

  ASSERT_OK_AND_ASSIGN(
      Comparison cond,
      Comparison::Create("var", intrinsic_proto::icon::v1::Comparison::EQUAL,
                         int64_t{4}));

  EXPECT_THAT(ToProto(cond), EqualsProto(expected_proto));
}

template <typename A, typename B>
constexpr bool CastsEqual(A a, B b) {
  return a == static_cast<A>(b);
}

TEST(Comparison, BooleanOperatorEnumCastableToProto) {
  using BooleanOp = Comparison::BooleanOperator;
  constexpr size_t kMaxValue =
      2;  // Max value in BooleanOperator by inspection.
  std::array<BooleanOp, kMaxValue> bool_op_values = {BooleanOp::kEqual,
                                                     BooleanOp::kNotEqual};

  // The switch ensures that all values defined in enum class BooleanOperator
  // are tested.
  for (auto value : bool_op_values) {
    switch (value) {
      case BooleanOp::kEqual:
        ASSERT_TRUE(
            CastsEqual(intrinsic_proto::icon::v1::Comparison::EQUAL, value));
        break;
      case BooleanOp::kNotEqual:
        ASSERT_TRUE(CastsEqual(intrinsic_proto::icon::v1::Comparison::NOT_EQUAL,
                               value));
        break;
    }
  }
}

TEST(Comparison, FloatOperatorEnumCastableToProto) {
  using FloatOp = Comparison::FloatOperator;

  std::vector<FloatOp> float_op_values = {
      // FloatOp::kEqual, FloatOp::kNotEqual, <-- not available for float
      FloatOp::kApproxEqual,        FloatOp::kApproxNotEqual,
      FloatOp::kLessThanOrEqual,    FloatOp::kLessThan,
      FloatOp::kGreaterThanOrEqual, FloatOp::kGreaterThan};

  // The switch ensures that all values defined in enum class FloatOperator
  // are tested.
  for (auto value : float_op_values) {
    switch (value) {
      // case FloatOp::kEqual:
      //   ASSERT_TRUE(CastsEqual(proto::Comparison::EQUAL, value));
      //   break;
      // case FloatOp::kNotEqual:
      //   ASSERT_TRUE(CastsEqual(proto::Comparison::NOT_EQUAL, value));
      //   break;
      case FloatOp::kApproxEqual:
        ASSERT_TRUE(CastsEqual(
            intrinsic_proto::icon::v1::Comparison::APPROX_EQUAL, value));
        break;
      case FloatOp::kApproxNotEqual:
        ASSERT_TRUE(CastsEqual(
            intrinsic_proto::icon::v1::Comparison::APPROX_NOT_EQUAL, value));
        break;
      case FloatOp::kLessThanOrEqual:
        ASSERT_TRUE(CastsEqual(
            intrinsic_proto::icon::v1::Comparison::LESS_THAN_OR_EQUAL, value));
        break;
      case FloatOp::kLessThan:
        ASSERT_TRUE(CastsEqual(intrinsic_proto::icon::v1::Comparison::LESS_THAN,
                               value));
        break;
      case FloatOp::kGreaterThanOrEqual:
        ASSERT_TRUE(CastsEqual(
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN_OR_EQUAL,
            value));
        break;
      case FloatOp::kGreaterThan:
        ASSERT_TRUE(CastsEqual(
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN, value));
        break;
    }
  }
}
TEST(Comparison, IntOperatorEnumCastableToProto) {
  using IntOp = Comparison::IntOperator;

  std::vector<IntOp> int_op_values = {
      IntOp::kEqual, IntOp::kNotEqual,
      // IntOp::kApproxEqual, IntOp::kApproxNotEqual, <-- not available for int
      IntOp::kLessThanOrEqual, IntOp::kLessThan, IntOp::kGreaterThanOrEqual,
      IntOp::kGreaterThan};

  // The switch ensures that all values defined in enum class IntOperator
  // are tested.
  for (auto value : int_op_values) {
    switch (value) {
      case IntOp::kEqual:
        ASSERT_TRUE(
            CastsEqual(intrinsic_proto::icon::v1::Comparison::EQUAL, value));
        break;
      case IntOp::kNotEqual:
        ASSERT_TRUE(CastsEqual(intrinsic_proto::icon::v1::Comparison::NOT_EQUAL,
                               value));
        break;
      case IntOp::kLessThanOrEqual:
        ASSERT_TRUE(CastsEqual(
            intrinsic_proto::icon::v1::Comparison::LESS_THAN_OR_EQUAL, value));
        break;
      case IntOp::kLessThan:
        ASSERT_TRUE(CastsEqual(intrinsic_proto::icon::v1::Comparison::LESS_THAN,
                               value));
        break;
      case IntOp::kGreaterThanOrEqual:
        ASSERT_TRUE(CastsEqual(
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN_OR_EQUAL,
            value));
        break;
      case IntOp::kGreaterThan:
        ASSERT_TRUE(CastsEqual(
            intrinsic_proto::icon::v1::Comparison::GREATER_THAN, value));
        break;
    }
  }
}

TEST(Comparison, Equals) {
  Comparison foo_equals_0("foo", Comparison::FloatOperator::kApproxEqual, 0.0);
  Comparison bar_equals_0("bar", Comparison::FloatOperator::kApproxEqual, 0.0);
  Comparison foo_not_equal_0("foo", Comparison::FloatOperator::kApproxNotEqual,
                             0.0);
  Comparison foo_equals_1("foo", Comparison::FloatOperator::kApproxEqual, 1.0);

  EXPECT_EQ(foo_equals_0, foo_equals_0);
  EXPECT_EQ(bar_equals_0, bar_equals_0);
  EXPECT_EQ(foo_not_equal_0, foo_not_equal_0);
  EXPECT_EQ(foo_equals_1, foo_equals_1);

  EXPECT_NE(foo_equals_0, bar_equals_0);
  EXPECT_NE(foo_equals_0, foo_not_equal_0);
  EXPECT_NE(foo_equals_0, foo_equals_1);
}

TEST(Comparison, IsHashable) {
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      Comparison("foo", Comparison::FloatOperator::kApproxEqual, 0.0),
      Comparison("bar", Comparison::FloatOperator::kApproxEqual, 0.0),
      Comparison("foo", Comparison::FloatOperator::kApproxNotEqual, 0.0),
      Comparison("foo", Comparison::FloatOperator::kApproxEqual, 1.0),
  }));
}

TEST(ConjunctionCondition, ToProto) {
  Comparison c0("variable1", Comparison::BooleanOperator::kEqual, false);
  Comparison c1("variable2", Comparison::BooleanOperator::kNotEqual, true);

  ConjunctionCondition condition(ConjunctionCondition::Operation::kAllOf,
                                 {c0, c1});

  EXPECT_THAT(ToProto(condition), EquivToProto(absl::Substitute(
                                      R"pb(
                                        operation: ALL_OF,
                                        conditions: {
                                          comparison: {
                                            state_variable_name: "variable1",
                                            operation: EQUAL,
                                            bool_value: false,
                                            max_abs_error: $0
                                          }
                                        }
                                        conditions: {
                                          comparison: {
                                            state_variable_name: "variable2",
                                            operation: NOT_EQUAL,
                                            bool_value: true,
                                            max_abs_error: $0
                                          }
                                        })pb",
                                      FullPrecision(kDefaultMaxAbsError))));
}

TEST(ConjunctionCondition, FromProto) {
  constexpr absl::string_view kProtoText = R"pb(
    operation: ALL_OF,
    conditions: {
      comparison: {
        state_variable_name: "variable1",
        operation: EQUAL,
        bool_value: false
      }
    }
    conditions: {
      comparison: {
        state_variable_name: "variable2",
        operation: NOT_EQUAL,
        bool_value: true
      }
    }
  )pb";

  intrinsic_proto::icon::v1::ConjunctionCondition proto;
  ASSERT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kProtoText, &proto));

  ASSERT_OK_AND_ASSIGN(ConjunctionCondition condition, FromProto(proto));
  EXPECT_EQ(condition.GetOperation(), ConjunctionCondition::Operation::kAllOf);

  EXPECT_THAT(
      condition.GetConditions(),
      UnorderedElementsAre(
          VariantWith<Comparison>(
              AllOf(Property(&Comparison::state_variable_name, "variable1"),
                    Property(&Comparison::operation,
                             intrinsic_proto::icon::v1::Comparison::EQUAL))),
          VariantWith<Comparison>(AllOf(
              Property(&Comparison::state_variable_name, "variable2"),
              Property(&Comparison::operation,
                       intrinsic_proto::icon::v1::Comparison::NOT_EQUAL)))));
}

TEST(ConjunctionCondition, FlatAllOfSuccess) {
  auto all_of = AllOf({IsFalse("false_variable"), IsTrue("true_variable")});
  EXPECT_EQ(all_of.GetOperation(), ConjunctionCondition::Operation::kAllOf);

  EXPECT_THAT(
      all_of.GetConditions(),
      UnorderedElementsAre(
          VariantWith<Comparison>(AllOf(
              Property(&Comparison::state_variable_name, "false_variable"),
              Property(&Comparison::operation,
                       intrinsic_proto::icon::v1::Comparison::EQUAL))),
          VariantWith<Comparison>(
              AllOf(Property(&Comparison::state_variable_name, "true_variable"),
                    Property(&Comparison::operation,
                             intrinsic_proto::icon::v1::Comparison::EQUAL)))));
}

TEST(ConjunctionCondition, FlatAnyOfSuccess) {
  auto any_of = AnyOf({IsFalse("false_variable"), IsTrue("true_variable")});
  EXPECT_EQ(any_of.GetOperation(), ConjunctionCondition::Operation::kAnyOf);

  EXPECT_THAT(
      any_of.GetConditions(),
      UnorderedElementsAre(
          VariantWith<Comparison>(AllOf(
              Property(&Comparison::state_variable_name, "false_variable"),
              Property(&Comparison::operation,
                       intrinsic_proto::icon::v1::Comparison::EQUAL))),
          VariantWith<Comparison>(
              AllOf(Property(&Comparison::state_variable_name, "true_variable"),
                    Property(&Comparison::operation,
                             intrinsic_proto::icon::v1::Comparison::EQUAL)))));
}

TEST(ConjunctionCondition, NestedAllOfAnyOf) {
  auto all_of = AllOf(
      {IsFalse("false_variable"),
       AnyOf({IsTrue("true_variable"), IsTrue("another_true_variable")})});
  EXPECT_EQ(all_of.GetOperation(), ConjunctionCondition::Operation::kAllOf);

  EXPECT_THAT(
      all_of.GetConditions(),
      UnorderedElementsAre(
          VariantWith<Comparison>(AllOf(
              Property(&Comparison::state_variable_name, "false_variable"),
              Property(&Comparison::operation,
                       intrinsic_proto::icon::v1::Comparison::EQUAL))),

          VariantWith<ConjunctionCondition>(AllOf(
              Property(&ConjunctionCondition::GetOperation,
                       ConjunctionCondition::Operation::kAnyOf),
              Property(
                  &ConjunctionCondition::GetConditions,
                  UnorderedElementsAre(
                      VariantWith<Comparison>(AllOf(
                          Property(&Comparison::state_variable_name,
                                   "true_variable"),
                          Property(
                              &Comparison::operation,
                              intrinsic_proto::icon::v1::Comparison::EQUAL))),
                      VariantWith<Comparison>(
                          AllOf(Property(&Comparison::state_variable_name,
                                         "another_true_variable"),
                                Property(&Comparison::operation,
                                         intrinsic_proto::icon::v1::Comparison::
                                             EQUAL)))))))));
}

TEST(ConjunctionCondition, NestedAnyOfAllOf) {
  auto any_of = AnyOf(
      {IsFalse("false_variable"),
       AllOf({IsTrue("true_variable"), IsTrue("another_true_variable")})});
  EXPECT_EQ(any_of.GetOperation(), ConjunctionCondition::Operation::kAnyOf);

  EXPECT_THAT(
      any_of.GetConditions(),
      UnorderedElementsAre(
          VariantWith<Comparison>(AllOf(
              Property(&Comparison::state_variable_name, "false_variable"),
              Property(&Comparison::operation,
                       intrinsic_proto::icon::v1::Comparison::EQUAL))),

          VariantWith<ConjunctionCondition>(AllOf(
              Property(&ConjunctionCondition::GetOperation,
                       ConjunctionCondition::Operation::kAllOf),
              Property(
                  &ConjunctionCondition::GetConditions,
                  UnorderedElementsAre(
                      VariantWith<Comparison>(AllOf(
                          Property(&Comparison::state_variable_name,
                                   "true_variable"),
                          Property(
                              &Comparison::operation,
                              intrinsic_proto::icon::v1::Comparison::EQUAL))),
                      VariantWith<Comparison>(
                          AllOf(Property(&Comparison::state_variable_name,
                                         "another_true_variable"),
                                Property(&Comparison::operation,
                                         intrinsic_proto::icon::v1::Comparison::
                                             EQUAL)))))))));
}

TEST(ConjunctionCondition, Equals) {
  ConjunctionCondition cond1(ConjunctionCondition::Operation::kAllOf,
                             {IsTrue("foo"), IsFalse("bar")});
  ConjunctionCondition cond2(ConjunctionCondition::Operation::kAnyOf,
                             {IsTrue("foo"), IsFalse("bar")});
  ConjunctionCondition cond3(ConjunctionCondition::Operation::kAnyOf,
                             {IsTrue("foo"), IsFalse("bar"), IsTrue("baz")});
  ConjunctionCondition cond4(ConjunctionCondition::Operation::kAnyOf,
                             {IsTrue("foo"), IsFalse("bar"), IsTrue("baz")});

  EXPECT_EQ(cond1, cond1);
  EXPECT_EQ(cond2, cond2);
  EXPECT_EQ(cond3, cond3);
  EXPECT_EQ(cond3, cond4);
  EXPECT_EQ(cond4, cond3);
  EXPECT_NE(cond1, cond2);
  EXPECT_NE(cond1, cond3);
  EXPECT_NE(cond2, cond3);
}

TEST(ConjunctionCondition, IsHashable) {
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly({
      ConjunctionCondition(
          ConjunctionCondition::Operation::kAllOf,
          {Comparison("foo", Comparison::FloatOperator::kApproxEqual, 0.0),
           Comparison("bar", Comparison::FloatOperator::kApproxNotEqual, 0.1)}),
      ConjunctionCondition(
          ConjunctionCondition::Operation::kAllOf,
          {Comparison("foo", Comparison::FloatOperator::kApproxEqual, 0.0),
           Comparison("bar", Comparison::FloatOperator::kApproxNotEqual, 0.1)}),
      ConjunctionCondition(
          ConjunctionCondition::Operation::kAnyOf,
          {Comparison("foo", Comparison::FloatOperator::kApproxEqual, 0.0),
           Comparison("bar", Comparison::FloatOperator::kApproxNotEqual, 0.1)}),
  }));
}

TEST(NegatedCondition, FlatNotDataCheck) {
  auto not_clause = Not({IsFalse("false_variable")});

  EXPECT_THAT(not_clause.GetCondition(),
              VariantWith<Comparison>(AllOf(
                  Property(&Comparison::state_variable_name, "false_variable"),
                  Property(&Comparison::operation,
                           intrinsic_proto::icon::v1::Comparison::EQUAL))));
}

TEST(NegatedCondition, IsHashable) {
  std::vector<Condition> conditions = {
      NegatedCondition(
          Comparison("foo", Comparison::FloatOperator::kApproxEqual, 0.0)),
      NegatedCondition(
          Comparison("foo", Comparison::FloatOperator::kApproxEqual, 0.0)),
      NegatedCondition(
          Comparison("foo", Comparison::FloatOperator::kApproxNotEqual, 0.0)),
      Comparison("foo", Comparison::FloatOperator::kApproxEqual,
                 0.0),  // Add non negated versions to see that this does not
                        // pose an issue.
      Comparison("foo", Comparison::FloatOperator::kApproxNotEqual, 0.0)};
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(conditions));
}

TEST(NegatedCondition, Equals) {
  NegatedCondition cond1(IsTrue("foo"));
  NegatedCondition cond2(IsTrue("foo"));
  NegatedCondition cond3(IsTrue("bar"));
  Condition cond4(IsTrue("bar"));

  EXPECT_EQ(cond1, cond1);
  EXPECT_EQ(cond2, cond2);
  EXPECT_EQ(cond1, cond2);
  EXPECT_EQ(Condition(cond1), Condition(cond2));
  EXPECT_NE(cond1, cond3);
  EXPECT_NE(cond2, cond3);
  EXPECT_NE(Condition(cond1), Condition(cond3));
  EXPECT_NE(cond4, Condition(cond3));
}

TEST(NegatedCondition, NestedNotAllOf) {
  auto not_clause =
      Not(AllOf({IsTrue("true_variable"), IsTrue("another_true_variable")}));

  EXPECT_THAT(
      not_clause.GetCondition(),
      VariantWith<ConjunctionCondition>(AllOf(
          Property(&ConjunctionCondition::GetOperation,
                   ConjunctionCondition::Operation::kAllOf),
          Property(
              &ConjunctionCondition::GetConditions,
              UnorderedElementsAre(
                  VariantWith<Comparison>(AllOf(
                      Property(&Comparison::state_variable_name,
                               "true_variable"),
                      Property(&Comparison::operation,
                               intrinsic_proto::icon::v1::Comparison::EQUAL))),
                  VariantWith<Comparison>(AllOf(
                      Property(&Comparison::state_variable_name,
                               "another_true_variable"),
                      Property(
                          &Comparison::operation,
                          intrinsic_proto::icon::v1::Comparison::EQUAL))))))));
}

TEST(NegatedCondition, NestedAllOfNot) {
  auto all_of =
      AllOf({Not(IsTrue("true_variable")), IsTrue("another_true_variable")});

  EXPECT_THAT(
      all_of.GetConditions(),
      UnorderedElementsAre(
          VariantWith<NegatedCondition>(AllOf(Property(
              &NegatedCondition::GetCondition,
              VariantWith<Comparison>(AllOf(
                  Property(&Comparison::state_variable_name, "true_variable"),
                  Property(&Comparison::operation,
                           intrinsic_proto::icon::v1::Comparison::EQUAL)))))),

          VariantWith<Comparison>(
              AllOf(Property(&Comparison::state_variable_name,
                             "another_true_variable"),
                    Property(&Comparison::operation,
                             intrinsic_proto::icon::v1::Comparison::EQUAL)))));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
