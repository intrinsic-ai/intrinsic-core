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

#include "intrinsic/icon/common/part_group.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "intrinsic/icon/proto/v1/types.pb.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::protobuf_matchers::EqualsProto;
using ::testing::UnorderedElementsAre;

TEST(Client, PartContains) {}

TEST(Client, PartEqualsSinglePartGroup) {
  EXPECT_EQ(Part("arm"), PartGroup({"arm"}));
  EXPECT_NE(Part("arm"), PartGroup({"gripper"}));
}

TEST(Client, PartGroupContains) {
  EXPECT_THAT(PartGroup(), UnorderedElementsAre());
  EXPECT_THAT(Part("arm"), UnorderedElementsAre("arm"));
  EXPECT_THAT(PartGroup({"arm", "gripper"}),
              UnorderedElementsAre("arm", "gripper"));
}

TEST(Client, IsSinglePart) {
  EXPECT_FALSE(IsSinglePart({}));
  EXPECT_TRUE(IsSinglePart(Part("arm")));
  EXPECT_FALSE(IsSinglePart({"arm", "gripper"}));
}

TEST(Client, GetIfSinglePart) {
  EXPECT_EQ(GetIfSinglePart({}), nullptr);
  EXPECT_EQ(*GetIfSinglePart(Part("arm")), "arm");
  EXPECT_EQ(GetIfSinglePart({"arm", "gripper"}), nullptr);
}

TEST(Client, FromProto) {
  // Empty part group.
  EXPECT_EQ(FromProto(intrinsic_proto::icon::v1::PartGroup()), PartGroup());

  // One part.
  intrinsic_proto::icon::v1::PartGroup single_part_proto;
  single_part_proto.add_parts("arm");
  EXPECT_EQ(FromProto(single_part_proto), Part("arm"));

  // Two parts.
  intrinsic_proto::icon::v1::PartGroup multi_parts_proto;
  multi_parts_proto.add_parts("arm");
  multi_parts_proto.add_parts("gripper");
  EXPECT_EQ(FromProto(multi_parts_proto), PartGroup({"gripper", "arm"}));
}

TEST(Client, ToProto) {
  // Empty proto.
  EXPECT_THAT(ToProto(PartGroup()),
              EqualsProto(intrinsic_proto::icon::v1::PartGroup()));

  // One part.
  intrinsic_proto::icon::v1::PartGroup single_part_proto;
  single_part_proto.add_parts("arm");
  EXPECT_THAT(ToProto(Part("arm")), EqualsProto(single_part_proto));

  // Two parts.
  intrinsic_proto::icon::v1::PartGroup multi_parts_proto;
  multi_parts_proto.add_parts("arm");
  multi_parts_proto.add_parts("gripper");
  EXPECT_THAT(ToProto(PartGroup({"gripper", "arm"})),
              EqualsProto(multi_parts_proto));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
