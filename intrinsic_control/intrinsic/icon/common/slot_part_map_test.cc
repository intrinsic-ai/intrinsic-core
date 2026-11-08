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

#include "intrinsic/icon/common/slot_part_map.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::intrinsic::ParseTextProtoOrDie;
using ::protobuf_matchers::EqualsProto;
using ::testing::ElementsAre;
using ::testing::Pair;

TEST(SlotPartMap, FromProto) {
  intrinsic_proto::icon::v1::SlotPartMap map_proto = ParseTextProtoOrDie(R"pb(
    slot_name_to_part_name { key: "slot_1" value: "some_part" }
  )pb");

  EXPECT_THAT(SlotPartMapFromProto(map_proto),
              ElementsAre(Pair("slot_1", "some_part")));
}

TEST(SlotPartMap, ProtoRoundTrip) {
  intrinsic_proto::icon::v1::SlotPartMap map_proto = ParseTextProtoOrDie(R"pb(
    slot_name_to_part_name { key: "slot_1" value: "some_part" }
    slot_name_to_part_name { key: "slot_2" value: "another_part" }
  )pb");

  EXPECT_THAT(ToProto(SlotPartMapFromProto(map_proto)), EqualsProto(map_proto));
}

}  // namespace
}  // namespace intrinsic::icon
