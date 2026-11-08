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

#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_streaming_io_registry_fake.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "internal/testing.h"
#include "intrinsic/icon/actions/action_utils.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_streaming_io_access.h"
#include "intrinsic/icon/control/c_api/wrappers/streaming_io_wrapper.h"
#include "intrinsic/icon/control/streaming_io_types.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"

namespace intrinsic::icon {
namespace {

constexpr char kStreamingInputName[] = "input1";
constexpr char kActionTypeName[] = "dummy_action";

using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::IsNull;
using ::testing::Pointee;

using FixedParameterProto = ::google::protobuf::FloatValue;
using StreamingInputProto = ::google::protobuf::Int64Value;
using StreamingOutputProto = ::google::protobuf::Int64Value;

::intrinsic_proto::icon::v1::ActionSignature GetActionSignature() {
  ActionSignatureBuilder b(kActionTypeName, "test action with streaming IOs");
  (void)b.SetFixedParametersType<FixedParameterProto>();
  (void)b.AddStreamingInput<StreamingInputProto>(kStreamingInputName, "");
  (void)b.AddStreamingOutput<StreamingOutputProto>("streaming_output", "");

  return b.Finish();
}

TEST(IconStreamingIoRegistryFake,
     AddStreamingOutputConverterfailsWithWrongTypeName) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  EXPECT_FALSE(io_registry.HasStreamingOutput());
  EXPECT_THAT(io_registry.InvokeOutputConverter<int>(1),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_THAT(
      (io_registry.AddOutputConverter(
          absl::StrCat("not_",
                       StreamingOutputProto::GetDescriptor()->full_name()),
          WrapStreamingOutputConverter<int, StreamingOutputProto>(
              [](int value) -> absl::StatusOr<StreamingOutputProto> {
                StreamingOutputProto value_proto;
                value_proto.set_value(value);
                return value_proto;
              }))),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("output converter returns")));
}

TEST(IconStreamingIoRegistryFake, AddStreamingOutputConverter) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  EXPECT_FALSE(io_registry.HasStreamingOutput());
  EXPECT_THAT(io_registry.InvokeOutputConverter<int>(1),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_OK((io_registry.AddOutputConverter(
      StreamingOutputProto::GetDescriptor()->full_name(),
      WrapStreamingOutputConverter<int, StreamingOutputProto>(
          [](int value) -> absl::StatusOr<StreamingOutputProto> {
            StreamingOutputProto value_proto;
            value_proto.set_value(value);
            return value_proto;
          }))));

  EXPECT_TRUE(io_registry.HasStreamingOutput());
  int expected_output = 895;
  ASSERT_OK_AND_ASSIGN(google::protobuf::Any output_any,
                       io_registry.InvokeOutputConverter<int>(expected_output));
  StreamingOutputProto output_proto;
  ASSERT_TRUE(output_any.UnpackTo(&output_proto));
  EXPECT_EQ(output_proto.value(), expected_output);

  EXPECT_THAT((io_registry.AddOutputConverter(
                  StreamingOutputProto::GetDescriptor()->full_name(),
                  WrapStreamingOutputConverter<int, StreamingOutputProto>(
                      [](int value) -> absl::StatusOr<StreamingOutputProto> {
                        StreamingOutputProto value_proto;
                        value_proto.set_value(value);
                        return value_proto;
                      }))),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(IconStreamingIoRegistryFake, OutputConverterReportsError) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  EXPECT_OK((io_registry.AddOutputConverter(
      StreamingOutputProto::GetDescriptor()->full_name(),
      WrapStreamingOutputConverter<int, StreamingOutputProto>(
          [](int value) -> absl::StatusOr<StreamingOutputProto> {
            return absl::InternalError("oh no.");
          }))));

  EXPECT_THAT(io_registry.InvokeOutputConverter<int>(42),
              StatusIs(absl::StatusCode::kInternal, "oh no."));
}

TEST(IconStreamingIoRegistryFake,
     AddStreamingInputConverterFailsWithWrongTypeName) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  EXPECT_FALSE(io_registry.HasStreamingInput(kStreamingInputName));
  EXPECT_THAT(io_registry.InvokeInputParser<int>(kStreamingInputName,
                                                 StreamingInputProto()),
              StatusIs(absl::StatusCode::kNotFound));

  EXPECT_THAT(
      (io_registry.AddInputParser(
          kStreamingInputName,
          absl::StrCat("not_",
                       StreamingInputProto::GetDescriptor()->full_name()),
          WrapStreamingInputParser<StreamingInputProto, int>(
              [](const StreamingInputProto& value_proto)
                  -> absl::StatusOr<int> { return value_proto.value(); }))),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("input parser takes")));
}

