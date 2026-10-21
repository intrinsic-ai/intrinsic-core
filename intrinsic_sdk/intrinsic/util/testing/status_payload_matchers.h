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

#ifndef INTRINSIC_UTIL_TESTING_STATUS_PAYLOAD_MATCHERS_H_
#define INTRINSIC_UTIL_TESTING_STATUS_PAYLOAD_MATCHERS_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "google/rpc/status.pb.h"
#include "intrinsic/util/proto/type_url.h"

namespace intrinsic::testing {

inline const absl::Status& GetStatus(const absl::Status& status) {
  return status;
}

template <typename T>
inline const absl::Status& GetStatus(const absl::StatusOr<T>& status_or) {
  return status_or.status();
}

template <typename M, class InnerProtoMatcher,
          typename =
              std::enable_if_t<std::is_base_of_v<google::protobuf::Message, M>>>
class StatusPayloadProtoMatcher {
 public:
  using is_gtest_matcher = void;

  explicit StatusPayloadProtoMatcher(const InnerProtoMatcher& proto_matcher)
      : proto_matcher_(proto_matcher) {}

  template <typename S>
  bool MatchAndExplain(const S& some_status, std::ostream* os) const {
    const absl::Status& status = GetStatus(some_status);
    if (status.ok()) {
      if (os) {
        *os << "which is OK and has no payload";
      }
      return false;
    }

    std::optional<absl::Cord> payload =
        status.GetPayload(AddTypeUrlPrefix<M>());
    if (payload.has_value()) {
      M message;
      if (!message.ParseFromString(*payload)) {
        if (os) {
          *os << "which has payload '" << AddTypeUrlPrefix<M>()
              << "' but which cannot be parsed as proto '"
              << M::descriptor()->full_name() << "'";
        }
        return false;
      }
      ::testing::StringMatchResultListener match_result;
      if (proto_matcher_.impl().MatchAndExplain(message, &match_result)) {
        if (os) {
          *os << match_result.str();
        }
        return true;
      }
      if (os) {
        *os << "which has payload '" << AddTypeUrlPrefix<M>() << "' "
            << match_result.str();
      }
      return false;
    }

    payload = status.GetPayload(AddTypeUrlPrefix<google::rpc::Status>());
    if (payload.has_value()) {
      google::rpc::Status rpc_status;
      if (!rpc_status.ParseFromString(*payload)) {
        if (os) {
          *os << "which has payload '"
              << AddTypeUrlPrefix<google::rpc::Status>()
              << "' but which cannot be parsed as proto '"
              << google::rpc::Status::descriptor()->full_name() << "'";
        }
        return false;
      }

      for (const google::protobuf::Any& detail : rpc_status.details()) {
        if (detail.type_url() == AddTypeUrlPrefix<M>()) {
          M message;
          if (!message.ParseFromString(*payload)) {
            if (os) {
              *os << "which has payload '" << AddTypeUrlPrefix<M>()
                  << "' inside a '" << AddTypeUrlPrefix<google::rpc::Status>()
                  << "' payload but which cannot be parsed as proto '"
                  << M::descriptor()->full_name() << "'";
            }
            return false;
          }
          ::testing::StringMatchResultListener match_result;
          if (proto_matcher_.impl().MatchAndExplain(message, &match_result)) {
            if (os) {
              *os << match_result.str();
            }
            return true;
          }
          if (os) {
            *os << "which has payload '" << AddTypeUrlPrefix<M>() << "' "
                << match_result.str();
          }
          return false;
        }
      }
    }

    if (os) {
      *os << "which has no payload '" << AddTypeUrlPrefix<M>()
          << "' and neither a '" << AddTypeUrlPrefix<google::rpc::Status>()
          << "' that contains a '" << M::descriptor()->full_name() << " detail";
    }
    return false;
  }

  void DescribeTo(std::ostream* os) const {
    if (os) {
      *os << "has a payload that ";
    }
    proto_matcher_.impl().DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const {
    if (os) {
      *os << "has no payload that ";
    }
    proto_matcher_.impl().DescribeTo(os);
  }

 private:
  const InnerProtoMatcher proto_matcher_;
};

class StatusPayloadGenericMatcher {
 public:
  using is_gtest_matcher = void;

  template <typename PayloadMatcher>
  explicit StatusPayloadGenericMatcher(std::string_view url,
                                       const PayloadMatcher& payload_matcher)
      : url_(url),
        payload_matcher_(
            ::testing::SafeMatcherCast<std::string>(payload_matcher)) {}

  template <typename S>
  bool MatchAndExplain(const S& some_status, std::ostream* os) const {
    const absl::Status& status = GetStatus(some_status);
    if (status.ok()) {
      if (os) {
        *os << "which is OK and has no payload";
      }
      return false;
    }

    std::optional<absl::Cord> payload = status.GetPayload(url_);
    if (!payload.has_value()) {
      if (os) {
        *os << "which has no generic payload '" << url_ << "'";
      }
      return false;
    }

    ::testing::StringMatchResultListener match_result;
    if (payload_matcher_.MatchAndExplain(std::string(*payload),
                                         &match_result)) {
      if (os) {
        *os << match_result.str();
      }
      return true;
    }
    if (os) {
      *os << "which has payload '" << url_ << "' " << match_result.str();
    }
    return false;
  }

  void DescribeTo(std::ostream* os) const {
    if (os) {
      *os << "has a payload '" << url_ << "' that ";
    }
    payload_matcher_.DescribeTo(os);
  }

  void DescribeNegationTo(std::ostream* os) const {
    if (os) {
      *os << "has no payload '" << url_ << "' that ";
    }
    payload_matcher_.DescribeTo(os);
  }

 private:
  std::string url_;
  const ::testing::Matcher<std::string> payload_matcher_;
};

template <typename M, class InnerProtoMatcher,
          typename =
              std::enable_if_t<std::is_base_of_v<google::protobuf::Message, M>>>
inline StatusPayloadProtoMatcher<M, InnerProtoMatcher> StatusHasProtoPayload(
    InnerProtoMatcher proto_matcher) {
  return StatusPayloadProtoMatcher<M, InnerProtoMatcher>(proto_matcher);
}

inline StatusPayloadGenericMatcher StatusHasGenericPayload(
    std::string_view url) {
  return StatusPayloadGenericMatcher(url, ::testing::_);
}

template <typename InnerMatcher>
inline StatusPayloadGenericMatcher StatusHasGenericPayload(
    std::string_view url, InnerMatcher payload_matcher) {
  return StatusPayloadGenericMatcher(url, std::move(payload_matcher));
}

}  // namespace intrinsic::testing

#endif  // INTRINSIC_UTIL_TESTING_STATUS_PAYLOAD_MATCHERS_H_
