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

#include "intrinsic/icon/actions/action_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "internal/testing.h"
#include "intrinsic/icon/proto/cart_space.pb.h"
#include "intrinsic/icon/proto/joint_space.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic_proto::icon::v1::FeatureInterfaceTypes;
using ::protobuf_matchers::EqualsProto;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::Property;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAreArray;

constexpr absl::string_view kParamName = "cart_goal";
constexpr absl::string_view kParamDescription =
    "The cartesian position to move to.";

constexpr absl::string_view kOtherParamName = "joint_goal";
constexpr absl::string_view kOtherParamDescription =
    "The joint position to move to.";

TEST(ActionSignatureBuilder, NoParams) {
  ActionSignatureBuilder builder("intrinsic.test.noop_action", "Does nothing.");

  auto signature = builder.Finish();
  EXPECT_EQ(signature.action_type_name(), "intrinsic.test.noop_action");
  EXPECT_EQ(signature.text_description(), "Does nothing.");
  EXPECT_THAT(signature.state_variable_infos(), IsEmpty());
  EXPECT_THAT(signature.streaming_input_infos(), IsEmpty());
  EXPECT_FALSE(signature.has_streaming_output_info());
}

TEST(ActionSignatureBuilder, TwoOfEach) {
  const std::string kActionName = "intrinsic.test.everything_action";
  const std::string kActionDescription =
      "Has every kind of parameter, but still does nothing.";

  ActionSignatureBuilder builder(kActionName, kActionDescription);

  EXPECT_OK(builder.SetFixedParametersType<intrinsic_proto::icon::CartVec6>());
  // Having two of each kind of parameter checks that the builder correctly adds
  // parameters when there are no name collisions.
  EXPECT_OK(builder.AddStreamingInput<intrinsic_proto::icon::CartVec6>(
      kParamName, kParamDescription));
  EXPECT_OK(builder.AddStreamingInput<intrinsic_proto::icon::JointVec>(
      kOtherParamName, kOtherParamDescription));
  EXPECT_OK(builder.AddStreamingOutput<intrinsic_proto::icon::CartVec6>(
      kParamName, kParamDescription));
  EXPECT_THAT(builder.AddStreamingOutput<intrinsic_proto::icon::JointVec>(
                  kOtherParamName, kOtherParamDescription),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_OK(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          "cart_goal_distance", kParamDescription));
  EXPECT_OK(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          "joint_goal_distance", kOtherParamDescription));

  auto signature = builder.Finish();

  EXPECT_EQ(signature.action_type_name(), kActionName);
  EXPECT_EQ(signature.text_description(), kActionDescription);
  EXPECT_THAT(signature.streaming_input_infos(), SizeIs(2));
  EXPECT_TRUE(signature.has_streaming_output_info());
  EXPECT_EQ(signature.streaming_output_info().parameter_name(), kParamName);
  EXPECT_EQ(signature.streaming_output_info().text_description(),
            kParamDescription);
  EXPECT_THAT(signature.state_variable_infos(), SizeIs(2));
}

