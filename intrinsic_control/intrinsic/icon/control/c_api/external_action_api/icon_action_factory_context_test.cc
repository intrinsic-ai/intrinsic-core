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

#include "intrinsic/icon/control/c_api/external_action_api/icon_action_factory_context.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <any>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/c_api/c_action_factory_context.h"
#include "intrinsic/icon/control/c_api/wrappers/action_factory_context_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/streaming_io_wrapper.h"
#include "intrinsic/icon/control/realtime_signal_types.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::testing::HasSubstr;
using ::testing::Pointee;

using StreamingInputProto = ::google::protobuf::Int64Value;
using StreamingInputRealtimeType = int64_t;
using StreamingOutputRealtimeType = double;
using StreamingOutputProto = ::google::protobuf::DoubleValue;

constexpr char kStreamingInputName[] = "roast_modifier";
constexpr char kServerName[] = "icon_server";
constexpr double kFrequencyHz = 1000.0;
constexpr size_t kNdof = 6;

constexpr char kActionTypeName[] = "toast_action";
constexpr ActionInstanceId kActionInstanceId(123);
constexpr char kSlotName[] = "toaster";
constexpr char kPartName[] = "fancy_toaster";
constexpr RealtimeSlotId kSlotId(0);

constexpr char kSignalName[] = "beep";
constexpr RealtimeSignalId kSignalId(938);

::intrinsic_proto::icon::v1::ServerConfig GetServerConfig() {
  ::intrinsic_proto::icon::v1::ServerConfig config;
  config.set_frequency_hz(kFrequencyHz);
  config.set_name(kServerName);
  return config;
}

::intrinsic_proto::icon::v1::ActionSignature GetActionSignature() {
  ActionSignatureBuilder b(kActionTypeName, "");
  CHECK_OK(b.AddPartSlot(
      kSlotName, "Used to crispify bread, or sometimes bagels.",
      /*required_feature_interfaces=*/
      {::intrinsic_proto::icon::v1::FEATURE_INTERFACE_JOINT_POSITION}));
  CHECK_OK(b.AddStreamingInput<StreamingInputProto>(kStreamingInputName, ""));
  CHECK_OK(b.AddStreamingOutput<StreamingOutputProto>("output", ""));
  CHECK_OK(b.AddRealtimeSignal(kSignalName, ""));
  return b.Finish();
}

::intrinsic_proto::icon::v1::PartConfig GetPartConfig() {
  ::intrinsic_proto::icon::v1::PartConfig config;
  config.set_name(kPartName);
  config.add_feature_interfaces(
      ::intrinsic_proto::icon::v1::FEATURE_INTERFACE_JOINT_POSITION);
  ::intrinsic_proto::icon::GenericPartConfig* generic_config =
      config.mutable_generic_config();
  generic_config->mutable_joint_position_config()->set_num_joints(kNdof);
  return config;
}

class IconActionFactoryContextTest : public ::testing::Test {
 public:
  IconActionFactoryContextTest()
      : streaming_io_storage_(ActionInstanceId(123), GetActionSignature()),
        slot_name_to_slot_info_({{kSlotName,
                                  SlotInfo{
                                      .config = GetPartConfig(),
                                      .slot_id = kSlotId,
                                  }}}),
        signal_name_to_signal_id_({{kSignalName, kSignalId}}),
        ctx_(GetServerConfig(), GetActionSignature(), slot_name_to_slot_info_,
             signal_name_to_signal_id_, streaming_io_storage_,
             kActionInstanceId, trajectory_map_),
        wrapped_context_(Wrap(&ctx_)),
        icon_context_(wrapped_context_, vtable_) {}

 protected:
  // Check streaming IO handler registration by accessing this.
  StreamingIoStorage streaming_io_storage_;

