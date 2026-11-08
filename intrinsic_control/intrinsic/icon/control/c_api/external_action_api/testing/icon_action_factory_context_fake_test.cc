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

#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_action_factory_context_fake.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_action_factory_context.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_realtime_signal_access_and_map_fake.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_slot_map_fake.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_streaming_io_registry_fake.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

constexpr char kSlotName[] = "robot";
constexpr char kStreamingInputName[] = "input1";
constexpr char kRealtimeSignalName[] = "signal1";
constexpr char kActionTypeName[] = "dummy_action";

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;

using FixedParameterProto = ::google::protobuf::FloatValue;
using StreamingInputProto = ::google::protobuf::Int64Value;
using StreamingOutputProto = ::google::protobuf::Int64Value;

::intrinsic_proto::icon::v1::ActionSignature GetActionSignature() {
  ActionSignatureBuilder b(kActionTypeName, "test action with streaming IOs");
  (void)b.SetFixedParametersType<FixedParameterProto>();
  (void)b.AddStreamingInput<StreamingInputProto>(kStreamingInputName, "");
  (void)b.AddStreamingOutput<StreamingOutputProto>("streaming_output", "");
  (void)b.AddRealtimeSignal(kRealtimeSignalName, "");
  return b.Finish();
}

TEST(IconActionFactoryContextFake, ServerConfig) {
  intrinsic_proto::icon::v1::ServerConfig config;
  config.set_name("beep_boop");
  config.set_frequency_hz(123);

  IconSlotMapFake slot_map;
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  IconRealtimeSignalAccessAndMapFake realtime_signal_access_and_map(
      GetActionSignature());
  IconActionFactoryContextFake fake(
      config, /*slot_map=*/slot_map,
      /*streaming_io_registry=*/io_registry,
      /*realtime_signal_access_and_map=*/realtime_signal_access_and_map);

  IconActionFactoryContext context = fake.MakeIconActionFactoryContext();

  EXPECT_THAT(context.ServerConfig(), EqualsProto(config));
}

TEST(IconActionFactoryContextFake, GetSlotInfo) {
  intrinsic_proto::icon::v1::ServerConfig config;

  IconSlotMapFake slot_map;
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  IconRealtimeSignalAccessAndMapFake realtime_signal_access_and_map(
      GetActionSignature());
  EXPECT_OK(slot_map.AddLoopbackFakeArmSlot(kSlotName));
  ASSERT_OK_AND_ASSIGN(SlotInfo info_expected,
                       slot_map.GetSlotInfoForSlot(kSlotName));
  IconActionFactoryContextFake fake(
      config, /*slot_map=*/slot_map,
      /*streaming_io_registry=*/io_registry,
      /*realtime_signal_access_and_map=*/realtime_signal_access_and_map);

  IconActionFactoryContext context = fake.MakeIconActionFactoryContext();

  EXPECT_THAT(context.GetSlotInfo(absl::StrCat("not_", kSlotName)),
              StatusIs(absl::StatusCode::kNotFound));

  ASSERT_OK_AND_ASSIGN(SlotInfo info, context.GetSlotInfo(kSlotName));
  EXPECT_EQ(info.slot_id, info_expected.slot_id);
  EXPECT_THAT(info.config, EqualsProto(info_expected.config));
}

TEST(IconActionFactoryContextFake, GetRealtimeSignalId) {
  intrinsic_proto::icon::v1::ServerConfig config;

  IconSlotMapFake slot_map;
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  IconRealtimeSignalAccessAndMapFake realtime_signal_access_and_map(
      GetActionSignature());
  IconActionFactoryContextFake fake(
      config, /*slot_map=*/slot_map,
      /*streaming_io_registry=*/io_registry,
      /*realtime_signal_access_and_map=*/realtime_signal_access_and_map);

  IconActionFactoryContext context = fake.MakeIconActionFactoryContext();
  EXPECT_OK(context.GetRealtimeSignalId(kRealtimeSignalName).status());
  EXPECT_THAT(context.GetRealtimeSignalId("bad_signal"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(IconActionFactoryContextFake, AddStreamingOutputConverter) {
  IconSlotMapFake slot_map;
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  IconRealtimeSignalAccessAndMapFake realtime_signal_access_and_map(
      GetActionSignature());
  IconActionFactoryContextFake fake(
      /*server_config=*/{},
      /*slot_map=*/slot_map,
      /*streaming_io_registry=*/io_registry,
      /*realtime_signal_access_and_map=*/realtime_signal_access_and_map);

  IconActionFactoryContext context = fake.MakeIconActionFactoryContext();

  EXPECT_FALSE(io_registry.HasStreamingOutput());
  EXPECT_THAT(io_registry.InvokeOutputConverter<int>(1),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_OK((context.AddStreamingOutputConverter<int, StreamingOutputProto>(
      [](int value) -> absl::StatusOr<StreamingOutputProto> {
        StreamingOutputProto value_proto;
        value_proto.set_value(value);
        return value_proto;
      })));

  EXPECT_TRUE(io_registry.HasStreamingOutput());
  int expected_output = 895;
  ASSERT_OK_AND_ASSIGN(google::protobuf::Any output_any,
                       io_registry.InvokeOutputConverter<int>(expected_output));
  StreamingOutputProto output_proto;
  ASSERT_TRUE(output_any.UnpackTo(&output_proto));
  EXPECT_EQ(output_proto.value(), expected_output);

  EXPECT_THAT((context.AddStreamingOutputConverter<int, StreamingOutputProto>(
                  [](int value) -> absl::StatusOr<StreamingOutputProto> {
                    StreamingOutputProto value_proto;
                    value_proto.set_value(value);
                    return value_proto;
                  })),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(IconActionFactoryContextFake, AddStreamingInputConverter) {
  IconSlotMapFake slot_map;
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  IconRealtimeSignalAccessAndMapFake realtime_signal_access_and_map(
      GetActionSignature());
  IconActionFactoryContextFake fake(
      /*server_config=*/{},
      /*slot_map=*/slot_map,
      /*streaming_io_registry=*/io_registry,
      /*realtime_signal_access_and_map=*/realtime_signal_access_and_map);

  IconActionFactoryContext context = fake.MakeIconActionFactoryContext();

  EXPECT_FALSE(io_registry.HasStreamingInput(kStreamingInputName));
  EXPECT_THAT(io_registry.InvokeInputParser<int>(kStreamingInputName,
                                                 StreamingInputProto()),
              StatusIs(absl::StatusCode::kNotFound));

  ASSERT_OK_AND_ASSIGN(
      StreamingInputId id,
      (context.AddStreamingInputParser<StreamingInputProto, int>(
          kStreamingInputName,
          [](const StreamingInputProto& value_proto) -> absl::StatusOr<int> {
            return value_proto.value();
          })));

  EXPECT_TRUE(io_registry.HasStreamingInput(id));
  EXPECT_TRUE(io_registry.HasStreamingInput(kStreamingInputName));

  StreamingInputProto input_proto;
  input_proto.set_value(94);
  ASSERT_OK_AND_ASSIGN(int value, io_registry.InvokeInputParser<int>(
                                      kStreamingInputName, input_proto));
  EXPECT_EQ(value, input_proto.value());

  EXPECT_THAT(
      (context.AddStreamingInputParser<StreamingInputProto, int>(
          kStreamingInputName,
          [](const StreamingInputProto& value_proto) -> absl::StatusOr<int> {
            return value_proto.value();
          })),
      StatusIs(absl::StatusCode::kAlreadyExists));
}

}  // namespace
}  // namespace intrinsic::icon