TEST(ActionSignatureBuilder, ClashingNames) {
  const std::string kActionName = "intrinsic.test.everything_action";
  const std::string kActionDescription =
      "Has every kind of parameter, but still does nothing.";

  ActionSignatureBuilder builder(kActionName, kActionDescription);

  EXPECT_OK(builder.SetFixedParametersType<intrinsic_proto::icon::CartVec6>());
  EXPECT_THAT(builder.SetFixedParametersType<intrinsic_proto::icon::CartVec6>(),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("Fixed parameters type already set")));

  EXPECT_OK(builder.AddStreamingInput<intrinsic_proto::icon::CartVec6>(
      kParamName, kParamDescription));
  EXPECT_THAT(builder.AddStreamingInput<intrinsic_proto::icon::CartVec6>(
                  kParamName, kParamDescription),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("Duplicate streaming input")));

  EXPECT_OK(builder.AddStreamingOutput<intrinsic_proto::icon::CartVec6>(
      kParamName, kParamDescription));
  EXPECT_THAT(builder.AddStreamingOutput<intrinsic_proto::icon::CartVec6>(
                  kParamName, kParamDescription),
              StatusIs(absl::StatusCode::kAlreadyExists,
                       HasSubstr("already has a streaming output")));

  EXPECT_OK(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          kParamName, kParamDescription));
  EXPECT_THAT(
      builder.AddStateVariable<intrinsic_proto::icon::v1::ActionSignature::
                                   StateVariableInfo::TYPE_DOUBLE>(
          kParamName, kParamDescription),
      StatusIs(absl::StatusCode::kAlreadyExists,
               HasSubstr("Duplicate state variable")));

  auto signature = builder.Finish();

  EXPECT_EQ(signature.action_type_name(), kActionName);
  EXPECT_EQ(signature.text_description(), kActionDescription);
  EXPECT_THAT(signature.streaming_input_infos(), SizeIs(1));
  EXPECT_TRUE(signature.has_streaming_output_info());
  EXPECT_THAT(signature.state_variable_infos(), SizeIs(1));
}

TEST(ActionSignatureBuilder, AddPartSlot) {
  const std::string kActionName = "intrinsic.test.everything_action";
  const std::string kActionDescription =
      "Has every kind of parameter, but still does nothing.";
  const std::string kSlotName = "first_slot";
  const std::string kSlotDescription =
      "A slot. Please do not insert bread, that's a different slot.";

  ActionSignatureBuilder builder(kActionName, kActionDescription);
  absl::flat_hash_set<FeatureInterfaceTypes> required_feature_interfaces{
      FeatureInterfaceTypes::FEATURE_INTERFACE_JOINT_POSITION,
      FeatureInterfaceTypes::FEATURE_INTERFACE_ADIO,
  };
  EXPECT_OK(builder.AddPartSlot(kSlotName, kSlotDescription,
                                required_feature_interfaces));

  auto signature = builder.Finish();

  EXPECT_EQ(signature.action_type_name(), kActionName);
  EXPECT_EQ(signature.text_description(), kActionDescription);
  // Check that the signature has a part_slot_infos map that maps from...
  EXPECT_THAT(signature.part_slot_infos(),
              ElementsAre(Pair(
                  // The Slot name we set above...
                  kSlotName,
                  // To a PartSlotInfo proto that has the correct description
                  // and set of required FeatureInterfaces.
                  AllOf(Property(&intrinsic_proto::icon::v1::ActionSignature::
                                     PartSlotInfo::description,
                                 kSlotDescription),
                        Property(&intrinsic_proto::icon::v1::ActionSignature::
                                     PartSlotInfo::is_optional,
                                 false),
                        Property(&intrinsic_proto::icon::v1::ActionSignature::
                                     PartSlotInfo::required_feature_interfaces,
                                 UnorderedElementsAreArray(
                                     required_feature_interfaces))))));
}

