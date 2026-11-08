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

#include "intrinsic/icon/cc_client/operational_status.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sstream>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "internal/testing.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::testing::IsEmpty;

TEST(OperationalStatusTest, DisabledFactory) {
  OperationalStatus status = OperationalStatus::Disabled();
  EXPECT_EQ(status.state(), OperationalState::kDisabled);
  EXPECT_THAT(status.fault_reason(), IsEmpty());
}

TEST(OperationalStatusTest, FaultedFactory) {
  OperationalStatus status = OperationalStatus::Faulted("Cosmic ray bitflip");
  EXPECT_EQ(status.state(), OperationalState::kFaulted);
  EXPECT_EQ(status.fault_reason(), "Cosmic ray bitflip");
}

TEST(OperationalStatusTest, EnabledFactory) {
  OperationalStatus status = OperationalStatus::Enabled();
  EXPECT_EQ(status.state(), OperationalState::kEnabled);
  EXPECT_THAT(status.fault_reason(), IsEmpty());
}

TEST(OperationalStatusTest, IsDisabled) {
  EXPECT_TRUE(IsDisabled(OperationalStatus::Disabled()));
  EXPECT_FALSE(IsDisabled(OperationalStatus::Faulted("")));
  EXPECT_FALSE(IsDisabled(OperationalStatus::Enabled()));
}

TEST(OperationalStatusTest, IsFaulted) {
  EXPECT_FALSE(IsFaulted(OperationalStatus::Disabled()));
  EXPECT_TRUE(IsFaulted(OperationalStatus::Faulted("")));
  EXPECT_FALSE(IsFaulted(OperationalStatus::Enabled()));
}

TEST(OperationalStatusTest, IsEnabled) {
  EXPECT_FALSE(IsEnabled(OperationalStatus::Disabled()));
  EXPECT_FALSE(IsEnabled(OperationalStatus::Faulted("")));
  EXPECT_TRUE(IsEnabled(OperationalStatus::Enabled()));
}

TEST(OperationalStatusTest, OperationalStateToString) {
  EXPECT_EQ(ToString(OperationalState::kDisabled), "DISABLED");
  EXPECT_EQ(ToString(OperationalState::kFaulted), "FAULTED");
  EXPECT_EQ(ToString(OperationalState::kEnabled), "ENABLED");
}

TEST(OperationalStatusTest, OperationalStateToOstream) {
  {
    std::ostringstream o;
    o << OperationalState::kDisabled;
    EXPECT_EQ(o.str(), "DISABLED");
  }
  {
    std::ostringstream o;
    o << OperationalState::kFaulted;
    EXPECT_EQ(o.str(), "FAULTED");
  }
  {
    std::ostringstream o;
    o << OperationalState::kEnabled;
    EXPECT_EQ(o.str(), "ENABLED");
  }
}

TEST(OperationalStatusTest, OperationalStatusToString) {
  EXPECT_EQ(ToString(OperationalStatus::Disabled()), "DISABLED");
  EXPECT_EQ(ToString(OperationalStatus::Faulted(
                "I'm sorry, Dave. I'm afraid I can't do that.")),
            "FAULTED(I'm sorry, Dave. I'm afraid I can't do that.)");
  EXPECT_EQ(ToString(OperationalStatus::Enabled()), "ENABLED");
}

TEST(OperationalStatusTest, OperationalStatusToOstream) {
  {
    std::ostringstream o;
    o << OperationalStatus::Disabled();
    EXPECT_EQ(o.str(), "DISABLED");
  }
  {
    std::ostringstream o;
    o << OperationalStatus::Faulted("Vogons");
    EXPECT_EQ(o.str(), "FAULTED(Vogons)");
  }
  {
    std::ostringstream o;
    o << OperationalStatus::Enabled();
    EXPECT_EQ(o.str(), "ENABLED");
  }
}

TEST(OperationalStatusTest, OperationalStateFromProto) {
  EXPECT_THAT(FromProto(intrinsic_proto::icon::v1::OperationalState::DISABLED),
              IsOkAndHolds(OperationalState::kDisabled));
  EXPECT_THAT(FromProto(intrinsic_proto::icon::v1::OperationalState::FAULTED),
              IsOkAndHolds(OperationalState::kFaulted));
  EXPECT_THAT(FromProto(intrinsic_proto::icon::v1::OperationalState::ENABLED),
              IsOkAndHolds(OperationalState::kEnabled));
  EXPECT_THAT(FromProto(intrinsic_proto::icon::v1::OperationalState::UNKNOWN),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(OperationalStatusTest, DisabledToProto) {
  intrinsic_proto::icon::v1::OperationalStatus expected;
  expected.set_state(intrinsic_proto::icon::v1::OperationalState::DISABLED);
  EXPECT_THAT(ToProto(OperationalStatus::Disabled()), EqualsProto(expected));
}

TEST(OperationalStatusTest, FaultedToProto) {
  intrinsic_proto::icon::v1::OperationalStatus expected;
  expected.set_state(intrinsic_proto::icon::v1::OperationalState::FAULTED);
  expected.set_fault_reason("Empty beer tank.");
  EXPECT_THAT(ToProto(OperationalStatus::Faulted("Empty beer tank.")),
              EqualsProto(expected));
}

TEST(OperationalStatusTest, DisabledFromProto) {
  intrinsic_proto::icon::v1::OperationalStatus proto;
  proto.set_state(intrinsic_proto::icon::v1::OperationalState::DISABLED);
  ASSERT_OK_AND_ASSIGN(OperationalStatus status, FromProto(proto));
  EXPECT_TRUE(IsDisabled(status));
}

TEST(OperationalStatusTest, FaultedFromProto) {
  intrinsic_proto::icon::v1::OperationalStatus proto;
  proto.set_state(intrinsic_proto::icon::v1::OperationalState::FAULTED);
  proto.set_fault_reason("Joint limits violated");
  ASSERT_OK_AND_ASSIGN(OperationalStatus status, FromProto(proto));
  EXPECT_TRUE(IsFaulted(status));
  EXPECT_EQ(status.fault_reason(), "Joint limits violated");
}

TEST(OperationalStatusTest, EnabledFromProto) {
  intrinsic_proto::icon::v1::OperationalStatus proto;
  proto.set_state(intrinsic_proto::icon::v1::OperationalState::ENABLED);
  ASSERT_OK_AND_ASSIGN(OperationalStatus status, FromProto(proto));
  EXPECT_TRUE(IsEnabled(status));
}

TEST(OperationalStatusTest, FromProtoFailsIfReasonIsSet) {
  intrinsic_proto::icon::v1::OperationalStatus proto;
  proto.set_state(intrinsic_proto::icon::v1::OperationalState::ENABLED);
  proto.set_fault_reason("non-empty fault reason");
  EXPECT_THAT(FromProto(proto), StatusIs(absl::StatusCode::kInvalidArgument));
  proto.set_state(intrinsic_proto::icon::v1::OperationalState::DISABLED);
  EXPECT_THAT(FromProto(proto), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(OperationalStatusTest, UnknownStatusFromProtoFails) {
  intrinsic_proto::icon::v1::OperationalStatus proto;
  proto.set_state(intrinsic_proto::icon::v1::OperationalState::UNKNOWN);
  EXPECT_THAT(FromProto(proto), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(OperationalStatusTest, BadThings) {
  // Please never do this!
  auto invalid_state = static_cast<OperationalState>(1234);
  EXPECT_EQ(ToProto(invalid_state),
            intrinsic_proto::icon::v1::OperationalState::UNKNOWN);
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