TEST(IconStreamingIoRegistryFake, AddStreamingInputConverter) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  EXPECT_FALSE(io_registry.HasStreamingInput(kStreamingInputName));
  EXPECT_THAT(io_registry.InvokeInputParser<int>(kStreamingInputName,
                                                 StreamingInputProto()),
              StatusIs(absl::StatusCode::kNotFound));

  ASSERT_OK_AND_ASSIGN(
      StreamingInputId id,
      (io_registry.AddInputParser(
          kStreamingInputName,
          StreamingInputProto::GetDescriptor()->full_name(),
          WrapStreamingInputParser<StreamingInputProto, int>(
              [](const StreamingInputProto& value_proto)
                  -> absl::StatusOr<int> { return value_proto.value(); }))));

  EXPECT_TRUE(io_registry.HasStreamingInput(id));
  EXPECT_TRUE(io_registry.HasStreamingInput(kStreamingInputName));

  StreamingInputProto input_proto;
  input_proto.set_value(94);
  ASSERT_OK_AND_ASSIGN(int value, io_registry.InvokeInputParser<int>(
                                      kStreamingInputName, input_proto));
  EXPECT_EQ(value, input_proto.value());

  EXPECT_THAT(
      (io_registry.AddInputParser(
          kStreamingInputName,
          StreamingInputProto::GetDescriptor()->full_name(),
          WrapStreamingInputParser<StreamingInputProto, int>(
              [](const StreamingInputProto& value_proto)
                  -> absl::StatusOr<int> { return value_proto.value(); }))),
      StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(IconStreamingIoRegistryFake, StreamingInputConverterReportsError) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  ASSERT_OK((io_registry.AddInputParser(
      kStreamingInputName, StreamingInputProto::GetDescriptor()->full_name(),
      WrapStreamingInputParser<StreamingInputProto, int>(
          [](const StreamingInputProto& value_proto) -> absl::StatusOr<int> {
            return absl::InternalError("oh no.");
          }))));
  EXPECT_THAT(io_registry.InvokeInputParser<int>(kStreamingInputName,
                                                 StreamingInputProto()),
              StatusIs(absl::StatusCode::kInternal, "oh no."));
}

TEST(IconStreamingIoRegistryAsStreamingIoAccess, Create) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  io_registry.MakeIconStreamingIoAccess();
}

TEST(IconStreamingIoRegistryAsStreamingIoAccess, PollInputFailsWithNoInputs) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  IconStreamingIoAccess io_access = io_registry.MakeIconStreamingIoAccess();
  EXPECT_THAT(io_access.PollInput<int>(StreamingInputId(0)).status(),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST(IconStreamingIoRegistryAsStreamingIoAccess, PollInputReturnsNullptr) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  ASSERT_OK_AND_ASSIGN(StreamingInputId id,
                       (io_registry.AddInputParser(
                           kStreamingInputName,
                           StreamingInputProto::GetDescriptor()->full_name(),
                           WrapStreamingInputParser<StreamingInputProto, int>(
                               [](const StreamingInputProto& value_proto)
                                   -> absl::StatusOr<int> {
                                 return absl::InternalError("oh no.");
                               }))));

  IconStreamingIoAccess io_access = io_registry.MakeIconStreamingIoAccess();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(const int* input,
                                    io_access.PollInput<int>(id));
  EXPECT_THAT(input, IsNull());
}

