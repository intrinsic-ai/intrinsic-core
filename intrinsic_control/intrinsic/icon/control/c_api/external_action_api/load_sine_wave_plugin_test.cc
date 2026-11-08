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


#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/duration.pb.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/control/action_factory_context.h"
#include "intrinsic/icon/control/actions/test_helpers.h"
#include "intrinsic/icon/control/c_api/external_action_api/sine_wave_action.pb.h"
#include "intrinsic/icon/control/c_api/external_action_api/sine_wave_plugin_action.h"
#include "intrinsic/icon/control/fake_realtime_slot_map_impl.h"
#include "intrinsic/icon/control/joint_position_command.h"
#include "intrinsic/icon/control/parts/fake_feature_interfaces.h"
#include "intrinsic/icon/control/parts/realtime_part_interface.h"
#include "intrinsic/icon/control/parts/testing/position_loopback_fake_part.h"
#include "intrinsic/icon/control/plugin_action.h"
#include "intrinsic/icon/control/realtime_signal_access.h"
#include "intrinsic/icon/control/realtime_signal_storage.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/rtcl_action.h"
#include "intrinsic/icon/control/rtcl_action_factory_registry.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/icon/control/streaming_io_helpers.h"
#include "intrinsic/icon/control/streaming_io_realtime.h"
#include "intrinsic/icon/control/streaming_io_storage.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/custom_action_plugin_loader.h"
#include "intrinsic/icon/testing/malloc_test.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_state.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/proto_time.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::Each;
using ::testing::Pointwise;
using ::testing::SizeIs;

constexpr absl::string_view kServerName = "icon_server";
constexpr absl::string_view kPartName = "part";
constexpr double kFrequency = 1.0;
constexpr size_t kNdof = 3;
constexpr double kFrequencyHz = 1.0;
constexpr double kMaxPos = 2.0;
constexpr ActionInstanceId kActionInstanceId(91);
constexpr RealtimeSlotId kSlotId(0);

intrinsic_proto::icon::v1::ServerConfig GetServerConfig() {
  intrinsic_proto::icon::v1::ServerConfig config;
  config.set_name(kServerName);
  config.set_frequency_hz(kFrequency);
  return config;
}

SlotInfo SlotInfoFromPart(const RealtimePartInterface& part) {
  return {.config = PartConfigFromPart(kPartName, part), .slot_id = kSlotId};
}

SignalStorageAndIdMap GetSignalStorageAndIdMap() {
  auto signal_storage_and_id_map =
      CreateRealtimeSignalStorage(SineWavePluginAction::GetSignature());
  CHECK_OK(signal_storage_and_id_map.status());
  return signal_storage_and_id_map.value();
}

TEST(LoadSineWavePlugin, LoadPluginFile) {
  GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();
  const std::string plugin_path = PathResolver::ResolveRunfilesPathForTest(
      "intrinsic_control/intrinsic/icon/control/c_api/"
      "external_action_api/");
  LOG(INFO) << "Loading plugins from " << plugin_path;
  ASSERT_OK_AND_ASSIGN(CustomActionPluginLoader loader,
                       CustomActionPluginLoader::LoadPlugins(
                           plugin_path, RegisterActionFromPlugin));
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::icon::v1::ActionSignature signature,
                       GetGlobalRtclActionFactoryRegistry().GetSignature(
                           SineWavePluginAction::kName));
  LOG(INFO) << signature;
}

class SineWaveActionTest : public ::testing::Test {
 public:
  SineWaveActionTest()
      : loopback_part_(
            kNdof, /*default_limits=*/
            CreateSimpleJointLimits(kNdof, /*max_position=*/kMaxPos,
                                    /*max_velocity=*/100,
                                    /*max_acceleration=*/1000,
                                    /*max_jerk=*/1000),
            /*maximum_limits=*/JointLimits::Unlimited(kNdof).value()),
        fake_slot_map_(
            {{kSlotId, std::ref(loopback_part_.GetFeatureInterfaces())}}),
        slot_map_(fake_slot_map_),
        io_storage_(kActionInstanceId, SineWavePluginAction::GetSignature()),
        signal_storage_and_map_(GetSignalStorageAndIdMap()),
        context_(GetServerConfig(), SineWavePluginAction::GetSignature(),
                 /*slot_name_to_slot_info=*/
                 {{std::string(SineWavePluginAction::kSlotName),
                   SlotInfoFromPart(loopback_part_)}},
                 signal_storage_and_map_.signal_id_map, io_storage_,
                 kActionInstanceId, trajectory_map_) {}

  void SetUp() override {
    GetGlobalRtclActionFactoryRegistry().ClearForTestingOnly();
    const std::string plugin_path = PathResolver::ResolveRunfilesPathForTest(
        "intrinsic_control/intrinsic/icon/control/c_api/"
        "external_action_api/");
    LOG(INFO) << "Loading plugins from " << plugin_path;
    // Save the plugin loader until the end of the test.
    ASSERT_OK_AND_ASSIGN(loader_, CustomActionPluginLoader::LoadPlugins(
                                      plugin_path, RegisterActionFromPlugin));
    EXPECT_OK(GetGlobalRtclActionFactoryRegistry()
                  .GetSignature(SineWavePluginAction::kName)
                  .status());
  }

