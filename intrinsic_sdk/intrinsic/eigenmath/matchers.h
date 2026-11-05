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

// This file contains gmock matchers for eigenmath types.

#ifndef INTRINSIC_EIGENMATH_MATCHERS_H_
#define INTRINSIC_EIGENMATH_MATCHERS_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "Eigen/Core"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gloop/util/gtl/iterator_adaptors.h"
#include "google/protobuf/descriptor.h"
#include "intrinsic/eigenmath/type_checks.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace eigenmath {
namespace testing {
namespace matchers_internal {
// Return whether the type T has a difference operator.
template <typename T, typename = void>
static constexpr bool HasDifferenceOp = false;
template <typename T>
static constexpr bool HasDifferenceOp<
    T, std::void_t<decltype(std::declval<T>() - std::declval<T>())>> = true;

template <typename ArgType, typename ExpectedType, typename Scalar>
bool GenericIsApprox(ArgType&& arg, ExpectedType&& expected,
                     Scalar first_tolerance, Scalar second_tolerance,
                     ::testing::MatchResultListener* result_listener) {
  using TestType = std::decay_t<ArgType>;
  if constexpr (HasDifferenceOp<TestType>) {
    // If either tensor (vector/matrix/others) has at least one NaN, we will
    // consider both tensors as different (or not comparable), and thus returns
    // false here.
    if (arg.array().isNaN().any() || expected.array().isNaN().any()) {
      *result_listener << "\n One array contains at least one NaN.";
      return false;
    }
    // We need to check the dimension first otherwise arg-expected asserts.
    if (arg.rows() != expected.rows() || arg.cols() != expected.cols()) {
      *result_listener << "\n dimension mismatch: actual=" << arg.rows() << "x"
                       << arg.cols() << ", expected=" << expected.rows() << "x"
                       << expected.cols();
      return false;
    }
    // Ensure that a comparison against a zero matrix/vector does not fail. This
    // is, because isApprox() compares (arg - expected) to the minimum norm of
    // arg and expected, which is 0 in case of a zero vector/matrix.
    // See, for example,
    // https://github.com/intrinsic-ai/intrinsic-core/blob/main/google3/third_party/eigen3/Eigen/src/Core/Fuzzy.h#L93
    if ((arg - expected).cwiseAbs().maxCoeff() < first_tolerance) return true;
    return expected.isApprox(arg, first_tolerance);
  } else if constexpr (IsPose<TestType>) {
    if (expected.isApprox(arg, first_tolerance, second_tolerance)) {
      return true;
    }
    decltype(arg) delta = arg * expected.inverse();
    *result_listener << "\n difference:\n" << delta;
    if constexpr (std::decay_t<
                      decltype(delta.translation())>::RowsAtCompileTime == 2) {
      const decltype(arg.translation()) translation_diff =
          (arg.translation() - expected.translation()).eval();
      *result_listener << "\n translation difference = "
                       << translation_diff.transpose()
                       << " (norm = " << translation_diff.norm() << " m)";
      const auto so2_diff = (expected.so2().inverse() * arg.so2());
      *result_listener << "\n rotation difference = " << so2_diff
                       << " (norm = " << so2_diff.norm() << " rad)";
    } else {
      const decltype(arg.translation()) translation_diff =
          (arg.translation() - expected.translation()).eval();
      *result_listener << "\n translation difference = "
                       << translation_diff.transpose()
                       << " (norm = " << translation_diff.norm() << " m)";
      const auto so3_diff = (expected.so3().inverse() * arg.so3());
      *result_listener << "\n rotation difference = " << so3_diff
                       << " (norm = " << so3_diff.norm() << " rad)";
    }
    return false;
  } else {
    return expected.isApprox(arg, first_tolerance);
  }
}
}  // namespace matchers_internal

// Returns true if the arg matches the given expected value within the given
// tolerance.
//
// Usage:
// const double kApproxTolerance = 1e-12;
// Pose3d actual_world_t_target;
// Pose3d expected_world_t_target;
//
// EXPECT_THAT(actual_world_t_target,
//             testing::IsApprox(expected_world_t_target, kApproxTolerance));
MATCHER_P2(IsApprox, expected, tolerance,
           std::string(negation ? "is not" : "is") +
               " approximately equal to:\n" +
               ::testing::PrintToString(expected) + "\n with tolerance " +
               ::testing::PrintToString(tolerance)) {
  return matchers_internal::GenericIsApprox(arg, expected, tolerance, tolerance,
                                            result_listener);
}

// Returns true if the arg matches the given expected value within the default
// tolerance.
//
// Usage:
// Pose3d actual_world_t_target;
// Pose3d expected_world_t_target;
//
// EXPECT_THAT(actual_world_t_target,
//             testing::IsApprox(expected_world_t_target));
MATCHER_P(IsApprox, expected,
          std::string(negation ? "is not" : "is") +
              " approximately equal to\n" +
              ::testing::PrintToString(expected)) {
  using Scalar = ScalarTypeOf<std::decay_t<decltype(arg)>>;
  return matchers_internal::GenericIsApprox(
      arg, expected, Eigen::NumTraits<Scalar>::dummy_precision(),
      Eigen::NumTraits<Scalar>::dummy_precision(), result_listener);
}

// Returns a matcher that checks if some actual range matches the expected
// range of values using the IsApprox matcher for each element.
template <typename Range>
auto ElementsAreApprox(Range&& expected, double tolerance) {
  return ::testing::ElementsAreArray(gtl::projection_view(
      expected,
      [tolerance](const auto& elem) { return IsApprox(elem, tolerance); }));
}

template <typename Range>
auto UnorderedElementsAreApprox(Range&& expected, double tolerance) {
  return ::testing::UnorderedElementsAreArray(gtl::projection_view(
      expected,
      [tolerance](const auto& elem) { return IsApprox(elem, tolerance); }));
}

// Returns true if the arg is approximately the same eigenvector as the given
// expected vector within the given tolerance.
//
// What makes two vectors the same eigenvector is if they:
//  - have the same magnitude
//  - are colinear
// In other words, a flip in sign of all components is allowed.
//
// Usage:
// const double kApproxTolerance = 1e-12;
// eigenmath::Vector3d actual_eigenvector;
// eigenmath::Vector3d expected_eigenvector;
//
// EXPECT_THAT(actual_eigenvector,
//     testing::IsApproxEigenVector(expected_eigenvector, kApproxTolerance));
MATCHER_P2(IsApproxEigenVector, expected, tolerance,
           std::string(negation ? "is not" : "is") +
               "approximately the same eigenvector as\n" +
               ::testing::PrintToString(expected) + " with tolerance " +
               ::testing::PrintToString(tolerance)) {
  if (arg.dot(expected) < 0.0) {
    return expected.isApprox(-arg, tolerance);
  } else {
    return expected.isApprox(arg, tolerance);
  }
}

// Returns true if the two-tuple arg's members match each other within the given
// tolerance.
//
// This is particularly useful for matching the contents of collections:
//
// const double kApproxTolerance = 1e-12;
// std::vector<eigenmath::Pose3d> actual_collection;
// std::vector<eigenmath::Pose3d> expected_collection;
//
// EXPECT_THAT(actual_collection,
//   ::testing::Pointwise(
//     ::IsApproxTuple(kApproxTolerance),
//     expected_collection))

MATCHER_P(IsApproxTuple, tolerance, "") {
  return matchers_internal::GenericIsApprox(std::get<0>(arg), std::get<1>(arg),
                                            tolerance, tolerance,
                                            result_listener);
}

// Returns true if the two-tuple arg's members match each other within the given
// tolerance.
//
// This is particularly useful for matching the contents of collections:
//
//  std::vector<eigenmath::Pose3d> actual_collection;
//  std::vector<eigenmath::Pose3d> expected_collection;
//
//  EXPECT_THAT(actual_collection,
//    ::testing::Pointwise(
//      ::IsApproxTuple(), expected_collection));
MATCHER(IsApproxTuple, "") {
  using Scalar = ScalarTypeOf<std::decay_t<decltype(std::get<0>(arg))>>;
  return matchers_internal::GenericIsApprox(
      std::get<0>(arg), std::get<1>(arg),
      Eigen::NumTraits<Scalar>::dummy_precision(),
      Eigen::NumTraits<Scalar>::dummy_precision(), result_listener);
}

// Matcher function to compare two poses. Two thresholds are provided, the first
// one determines the threshold for the norm of the delta translation. The
// second one determines the threshold of the delta absolute angle.
//
// eigenmath::Pose3d a;
// eigenmath::Pose3d b;
// double threshold_translation = 0.5;
// double threshold_angle = 0.4;
//
// EXPECT_THAT(a, testing::IsApprox(b,threshold_translation, threshold_angle));
MATCHER_P3(IsApprox, expected, threshold_norm_translation, threshold_angle,
           std::string(negation ? "is not" : "is") +
               " approximately equal to:\n" +
               ::testing::PrintToString(expected) + "\n with tolerance " +
               ::testing::PrintToString(threshold_norm_translation) + "m and " +
               ::testing::PrintToString(threshold_angle) + "rad") {
  return matchers_internal::GenericIsApprox(arg, expected,
                                            threshold_norm_translation,
                                            threshold_angle, result_listener);
}

// Matches two undirected line segments (allows swapped endpoints).
MATCHER_P(IsApproxUndirected, tolerance,
          "has endpoints approximately equal to with tolerance " +
              ::testing::PrintToString(tolerance)) {
  using std::get;
  return (get<1>(arg).from.isApprox(get<0>(arg).from, tolerance) &&
          get<1>(arg).to.isApprox(get<0>(arg).to, tolerance)) ||
         (get<1>(arg).to.isApprox(get<0>(arg).from, tolerance) &&
          get<1>(arg).from.isApprox(get<0>(arg).to, tolerance));
}
template <typename T>
auto IsApproxUndirected(T&& expected, double tolerance)
    -> decltype(::testing::internal::MatcherBindSecond(
        IsApproxUndirected(tolerance), std::forward<T>(expected))) {
  return ::testing::internal::MatcherBindSecond(IsApproxUndirected(tolerance),
                                                std::forward<T>(expected));
}

// Matches two directed line segments.
MATCHER_P(IsApproxDirected, tolerance,
          "has endpoints approximately equal to with tolerance " +
              ::testing::PrintToString(tolerance)) {
  using std::get;
  return (get<1>(arg).from.isApprox(get<0>(arg).from, tolerance) &&
          get<1>(arg).to.isApprox(get<0>(arg).to, tolerance));
}
template <typename T>
auto IsApproxDirected(T&& expected, double tolerance)
    -> decltype(::testing::internal::MatcherBindSecond(
        IsApproxDirected(tolerance), std::forward<T>(expected))) {
  return ::testing::internal::MatcherBindSecond(IsApproxDirected(tolerance),
                                                std::forward<T>(expected));
}

// Takes a given proto and searches for all submessages of type matching
// "intrinsic_proto.Pose". It ignores these submessages and tests the
// remaining protos for equality. Then, it compares all of the submessages by
// converting them to intrinsic::Pose3d and compares them using the given
// tolerance.
// TODO(b/225404746): handle pose protos within repeated fields.
MATCHER_P2(IsApproximatelyPosed, proto_msg, tolerance, "matches pose update") {
  // Search the proto for nested pose fields
  std::vector<std::string> pose_fields;
  std::function<void(const google::protobuf::Message&, std::string)>
      add_pose_fields =
          [&](const google::protobuf::Message& msg, std::string path) {
            std::vector<const google::protobuf::FieldDescriptor*> msg_fields;
            msg.GetReflection()->ListFields(msg, &msg_fields);
            for (auto* msg_field : msg_fields) {
              auto* nested_msg = msg_field->message_type();
              if (!nested_msg) {
                continue;
              }

              const std::string msg_field_path =
                  path.empty() ? std::string(msg_field->name())
                               : absl::StrCat(path, ".", msg_field->name());
              std::string_view nested_msg_type(nested_msg->full_name());
              if (nested_msg_type == "intrinsic_proto.Pose") {
                pose_fields.push_back(msg_field_path);
              } else {
                add_pose_fields(msg.GetReflection()->GetMessage(msg, msg_field),
                                msg_field_path);
              }
            }
          };
  add_pose_fields(proto_msg, "");

  // Compare all proto fields using custom approximate matcher
  std::function<bool(const google::protobuf::Message&,
                     const google::protobuf::Message&, std::string)>
      check_poses = [&](const google::protobuf::Message& want,
                        const google::protobuf::Message& got,
                        std::string path_to_pose) {
        if (path_to_pose.empty()) {
          intrinsic_proto::Pose want_pose_proto;
          if (!want_pose_proto.MergeFromString(want.SerializeAsCord())) {
            return false;
          }

          intrinsic_proto::Pose got_pose_proto;
          if (!got_pose_proto.MergeFromString(got.SerializeAsCord())) {
            return false;
          }

          absl::StatusOr<Pose3d> want_pose = FromProto(want_pose_proto);
          absl::StatusOr<Pose3d> got_pose = FromProto(got_pose_proto);

          if (!want_pose.ok() || !got_pose.ok()) {
            return false;
          }

          return want_pose->isApprox(*got_pose, tolerance);
        } else {
          // Get the next field one level deeper
          std::vector<std::string> field_paths =
              absl::StrSplit(path_to_pose, '.');
          const absl::string_view field_path = field_paths[0];
          const std::string remaining_path =
              absl::StrJoin(field_paths.begin() + 1, field_paths.end(), ".");

          // Extract the field from each message
          auto message_for_field =
              [](const google::protobuf::Message& msg,
                 absl::string_view field) -> const google::protobuf::Message& {
            std::vector<const google::protobuf::FieldDescriptor*> msg_fields;
            msg.GetReflection()->ListFields(msg, &msg_fields);
            for (auto* msg_field : msg_fields) {
              if (msg_field->name() == field) {
                return msg.GetReflection()->GetMessage(msg, msg_field);
              }
            }

            // This is fatal because we already searched the protos for these
            // message fields
            LOG(FATAL) << "Can't find message at field '" << field << "'";
            return msg;
          };

          return check_poses(message_for_field(want, field_path),
                             message_for_field(got, field_path),
                             remaining_path);
        }
      };

  // Do the actual comparisons.
  ::protobuf_matchers::internal::ProtoComparison comp;
  comp.ignore_field_paths = pose_fields;
  bool match =
      ::protobuf_matchers::internal::ProtoCompare(comp, arg, proto_msg);

  for (auto& pose_field : pose_fields) {
    match = match && check_poses(arg, proto_msg, pose_field);
  }

  return match;
}

// Same as above but uses a default tolerance.
MATCHER_P(IsApproximatelyPosed, proto_msg, "matches pose update") {
  return ExplainMatchResult(
      IsApproximatelyPosed(proto_msg,
                           Eigen::NumTraits<double>::dummy_precision()),
      arg, result_listener);
}

struct EigenMatcherTolerance {
  std::optional<float> abs_error = std::nullopt;
  std::optional<float> rel_error = std::nullopt;