TEST(IconStreamingIoRegistryAsStreamingIoAccess, PollInputSucceeds) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  ASSERT_OK_AND_ASSIGN(
      StreamingInputId id,
      (io_registry.AddInputParser(
          kStreamingInputName,
          StreamingInputProto::GetDescriptor()->full_name(),
          WrapStreamingInputParser<StreamingInputProto, int>(
              [](const StreamingInputProto& value_proto)
                  -> absl::StatusOr<int> { return value_proto.value(); }))));

  StreamingInputProto input_proto;
  input_proto.set_value(94);
  ASSERT_OK_AND_ASSIGN(
      int expected_value,
      io_registry.InvokeInputParser<int>(kStreamingInputName, input_proto));
  EXPECT_EQ(expected_value, input_proto.value());

  IconStreamingIoAccess io_access = io_registry.MakeIconStreamingIoAccess();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(const int* input,
                                    io_access.PollInput<int>(id));
  EXPECT_EQ(*input, expected_value);
}

TEST(IconStreamingIoRegistryAsStreamingIoAccess,
     InputPointerRemainsValidAfterSecondWrite) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());

  ASSERT_OK_AND_ASSIGN(
      StreamingInputId id,
      (io_registry.AddInputParser(
          kStreamingInputName,
          StreamingInputProto::GetDescriptor()->full_name(),
          WrapStreamingInputParser<StreamingInputProto, int>(
              [](const StreamingInputProto& value_proto)
                  -> absl::StatusOr<int> { return value_proto.value(); }))));

  StreamingInputProto input_proto;
  input_proto.set_value(94);
  ASSERT_OK_AND_ASSIGN(
      int first_expected_value,
      io_registry.InvokeInputParser<int>(kStreamingInputName, input_proto));
  EXPECT_EQ(first_expected_value, input_proto.value());

  IconStreamingIoAccess io_access = io_registry.MakeIconStreamingIoAccess();
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(const int* input,
                                    io_access.PollInput<int>(id));
  EXPECT_THAT(input, Pointee(first_expected_value));

  input_proto.set_value(input_proto.value() + 1);
  EXPECT_OK(
      io_registry.InvokeInputParser<int>(kStreamingInputName, input_proto));
  // The pointer we got from the previous invocation of PollInput() is still
  // valid, even though a second value has been written, and is still equal to
  // the *first* input value.
  EXPECT_THAT(input, Pointee(first_expected_value));

  // After calling PollInput for a second time, we get a different result (and
  // probably a different pointer, but that's an implementation detail, so don't
  // test for that).
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(input, io_access.PollInput<int>(id));
  EXPECT_THAT(input, Pointee(input_proto.value()));
}

TEST(IconStreamingIoRegistryAsStreamingIoAccess, WriteOutputFailsWithNoOutput) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  IconStreamingIoAccess io_access = io_registry.MakeIconStreamingIoAccess();
  EXPECT_THAT(io_access.WriteOutput<int>(290),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(IconStreamingIoRegistryAsStreamingIoAccess, WriteOutputSucceeds) {
  IconStreamingIoRegistryFake io_registry(GetActionSignature());
  EXPECT_OK((io_registry.AddOutputConverter(
      StreamingOutputProto::GetDescriptor()->full_name(),
      WrapStreamingOutputConverter<int, StreamingOutputProto>(
          [](int value) -> absl::StatusOr<StreamingOutputProto> {
            StreamingOutputProto value_proto;
            value_proto.set_value(value);
            return value_proto;
          }))));
  {
    // No output written yet, so no value in the std::optional.
    ASSERT_OK_AND_ASSIGN(std::optional<google::protobuf::Any> output_any,
                         io_registry.GetLatestOutput());
    EXPECT_FALSE(output_any.has_value());
  }
  IconStreamingIoAccess io_access = io_registry.MakeIconStreamingIoAccess();
  EXPECT_OK(io_access.WriteOutput<int>(290));

  ASSERT_OK_AND_ASSIGN(std::optional<google::protobuf::Any> output_any,
                       io_registry.GetLatestOutput());
  ASSERT_TRUE(output_any.has_value());
  StreamingOutputProto output_proto;
  ASSERT_TRUE(output_any.value().UnpackTo(&output_proto));
  EXPECT_EQ(output_proto.value(), 290);
}

}  // namespace
}  // namespace intrinsic::icon