 private:
  absl::flat_hash_map<std::string, SlotInfo> slot_name_to_slot_info_;
  absl::flat_hash_map<std::string, RealtimeSignalId> signal_name_to_signal_id_;
  absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA> trajectory_map_;
  ActionFactoryContext ctx_;
  IntrinsicIconActionFactoryContextVtable vtable_ =
      GetActionFactoryContextVtable();
  IntrinsicIconActionFactoryContext* wrapped_context_;

 protected:
  // This is the main thing we test in this unit test.
  IconActionFactoryContext icon_context_;
};

TEST_F(IconActionFactoryContextTest, Create) {}

TEST_F(IconActionFactoryContextTest, ServerConfig) {
  EXPECT_THAT(icon_context_.ServerConfig(), EqualsProto(GetServerConfig()));
}

TEST_F(IconActionFactoryContextTest, GetSlotInfoFailsForUnknownSlot) {
  EXPECT_THAT(icon_context_.GetSlotInfo(absl::StrCat("not_", kSlotName)),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(IconActionFactoryContextTest, GetSlotInfoSucceeds) {
  ASSERT_OK_AND_ASSIGN(SlotInfo info, icon_context_.GetSlotInfo(kSlotName));
  EXPECT_EQ(info.slot_id, kSlotId);
  EXPECT_THAT(info.config, EqualsProto(GetPartConfig()));
}

TEST_F(IconActionFactoryContextTest, RegisterInputParserFailsWithInvalidName) {
  EXPECT_THAT(
      (icon_context_.AddStreamingInputParser<StreamingOutputProto, int>(
          absl::StrCat("not_", kStreamingInputName),
          [](const StreamingOutputProto& /*ignored*/) -> absl::StatusOr<int> {
            return absl::UnimplementedError("whoops");
          })),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("does not have a streaming input named")));
}

TEST_F(IconActionFactoryContextTest, RegisterInputParserFailsWithWrongType) {
  EXPECT_THAT((icon_context_.AddStreamingInputParser<StreamingOutputProto, int>(
                  kStreamingInputName,
                  [](const StreamingOutputProto& /*wrong_type*/)
                      -> absl::StatusOr<int> {
                    return absl::UnimplementedError("whoops");
                  })),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("type")));
}

TEST_F(IconActionFactoryContextTest, RegisterInputParserForwardsError) {
  ASSERT_OK_AND_ASSIGN(
      StreamingInputId id,
      (icon_context_.AddStreamingInputParser<StreamingInputProto,
                                             StreamingInputRealtimeType>(
          kStreamingInputName,
          [](const StreamingInputProto& /*ignored*/)
              -> absl::StatusOr<StreamingInputRealtimeType> {
            return absl::UnimplementedError("whoops");
          })));
  // Using inside knowledge here: StreamingIoStorage hands out StreamingInputIds
  // that are indices into its input_channels_ array, so we'll index into that.
  // But first, we must make sure that the access is valid first.
  ASSERT_GT(streaming_io_storage_.input_channels_.size(), id.value());
  EXPECT_EQ(streaming_io_storage_.input_channels_.at(id.value()).input_name,
            kStreamingInputName);
  // Invoke the parser with a proto of the correct type, and get back the error
  // from the lambda above.
  google::protobuf::Any params_any;
  ASSERT_TRUE(params_any.PackFrom(StreamingInputProto()));
  EXPECT_THAT(streaming_io_storage_.input_channels_.at(id.value())
                  .input_parser(params_any),
              StatusIs(absl::StatusCode::kUnimplemented, "whoops"));
}

