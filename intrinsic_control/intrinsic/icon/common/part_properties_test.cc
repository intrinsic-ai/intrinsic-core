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

#include "intrinsic/icon/common/part_properties.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "internal/testing.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::protobuf_matchers::EqualsProto;

TEST(PartPropertyValueProtoConversion, BoolRoundtrip) {
  ::intrinsic_proto::icon::v1::PartPropertyValue proto;
  proto.set_bool_value(true);
  ASSERT_OK_AND_ASSIGN(PartPropertyValue value, FromProto(proto));
  EXPECT_THAT(ToProto(value), EqualsProto(proto));
}

TEST(PartPropertyValueProtoConversion, DoubleRoundtrip) {
  ::intrinsic_proto::icon::v1::PartPropertyValue proto;
  proto.set_double_value(23.643);
  ASSERT_OK_AND_ASSIGN(PartPropertyValue value, FromProto(proto));
  EXPECT_THAT(ToProto(value), EqualsProto(proto));
}

}  // namespace
}  // namespace intrinsic::icon