TEST(ActionSignatureBuilder, AddOptionalPartSlot) {
  const std::string kActionName = "intrinsic.test.optional_slot_action";
  const std::string kActionDescription = "Has an optional part slot.";
  const std::string kSlotName = "optional_slot";
  const std::string kSlotDescription = "An optional arm slot.";

  ActionSignatureBuilder builder(kActionName, kActionDescription);
  const absl::flat_hash_set<FeatureInterfaceTypes> required_feature_interfaces{
      FeatureInterfaceTypes::FEATURE_INTERFACE_JOINT_POSITION,
      FeatureInterfaceTypes::FEATURE_INTERFACE_JOINT_LIMITS,
  };
  const absl::flat_hash_set<FeatureInterfaceTypes> optional_feature_interfaces{
      FeatureInterfaceTypes::FEATURE_INTERFACE_ADIO,
  };
  EXPECT_OK(builder.AddOptionalPartSlot(kSlotName, kSlotDescription,
                                        required_feature_interfaces,
                                        optional_feature_interfaces));

  const auto signature = builder.Finish();

  EXPECT_THAT(
      signature.part_slot_infos(),
      ElementsAre(Pair(
          kSlotName,
          AllOf(
              Property(&intrinsic_proto::icon::v1::ActionSignature::
                           PartSlotInfo::description,
                       kSlotDescription),
              Property(&intrinsic_proto::icon::v1::ActionSignature::
                           PartSlotInfo::is_optional,
                       true),
              Property(&intrinsic_proto::icon::v1::ActionSignature::
                           PartSlotInfo::required_feature_interfaces,
                       UnorderedElementsAreArray(required_feature_interfaces)),
              Property(
                  &intrinsic_proto::icon::v1::ActionSignature::PartSlotInfo::
                      optional_feature_interfaces,
                  UnorderedElementsAreArray(optional_feature_interfaces))))));
}

TEST(ActionSignatureBuilder,
     AddPartSlotWithEmptyRequiredInterfacesIsNotOptional) {
  ActionSignatureBuilder builder("intrinsic.test.empty_required_slot_action",
                                 "AddPartSlot always creates a required slot.");
  const absl::flat_hash_set<FeatureInterfaceTypes> optional_feature_interfaces{
      FeatureInterfaceTypes::FEATURE_INTERFACE_ADIO,
  };
  EXPECT_OK(builder.AddPartSlot(
      "required_despite_empty_interfaces", "Required slot.",
      /*required_feature_interfaces=*/{}, optional_feature_interfaces));

  const auto signature = builder.Finish();
  EXPECT_FALSE(signature.part_slot_infos()
                   .at("required_despite_empty_interfaces")
                   .is_optional());
}