TEST_F(IconActionFactoryContextTest, RegisterInputParserSucceeds) {
  ASSERT_OK_AND_ASSIGN(
      StreamingInputId id,
      (icon_context_.AddStreamingInputParser<StreamingInputProto,
                                             StreamingInputRealtimeType>(
          kStreamingInputName,
          [](const StreamingInputProto& input)
              -> absl::StatusOr<StreamingInputRealtimeType> {
            return input.value();
          })));
  // Using inside knowledge here: StreamingIoStorage hands out StreamingInputIds
  // that are indices into its input_channels_ array, so we'll index into that.
  // But first, we must make sure that the access is valid first.
  ASSERT_GT(streaming_io_storage_.input_channels_.size(), id.value());
  EXPECT_EQ(streaming_io_storage_.input_channels_.at(id.value()).input_name,
            kStreamingInputName);
  // Invoke the parser with a proto of the correct type, and get back the error
  // from the lambda above.
  google::protobuf::Any params_any;
  StreamingInputProto input_proto;
  input_proto.set_value(42);
  ASSERT_TRUE(params_any.PackFrom(input_proto));
  ASSERT_OK_AND_ASSIGN(std::any input_parsed,
                       streaming_io_storage_.input_channels_.at(id.value())
                           .input_parser(params_any));
  // Since the input parser is wrapped, return value of the lambda above is
  // changed into WrappedCApiStreamingInputRealtimeType.
  WrappedCApiStreamingInputRealtimeType* input_ptr =
      std::any_cast<WrappedCApiStreamingInputRealtimeType>(&input_parsed);
  ASSERT_NE(input_ptr, nullptr);
  EXPECT_THAT(
      UnwrapStreamingInput<StreamingInputRealtimeType>(input_ptr->get()),
      RealtimeIsOkAndHolds(Pointee(42)));
}

TEST_F(IconActionFactoryContextTest,
       RegisterOutputConverterFailsWithWrongType) {
  EXPECT_THAT(
      (icon_context_.AddStreamingOutputConverter<int, StreamingInputProto>(
          [](int /*ignored*/) -> absl::StatusOr<StreamingInputProto> {
            return absl::UnimplementedError("whoops");
          })),
      StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("type")));
}

TEST_F(IconActionFactoryContextTest, RegisterOutputConverterForwardsError) {
  ASSERT_OK(
      (icon_context_.AddStreamingOutputConverter<StreamingOutputRealtimeType,
                                                 StreamingOutputProto>(
          [](const StreamingOutputRealtimeType& /*ignored*/)
              -> absl::StatusOr<StreamingOutputProto> {
            return absl::UnimplementedError("whoops");
          })));
  ASSERT_NE(streaming_io_storage_.output_channel_, nullptr);
  // Invoke the converter with  the correct type, and get back the error from
  // the lambda above.
  // More inside knowledge here: Since the output converter is registered via
  // the C API, we need to use the corresponding wrapped signature, rather than
  // that of the original lambda above.
  StreamingOutputRealtimeType output = 1.5;
  WrappedCApiStreamingOutputRealtimeType wrapped_output;
  std::memcpy(wrapped_output.data(), &output, sizeof(output));
  EXPECT_THAT(streaming_io_storage_.output_channel_->output_converter_(
                  std::any(wrapped_output)),
              StatusIs(absl::StatusCode::kUnimplemented, HasSubstr("whoops")));
}

TEST_F(IconActionFactoryContextTest, RegisterOutputConverterSucceeds) {
  ASSERT_OK(
      (icon_context_.AddStreamingOutputConverter<StreamingOutputRealtimeType,
                                                 StreamingOutputProto>(
          [](const StreamingOutputRealtimeType& output)
              -> absl::StatusOr<StreamingOutputProto> {
            StreamingOutputProto output_proto;
            output_proto.set_value(output);
            return output_proto;
          })));
  ASSERT_NE(streaming_io_storage_.output_channel_, nullptr);
  // Invoke the converter with  the correct type, and get back the error from
  // the lambda above.
  // More inside knowledge here: Since the output converter is registered via
  // the C API, we need to use the corresponding wrapped signature, rather than
  // that of the original lambda above.
  StreamingOutputRealtimeType output = 1.5;
  WrappedCApiStreamingOutputRealtimeType wrapped_output;
  std::memcpy(wrapped_output.data(), &output, sizeof(output));
  ASSERT_OK(streaming_io_storage_.output_channel_->output_converter_(
      std::any(wrapped_output)));
}

}  // namespace
}  // namespace intrinsic::icon
