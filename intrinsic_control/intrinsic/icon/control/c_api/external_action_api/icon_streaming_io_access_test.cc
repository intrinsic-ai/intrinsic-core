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

#include "intrinsic/icon/control/c_api/external_action_api/icon_streaming_io_access.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <any>
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "google/protobuf/wrappers.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/c_api/c_action_factory_context.h"
#include "intrinsic/icon/control/c_api/c_streaming_io_realtime_access.h"
#include "intrinsic/icon/control/c_api/wrappers/action_factory_context_wrapper.h"
#include "intrinsic/icon/control/c_api/wrappers/streaming_io_realtime_access_wrapper.h"
#include "intrinsic/icon/control/streaming_io_helpers.h"
#include "intrinsic/icon/control/streaming_io_realtime.h"
#include "intrinsic/icon/control/streaming_io_registry.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/icon/utils/realtime_status_or.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/pose.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/util/status/status_macros.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic::icon {
namespace {
using absl_testing::StatusIs;
using ::protobuf_matchers::EquivToProto;

constexpr absl::string_view kActionTypeName = "streaming_io_action";
constexpr ActionInstanceId kActionInstanceId(42);
constexpr absl::string_view kStreamingInputNamePose = "desired_pose";
constexpr absl::string_view kStreamingOutputName = "output";

// Copies the bytes from `streaming_output` into a google::protobuf::BytesValue
// message.
absl::StatusOr<google::protobuf::BytesValue> ConvertRawBuffer(
    const WrappedCApiStreamingOutputRealtimeType& streaming_output) {
  google::protobuf::BytesValue value_proto;
  value_proto.set_value(
      absl::string_view(streaming_output.data(), streaming_output.size()));
  return value_proto;
}

// Converts `input_proto` to a Pose object, then moves that Pose into a
// shared_ptr (casting the pointer to IntrinsicIconStreamingInputType* in the
// process).
absl::StatusOr<WrappedCApiStreamingInputRealtimeType> ParseProtoToWrappedType(
    const intrinsic_proto::Pose& input_proto) {
  INTR_ASSIGN_OR_RETURN(Pose input, intrinsic_proto::FromProto(input_proto));
  return std::shared_ptr<IntrinsicIconStreamingInputType>(
      // This matches the way streaming inputs are wrapped in
      // intrinsic/icon/control/c_api/wrappers/streaming_io_wrapper.h
      reinterpret_cast<IntrinsicIconStreamingInputType*>(
          new std::any(std::move(input))),
      [](IntrinsicIconStreamingInputType* self) {
        delete reinterpret_cast<std::any*>(self);
      });
}

intrinsic_proto::icon::v1::ActionSignature TestSignature() {
  ActionSignatureBuilder b(
      kActionTypeName,
      "An Action that has one streaming input and one streaming output.");
  EXPECT_OK(b.AddStreamingInput<intrinsic_proto::Pose>(kStreamingInputNamePose,
                                                       "Some 6D Pose."));
  EXPECT_OK(b.AddStreamingOutput<google::protobuf::BytesValue>(
      kStreamingOutputName, "A number of bytes of unspecified provenance."));
  return b.Finish();
}

class TestIconStreamingIoAccess : public ::testing::Test {
 public:
  TestIconStreamingIoAccess() : storage_(kActionInstanceId, TestSignature()) {}

  void SetUp() override {
    StreamingIoRegistry registry(storage_);
    ASSERT_OK_AND_ASSIGN(
        input_id_,
        (registry.AddStreamingInputParser<
            intrinsic_proto::Pose, WrappedCApiStreamingInputRealtimeType>(
            kStreamingInputNamePose, ParseProtoToWrappedType)));
    ASSERT_OK((registry.AddStreamingOutputConverter<
               WrappedCApiStreamingOutputRealtimeType,
               google::protobuf::BytesValue>(&ConvertRawBuffer)));

    // Can't do this in the constructor because we need to add the InputParser
    // and OutputConverter before instantiating the RealtimeStreamingIoStorage
    // object.
    rt_storage_ = std::make_unique<RealtimeStreamingIoStorage>(
        FromStreamingIoStorage(storage_));
    rt_access_ = std::make_unique<StreamingIoRealtimeAccess>(
        ::intrinsic::Clock::Now(), *rt_storage_);
    access_wrapped_ = Wrap(rt_access_.get());
  }

  RealtimeStreamingIoStorage GetRealtimeStreamingIoStorage() {
    return FromStreamingIoStorage(storage_);
  }

 protected:
  StreamingIoStorage storage_;
  std::unique_ptr<RealtimeStreamingIoStorage> rt_storage_;

 private:
  std::unique_ptr<StreamingIoRealtimeAccess> rt_access_;

 protected:
  IntrinsicIconStreamingIoRealtimeAccess* access_wrapped_;
  StreamingInputId input_id_;
};

TEST_F(TestIconStreamingIoAccess, Init) {}

TEST_F(TestIconStreamingIoAccess, CtorDoesNotMalloc) {
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  IconStreamingIoAccess access(access_wrapped_,
                               GetStreamingIoRealtimeAccessVtable());
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
}

TEST_F(TestIconStreamingIoAccess, PollInputFailsWithInvalidId) {
  IconStreamingIoAccess access(access_wrapped_,
                               GetStreamingIoRealtimeAccessVtable());
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<const Pose*> input_pose =
      access.PollInput<Pose>(StreamingInputId(input_id_.value() + 1));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(input_pose.status(), StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(TestIconStreamingIoAccess,
       PollInputSucceedsButReturnsNullptrWithValidButUnsetInput) {
  IconStreamingIoAccess access(access_wrapped_,
                               GetStreamingIoRealtimeAccessVtable());
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<const Pose*> input_pose = access.PollInput<Pose>(input_id_);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_THAT(input_pose, RealtimeIsOkAndHolds(nullptr));
}

TEST_F(TestIconStreamingIoAccess, PollInputSucceeds) {
  IconStreamingIoAccess access(access_wrapped_,
                               GetStreamingIoRealtimeAccessVtable());
  intrinsic_proto::Pose pose_proto;
  pose_proto.mutable_orientation()->set_w(1.0);
  EXPECT_OK(WriteStreamingInput(kStreamingInputNamePose, pose_proto, storage_));

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatusOr<const Pose*> input_pose = access.PollInput<Pose>(input_id_);
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  ASSERT_OK(input_pose.status());
  ASSERT_NE(input_pose.value(), nullptr);
  EXPECT_THAT(ToProto(*input_pose.value()), EquivToProto(pose_proto));
}

TEST_F(TestIconStreamingIoAccess, WriteOutput) {
  IconStreamingIoAccess access(access_wrapped_,
                               GetStreamingIoRealtimeAccessVtable());
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  RealtimeStatus write_status =
      access.WriteOutput<std::array<char, 4>>({'o', 'm', 'g', 0});
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
  EXPECT_OK(write_status);
  ASSERT_OK_AND_ASSIGN(
      auto bytes_proto,
      PollStreamingOutput<google::protobuf::BytesValue>(absl::Now(), storage_));
  EXPECT_EQ(std::string(bytes_proto.value()).substr(0, 3), "omg");
  // Reading the same streaming output again returns the same value (the latest
  // streaming output is still "omg").
  ASSERT_OK_AND_ASSIGN(
      auto bytes_proto_2,
      PollStreamingOutput<google::protobuf::BytesValue>(absl::Now(), storage_));
  EXPECT_EQ(std::string(bytes_proto_2.value()).substr(0, 3), "omg");
}

}  // namespace
}  // namespace intrinsic::icon