  template <typename LHS, typename RHS>
  bool operator()(const LHS& lhs, const RHS& rhs) const {
    if (abs_error.has_value() && ((lhs - rhs).norm() > abs_error.value())) {
      return false;
    }
    if (rel_error.has_value() &&
        ((lhs - rhs).norm() > rel_error.value() * rhs.norm())) {
      return false;
    }
    return true;
  }
};

// Returns a matcher that compares two Eigen matrices with a given tolerance.
// The tolerance can be specified as absolute or relative error.
//
// The absolute error is specified as a float or
// EigenMatcherTolerance{.abs_error = TOLERANCE_VALUE}. For example:
//   EXPECT_THAT(matrix, EigenMatrixNear(expected_matrix, 1e-6));
//   EXPECT_THAT(matrix, EigenMatrixNear(expected_matrix,
//                                       EigenMatcherTolerance{.abs_error =
//                                       1e-6}));
//
// The relative error is specified EigenMatcherTolerance{.rel_error =
// TOLERANCE_VALUE}. For example:
//   EXPECT_THAT(matrix, EigenMatrixNear(expected_matrix,
//                                       EigenMatcherTolerance{.rel_error =
//                                       TOLERANCE_VALUE}));
//
// To specify both absolute and relative error, use
// EigenMatcherTolerance{.abs_error = TOLERANCE_VALUE, .rel_error =
// TOLERANCE_VALUE}. For example:
//   EXPECT_THAT(matrix, EigenMatrixNear(expected_matrix,
//                                       EigenMatcherTolerance{.abs_error =
//                                       1e-6, .rel_error = 1e-3}));
// In that case, the matcher fails if either absolute or relative error is
// exceeded.
MATCHER_P2(EigenMatrixNear, matrix, error, "") {
  return EigenMatcherTolerance{error}(arg, matrix);
}

// Returns multiple matcher objects for a vector of Eigen matrices.
// This function can be used in context with EXPECT_THAT as in this example:
//   using ::testing::ElementsAreArray;
//   std::vector<Vector3f> some_data = ...;
//   EXPECT_THAT(some_data,
//     ElementsAreArray(ArrayEigenMatrixNear(std::vector<Vector3f>{
//       Vector3f(1, 1, 1), Vector3f(1, 1, 1), ...
//     })));
template <typename T, int Rows, int Cols, int Options>
std::vector<::testing::Matcher<Eigen::Matrix<T, Rows, Cols, Options>>>
ArrayEigenMatrixNear(
    const std::vector<Eigen::Matrix<T, Rows, Cols, Options>>& values,
    float max_abs_error = 1e-5) {
  std::vector<::testing::Matcher<Eigen::Matrix<T, Rows, Cols, Options>>>
      matchers;
  matchers.reserve(values.size());
  for (const auto& v : values) {
    matchers.emplace_back(EigenMatrixNear(v, max_abs_error));
  }
  return matchers;
}

}  // namespace testing
}  // namespace eigenmath
}  // namespace intrinsic

#endif  // INTRINSIC_EIGENMATH_MATCHERS_H_