TEST(ActionSignatureBuilder, AddDuplicatePartSlotFails) {
  const std::string kActionName = "intrinsic.test.duplicate_slot_action";
  const std::string kActionDescription = "Am I repeating myself?";
  const std::string kSlotName = "first_slot";
  const std::string kSlotDescription =
      "A slot. Please do not insert bread, that's a different slot.";

  ActionSignatureBuilder builder(kActionName, kActionDescription);
  absl::flat_hash_set<FeatureInterfaceTypes> required_feature_interfaces{
      FeatureInterfaceTypes::FEATURE_INTERFACE_JOINT_POSITION,
      FeatureInterfaceTypes::FEATURE_INTERFACE_ADIO,
  };
  EXPECT_OK(builder.AddPartSlot(kSlotName, kSlotDescription,
                                required_feature_interfaces));
  EXPECT_THAT(builder.AddPartSlot(kSlotName, kSlotDescription,
                                  required_feature_interfaces),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(builder.AddOptionalPartSlot(kSlotName, kSlotDescription,
                                          required_feature_interfaces),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST(ActionSignatureBuilder,
     AddFeatureInterfaceIsBothRequiredAndOptionalFails) {
  const std::string kActionName =
      "intrinsic.test.invalid_feature_interfaces_action";
  const std::string kActionDescription =
      "Is it optional, or is it required? Make up your mind!";
  const std::string kSlotName = "first_slot";
  const std::string kSlotDescription =
      "A slot. Please to not insert bread, that's a different slot.";

  ActionSignatureBuilder builder(kActionName, kActionDescription);
  absl::flat_hash_set<FeatureInterfaceTypes> required_feature_interfaces{
      FeatureInterfaceTypes::FEATURE_INTERFACE_JOINT_POSITION,
      FeatureInterfaceTypes::FEATURE_INTERFACE_ADIO,
  };
  absl::flat_hash_set<FeatureInterfaceTypes> optional_feature_interfaces{
      FeatureInterfaceTypes::FEATURE_INTERFACE_JOINT_POSITION,
  };
  EXPECT_THAT(
      builder.AddPartSlot(kSlotName, kSlotDescription,
                          required_feature_interfaces,
                          optional_feature_interfaces),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr(intrinsic_proto::icon::v1::FeatureInterfaceTypes_Name(
                   FeatureInterfaceTypes::FEATURE_INTERFACE_JOINT_POSITION))));
}

TEST(ActionSignatureBuilder, AddRealtimeSignal) {
  const std::string kActionName = "intrinsic.test.realtime_signal_receiver";
  const std::string kActionDescription =
      "Has a realtime signal, but still does nothing.";
  const std::string kSignalName = "beep";
  const std::string kSignalDescription = "Beep Boop!";

  ActionSignatureBuilder builder(kActionName, kActionDescription);
  EXPECT_OK(builder.AddRealtimeSignal(kSignalName, kSignalDescription));

  auto signature = builder.Finish();

  EXPECT_EQ(signature.action_type_name(), kActionName);
  EXPECT_EQ(signature.text_description(), kActionDescription);
  // Check that the signature has a part_slot_infos map that maps from...
  EXPECT_THAT(signature.realtime_signal_infos(),
              ElementsAre(EqualsProto(
                  absl::Substitute(R"pb(
                                     signal_name: "$0" text_description: "$1"
                                   )pb",
                                   kSignalName, kSignalDescription))));
}

TEST(ActionSignatureBuilder, AddRealtimeSignalFailsForDuplicateSignal) {
  ActionSignatureBuilder builder(
      "intrinsic.test.duplicate_signals",
      "Oh no, I tried to register the same signal twice!");
  EXPECT_OK(builder.AddRealtimeSignal("beep", "Beep Boop!"));
  EXPECT_THAT(builder.AddRealtimeSignal("beep", "Boop Beep!"),
              StatusIs(absl::StatusCode::kAlreadyExists, HasSubstr("beep")));
}

TEST(ActionSignatureBuilder, AddSupportedBehaviorOverride) {
  ActionSignatureBuilder builder("intrinsic.test.capabilities", "something");
  EXPECT_OK(builder.AddSupportedBehaviorOverride(
      intrinsic_proto::icon::v1::BehaviorOverrideRequest::
          BEHAVIOR_OVERRIDE_REQUEST_UNKNOWN,
      "Unknown override description."));

  EXPECT_OK(builder.AddSupportedBehaviorOverride(
      intrinsic_proto::icon::v1::BehaviorOverrideRequest::
          BEHAVIOR_OVERRIDE_REQUEST_PAUSE,
      "Decelerates smoothly."));

  auto signature = builder.Finish();

  EXPECT_THAT(
      signature.behavior_override_infos(),
      ElementsAre(
          EqualsProto(
              R"pb(override_request: BEHAVIOR_OVERRIDE_REQUEST_UNKNOWN
                   text_description: "Unknown override description.")pb"),
          EqualsProto(
              R"pb(override_request: BEHAVIOR_OVERRIDE_REQUEST_PAUSE
                   text_description: "Decelerates smoothly.")pb")));
}

TEST(ActionSignatureBuilder,
     AddSupportedBehaviorOverridelFailsForDuplicateSignal) {
  ActionSignatureBuilder builder(
      "intrinsic.test.duplicate_capabilities",
      "Oh no, I tried to register the same capability twice!");
  EXPECT_OK(builder.AddSupportedBehaviorOverride(
      intrinsic_proto::icon::v1::BehaviorOverrideRequest::
          BEHAVIOR_OVERRIDE_REQUEST_PAUSE,
      "Pause description."));
  EXPECT_THAT(builder.AddSupportedBehaviorOverride(
                  intrinsic_proto::icon::v1::BehaviorOverrideRequest::
                      BEHAVIOR_OVERRIDE_REQUEST_PAUSE,
                  "Pause description."),
              StatusIs(absl::StatusCode::kAlreadyExists, HasSubstr("PAUSE")));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
