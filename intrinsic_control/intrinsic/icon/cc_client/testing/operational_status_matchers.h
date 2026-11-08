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

#ifndef INTRINSIC_ICON_CC_CLIENT_TESTING_OPERATIONAL_STATUS_MATCHERS_H_
#define INTRINSIC_ICON_CC_CLIENT_TESTING_OPERATIONAL_STATUS_MATCHERS_H_

#include <gmock/gmock.h>

#include <sstream>
#include <string>

#include "intrinsic/icon/cc_client/operational_status.h"

namespace intrinsic::icon {

namespace internal {
// Describes a matcher to a string output.
template <typename PolymorphicMatcherType, typename MatcherType = std::string>
static std::string DescribeToString(const PolymorphicMatcherType& matcher) {
  ::std::stringstream ss;
  ::testing::MatcherCast<MatcherType>(matcher).DescribeTo(&ss);
  return ss.str();
}

// Describes a matcher's negation to a string output.
template <typename PolymorphicMatcherType, typename MatcherType = std::string>
static std::string DescribeNegationToString(
    const PolymorphicMatcherType& matcher) {
  ::std::stringstream ss;
  ::testing::MatcherCast<MatcherType>(matcher).DescribeNegationTo(&ss);
  return ss.str();
}
}  // namespace internal

// The OperationalStatusIs matcher checks if an OperationalStatus is in the
// expected state.
//
// Example:
//
//  EXPECT_THAT(OperationalStatus::Disabled(),
//              OperationalStatusIs(OperationalState::kDisabled));
MATCHER_P(OperationalStatusIs, expected_state,
          (negation ? "is not " : "is ") + ToString(expected_state)) {
  return arg.state() == expected_state;
}

// The OperationalStatusIsFaulted matcher checks if an OperationalStatus is in
// the faulted state, with a fault reason that matches `fault_reason_matcher`.
//
// Example:
//
//  EXPECT_THAT(OperationalStatus::Faulted("joint limits exceeded"),
//              OperationalStatusIsFaulted(::testing::HasSubstr("limits")));
MATCHER_P(
    OperationalStatusIsFaulted, fault_reason_matcher,
    (negation ? "is not faulted " : "is faulted") +
        (negation ? " or fault_reason " +
                        internal::DescribeNegationToString(fault_reason_matcher)
                  : " and fault_reason " +
                        internal::DescribeToString(fault_reason_matcher))) {
  return IsFaulted(arg) &&
         ::testing::MatcherCast<const std::string&>(fault_reason_matcher)
             .Matches(arg.fault_reason());
}

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CC_CLIENT_TESTING_OPERATIONAL_STATUS_MATCHERS_H_