  absl::StatusOr<std::unique_ptr<RtclActionInterface>> CreateActionWithParams(
      const SineWavePluginAction::ParameterProto& params) {
    google::protobuf::Any params_any;
    if (!params_any.PackFrom(params)) {
      return absl::InternalError("Failed to pack params");
    }
    return GetGlobalRtclActionFactoryRegistry().CallFactory(
        SineWavePluginAction::kName, params_any, context_);
  }

  // Emulates one control step of the part and action->
  RealtimeStatus TickAndVerify(RtclActionInterface& action) {
    RealtimeStreamingIoStorage rt_io_storage =
        FromStreamingIoStorage(io_storage_);
    StreamingIoRealtimeAccess rt_io_access(intrinsic::Clock::FromNSec(12354),
                                           rt_io_storage);
    RealtimeSignalAccess signal_access(signal_storage_and_map_.signal_storage);

    IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
    INTRINSIC_RT_EXPECT_OK(action.Sense({
        slot_map_,
        rt_io_access,
        signal_access,
    }));
    INTRINSIC_RT_EXPECT_OK(action.Control({slot_map_}));
    IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();
    return loopback_part_.ApplyCommandWithoutParamsTestOnly();
  }

  RealtimeStatus SetPosition(const eigenmath::VectorNd& joints) {
    INTRINSIC_RT_RETURN_IF_ERROR(
        loopback_part_.GetInterface<FakeJointPosition>().SetPositionSetpoints(
            JointPositionCommand(joints)));
    INTRINSIC_RT_RETURN_IF_ERROR(
        loopback_part_.ReadStatusWithoutParamsTestOnly());
    return loopback_part_.ApplyCommandWithoutParamsTestOnly();
  }

  JointPositionCommand PreviousPositionSetpoints() {
    return loopback_part_.JointPositionInterface().PreviousPositionSetpoints();
  }

 protected:
  // The other members may depend on functions loaded from a plugin, so the
  // loader must go first, to ensure that it is destroyed last.
  CustomActionPluginLoader loader_;
  PositionLoopbackFakePart loopback_part_;
  FakeRealtimeSlotMapImpl fake_slot_map_;
  RealtimeSlotMap slot_map_;
  StreamingIoStorage io_storage_;
  SignalStorageAndIdMap signal_storage_and_map_;
  absl::flat_hash_map<ActionInstanceId, JointTrajectoryPVA> trajectory_map_;
  ActionFactoryContext context_;
};

TEST_F(SineWaveActionTest, Create) {
  SineWavePluginAction::ParameterProto params;
  for (int i = 0; i < kNdof; ++i) {
    auto* joint = params.mutable_joints()->Add();
    joint->set_amplitude_rad(i);
    joint->set_frequency_hz(0.25);
  }
  ASSERT_OK_AND_ASSIGN(auto action, CreateActionWithParams(params));

  intrinsic_proto::icon::v1::ActionSignature signature =
      SineWavePluginAction::GetSignature();
  for (const intrinsic_proto::icon::v1::ActionSignature::StateVariableInfo&
           state_variable_info : signature.state_variable_infos()) {
    EXPECT_THAT(
        action->GetStateVariable(state_variable_info.state_variable_name()),
        AnyOf(RealtimeIsOk(),
              RealtimeStatusIs(absl::StatusCode::kUnavailable)));
  }
}

TEST_F(SineWaveActionTest, Works) {
  EXPECT_OK(SetPosition(eigenmath::VectorNd::Zero(kNdof)));

  SineWavePluginAction::ParameterProto params;
  for (int i = 0; i < kNdof; ++i) {
    auto* joint = params.mutable_joints()->Add();
    joint->set_amplitude_rad(i);
    joint->set_frequency_hz(0.25);
  }
  ASSERT_OK_AND_ASSIGN(auto action, CreateActionWithParams(params));

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(action->OnEnter({slot_map_}));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  // Action has not published a streaming output yet.
  EXPECT_THAT(PollStreamingOutput<google::protobuf::Duration>(
                  absl::Now() + absl::Milliseconds(20), io_storage_),
              StatusIs(absl::StatusCode::kDeadlineExceeded));

  // The initial sense/control step initializes the Action and zeroes the time.
  EXPECT_OK(TickAndVerify(*action));
  EXPECT_OK(PollStreamingOutput<google::protobuf::Duration>(
      absl::Now() + absl::Milliseconds(20), io_storage_));

  // Step once for each degree of freedom. Before the first step, the joints
  // should be at 0, and afterwards they should be at params->amplitudes[i],
  // then 0, then -params->amplitude[i], and so on.
  EXPECT_THAT(PreviousPositionSetpoints().position(), Each(DoubleEq(0.)));
  EXPECT_OK(TickAndVerify(*action));

  EXPECT_OK(PollStreamingOutput<google::protobuf::Duration>(
      absl::Now() + absl::Milliseconds(20), io_storage_));
  EXPECT_THAT(PreviousPositionSetpoints().position(),
              Pointwise(DoubleEq(), {0, 1, 2}));

  EXPECT_OK(TickAndVerify(*action));

  EXPECT_THAT(PreviousPositionSetpoints().position(),
              Each(DoubleNear(0., 1e-10)));

  EXPECT_OK(TickAndVerify(*action));

  EXPECT_THAT(PreviousPositionSetpoints().position(),
              Pointwise(DoubleEq(), {0, -1, -2}));

  EXPECT_OK(TickAndVerify(*action));

  EXPECT_THAT(PreviousPositionSetpoints().position(),
              Each(DoubleNear(0., 1e-10)));
}

TEST_F(SineWaveActionTest, MaintainsContinuity) {
  JointStateP jstate;
  ASSERT_OK(jstate.SetSize(kNdof));
  jstate.position.setConstant(kMaxPos * 0.5);
  EXPECT_OK(SetPosition(jstate.position));

  SineWavePluginAction::ParameterProto params;
  for (int i = 0; i < kNdof; ++i) {
    auto* joint = params.mutable_joints()->Add();
    joint->set_amplitude_rad(i);
    joint->set_frequency_hz(0.25);
  }
  ASSERT_OK_AND_ASSIGN(auto action, CreateActionWithParams(params));

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(action->OnEnter({slot_map_}));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  EXPECT_OK(TickAndVerify(*action));

  const JointPositionCommand actual_commanded_position =
      PreviousPositionSetpoints();
  // Expect that the actual commanded position is the same as the previous
  // position command.
  EXPECT_THAT(actual_commanded_position.position().head(kNdof),
              Pointwise(DoubleEq(), jstate.position));
}

TEST_F(SineWaveActionTest, ClampsToPositionLimits) {
  SineWavePluginAction::ParameterProto params;
  for (size_t i = 0; i < kNdof; ++i) {
    ::intrinsic_proto::icon::external_actions::SineWaveFixedParams::
        JointSineParams* joint = params.add_joints();
    joint->set_amplitude_rad(2 * kMaxPos);
    joint->set_frequency_hz(0.25);
  }
  ASSERT_OK_AND_ASSIGN(auto action, CreateActionWithParams(params));

  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(action->OnEnter({slot_map_}));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  int command_ticks = 0;

  // The initial sense/control step initializes the Action and zeroes the time.
  EXPECT_OK(TickAndVerify(*action));
  command_ticks++;
  ASSERT_THAT(PreviousPositionSetpoints().position(),
              AllOf(SizeIs(kNdof), Each(DoubleEq(0.))));

  // After another step, the sine wave reaches the maximum amplitude of 2 *
  // kMaxPos, but the Action should clamp that to kMaxPos.
  EXPECT_OK(TickAndVerify(*action));
  command_ticks++;

  ASSERT_THAT(PreviousPositionSetpoints().position(),
              AllOf(SizeIs(kNdof), Each(DoubleEq(kMaxPos))));

  // Another step takes us back to 0...
  EXPECT_OK(TickAndVerify(*action));
  command_ticks++;

  ASSERT_THAT(PreviousPositionSetpoints().position(),
              AllOf(SizeIs(kNdof), Each(DoubleNear(0., 1e-10))));

  // And yet another to the negative peak, which is clamped to negative kMaxPos.
  EXPECT_OK(TickAndVerify(*action));
  command_ticks++;

  ASSERT_THAT(PreviousPositionSetpoints().position(),
              AllOf(SizeIs(kNdof), Each(DoubleEq(-1 * kMaxPos))));

  // Run sense one more time to update state variables
  RealtimeStreamingIoStorage rt_io_storage =
      FromStreamingIoStorage(io_storage_);
  StreamingIoRealtimeAccess rt_io_access(intrinsic::Clock::FromNSec(12354),
                                         rt_io_storage);
  RealtimeSignalAccess signal_access(signal_storage_and_map_.signal_storage);
  IF_INTRINSIC_MALLOC_TEST_INIT_COUNTER();
  INTRINSIC_RT_EXPECT_OK(
      action->Sense({slot_map_, rt_io_access, signal_access}));
  IF_INTRINSIC_MALLOC_TEST_EXPECT_NO_ALLOCATIONS();

  ASSERT_OK_AND_ASSIGN(auto time_since_action_start,
                       PollStreamingOutput<google::protobuf::Duration>(
                           absl::Now() + absl::Milliseconds(20), io_storage_));
  ASSERT_OK_AND_ASSIGN(absl::Duration duration,
                       ToAbslDuration(time_since_action_start));
  EXPECT_EQ(duration, absl::Seconds(command_ticks / kFrequencyHz));
}
}  // namespace
}  // namespace intrinsic::icon
