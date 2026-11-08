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

#include "intrinsic/icon/hal/hardware_module_manager.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/server_builder.h"
#include "internal/testing.h"
#include "intrinsic/icon/control/realtime_operational_status.h"
#include "intrinsic/icon/control/realtime_state_manager_interface.h"
#include "intrinsic/icon/hal/hardware_module_interface.h"
#include "intrinsic/icon/hal/hardware_module_proxy.h"
#include "intrinsic/icon/hal/hardware_module_registry.h"
#include "intrinsic/icon/hal/hardware_module_runtime.h"
#include "intrinsic/icon/hal/interfaces/hardware_module_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/io_controller.fbs.h"
#include "intrinsic/icon/hal/interfaces/joint_state.fbs.h"
#include "intrinsic/icon/hal/module_config.h"
#include "intrinsic/icon/hal/test/mock_hardware_module.h"
#include "intrinsic/icon/hal/test/test_hardware_module.h"
#include "intrinsic/icon/hal/test/test_hardware_module_config.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/shared_memory_manager.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/testing/unique_segment_name.h"
#include "intrinsic/icon/server/config/icon_main_config.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"

namespace intrinsic::icon {

using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::intrinsic::icon::test_hardware_module::TestHardwareModule;
using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing ::InSequence;
using ::testing::Lt;
using ::testing::Optional;
using ::testing::Pair;
using ::testing ::Return;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;

namespace {

constexpr absl::Duration kTransitionTimeout = absl::Seconds(10);
constexpr absl::string_view kStrictDioCommandInterfaceName =
    "strict_dio_command";
constexpr absl::string_view kModuleName = "my_test_hardware_module";

inline ModuleConfig GetModuleConfig(std::string_view memory_namespace,
                                    std::string_view name = kModuleName) {
  return ModuleConfig(
      ParseTextProtoOrDie(::test_hardware_module::GetTestHardwareModuleConfig(
          /*module_name=*/name)),
      memory_namespace,
      /*realtime_clock=*/nullptr);
}

inline ModuleConfig GetStrictModuleConfig(std::string_view memory_namespace,
                                          std::string_view name = kModuleName) {
  return ModuleConfig(
      ParseTextProtoOrDie(
          ::test_hardware_module::GetStrictTestHardwareModuleConfig(
              /*module_name=*/name,
              /*interface_prefix=*/
              "joint", /*strict_dio_command_interface_name=*/
              kStrictDioCommandInterfaceName)),
      memory_namespace,
      /*realtime_clock=*/nullptr);
}

inline HardwareModule GetModuleInstance(std::string_view memory_namespace,
                                        std::string_view name = kModuleName) {
  return hardware_module_registry::CreateInstance(
      GetModuleConfig(/*memory_namespace=*/memory_namespace, name),
      /*realtime_clock=*/nullptr);
}

inline HardwareModule GetStrictModuleInstance(
    std::string_view memory_namespace, std::string_view name = kModuleName) {
  return hardware_module_registry::CreateInstance(
      GetStrictModuleConfig(/*memory_namespace=*/memory_namespace,
                            /*name=*/name),
      /*realtime_clock=*/nullptr);
}

class HardwareModuleManagerTest : public ::testing::Test {
 public:
  HardwareModuleManagerTest() = default;

  void SetUp() override {
    std::string memory_namespace = UniqueMemoryNamespace();
    {
      ASSERT_OK_AND_ASSIGN(
          auto shm_manager,
          SharedMemoryManager::Create(memory_namespace, kModuleName));
      ASSERT_OK_AND_ASSIGN(runtime_,
                           HardwareModuleRuntime::Create(
                               std::move(shm_manager),
                               GetModuleInstance(
                                   /*memory_namespace=*/memory_namespace)));
    }

    hardware_module_ = dynamic_cast<TestHardwareModule*>(
        runtime_->GetHardwareModule().instance.get());
    ASSERT_NE(hardware_module_, nullptr);
    grpc::ServerBuilder builder;
    ASSERT_OK(runtime_->Run(builder));
    auto module_config = GetModuleConfig(/*memory_namespace=*/memory_namespace);
    ASSERT_OK_AND_ASSIGN(
        auto hardware_module_proxy,
        HardwareModuleProxy::Attach(module_config, absl::ZeroDuration()));
    ASSERT_TRUE(hardware_module_proxy.IsConnected());
    ASSERT_OK(manager_.Add(std::move(hardware_module_proxy),
                           /*proxy_drives_clock=*/false,
                           intrinsic_proto::icon::HardwareConfig{}));
    hwm_proxy_ = manager_.GetHardwareModuleProxy(kModuleName);
  }

 protected:
  absl_nonnull std::unique_ptr<HardwareModuleRuntime> runtime_;
  HardwareModuleManager manager_;
  TestHardwareModule* hardware_module_;
  const HardwareModuleProxy* hwm_proxy_;
};

TEST_F(HardwareModuleManagerTest, RequestTransitionTest) {
  ASSERT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  ASSERT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));
  ASSERT_OK(manager_.UpdateState());

  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      std::optional<HardwareModuleManager::CommandId> id,
      manager_.RequestTransition(
          RealtimeStateManagerInterface::TransitionRequest::kEnableMotion));
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(manager_.PendingTransitionRequest(*id),
            RealtimeStateManagerInterface::TransitionRequest::kEnableMotion);
  EXPECT_OK(manager_.ProcessTransitionRequestAsync());
  EXPECT_EQ(manager_.PendingTransitionRequest(*id), std::nullopt);
}

TEST_F(HardwareModuleManagerTest, RequestTransitionCommandIdChanges) {
  ASSERT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  ASSERT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));
  ASSERT_OK(manager_.UpdateState());
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto id,
      manager_.RequestTransition(
          RealtimeStateManagerInterface::TransitionRequest::kEnableMotion));
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(manager_.PendingTransitionRequest(*id),
            RealtimeStateManagerInterface::TransitionRequest::kEnableMotion);
  while (manager_.GetRequestState() !=
         RealtimeStateManagerInterface::RequestState::kDone) {
    ASSERT_OK(manager_.ProcessTransitionRequestAsync());
    ASSERT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
    ASSERT_OK(manager_.UpdateState());
  }

  EXPECT_OK(manager_.ProcessTransitionRequestAsync());
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto id2,
      manager_.RequestTransition(
          RealtimeStateManagerInterface::TransitionRequest::kDisableMotion));
  EXPECT_NE(id, id2);
}

TEST_F(HardwareModuleManagerTest, DirectEnableFails) {
  ASSERT_THAT(manager_.EnableMotionTestOnly(absl::Now() + kTransitionTimeout),
              StatusIs(absl::StatusCode::kUnavailable));
}

TEST_F(HardwareModuleManagerTest, FailingTransitionIsDetected) {
  hardware_module_->SetTransitionCallback([](intrinsic_fbs::TransitionRequest) {
    return InternalError("custom error");
  });
  // The internal error is not propagated. The manager only knows that the
  // desired state was not reached.
  ASSERT_THAT(
      manager_.EnableMotionTestOnly(absl::Now() + kTransitionTimeout),
      StatusIs(absl::StatusCode::kUnavailable, HasSubstr("is in state")));
}

TEST_F(HardwareModuleManagerTest, ForwardsFaultReason) {
  runtime_->SetStateTestOnly(intrinsic_fbs::StateCode::kFatallyFaulted,
                             "custom error");

  // The manager only knows that the
  // desired state was not reached. The error is read from the
  // hardware module.
  ASSERT_THAT(
      manager_.EnableMotionTestOnly(absl::Now() + kTransitionTimeout),
      StatusIs(absl::StatusCode::kUnavailable,
               AllOf(HasSubstr("is in state"), HasSubstr("custom error"))));
}

TEST_F(HardwareModuleManagerTest, TransitionTakesLongerThanDeadline) {
  hardware_module_->SetTransitionCallback([](intrinsic_fbs::TransitionRequest) {
    absl::SleepFor(absl::Seconds(10));
    return OkStatus();
  });
  ASSERT_THAT(
      manager_.Prepare(absl::Now() + absl::Seconds(2)),
      StatusIs(absl::StatusCode::kDeadlineExceeded,
               HasSubstr("while waiting for hardware modules to finish")));
}

TEST_F(HardwareModuleManagerTest, AlreadyPendingTransitionTest) {
  ASSERT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  ASSERT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));
  ASSERT_OK(manager_.UpdateState());

  EXPECT_OK(
      manager_
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());
  // It's fine to request the same transition twice while pending.
  EXPECT_OK(
      manager_
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());

  // A different transition cannot be requested while a different transition is
  // pending.
  EXPECT_THAT(
      manager_
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kDisableMotion)
          .status(),
      StatusIs(absl::StatusCode::kResourceExhausted));
}

TEST_F(HardwareModuleManagerTest, FailingTransitionTest) {
  const auto* hwm_proxy = manager_.GetHardwareModuleProxy(kModuleName);
  EXPECT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.UpdateState());

  // -- End of test setup --
  hardware_module_->SetFailingTransitions(absl::StatusCode::kInternal);

  EXPECT_NE(manager_.GetRequestState(),
            RealtimeStateManagerInterface::RequestState::kActive);
  // Request a transition and process it, wait until it's done, and check that
  // the state is faulted.
  EXPECT_OK(
      manager_
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());

  while (manager_.GetRequestState() !=
         RealtimeStateManagerInterface::RequestState::kDone) {
    EXPECT_OK(manager_.ProcessTransitionRequestAsync());
    EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
    EXPECT_OK(manager_.UpdateState());
    absl::SleepFor(absl::Milliseconds(100));
  }
  EXPECT_EQ(hwm_proxy->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kFaulted)
      << " Actual state: "
      << intrinsic_fbs::EnumNameStateCode(
             hwm_proxy->GetHardwareModuleState()->code());
}

TEST_F(HardwareModuleManagerTest,
       AbortedErrorLeadsToFatallyFaultedStateDuringActivate) {
  const auto* hwm_proxy = manager_.GetHardwareModuleProxy(kModuleName);
  EXPECT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  hardware_module_->SetTransitionCallback(
      [](intrinsic_fbs::TransitionRequest) { return AbortedError("aborted"); });
  EXPECT_THAT(manager_.Deactivate(absl::Now() + kTransitionTimeout),
              StatusIs(absl::StatusCode::kUnavailable));

  EXPECT_EQ(hwm_proxy->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kFatallyFaulted)
      << " Actual state: "
      << intrinsic_fbs::EnumNameStateCode(
             hwm_proxy->GetHardwareModuleState()->code());
}

TEST_F(HardwareModuleManagerTest,
       AbortedErrorLeadsToFatallyFaultedStateDuringDeactivate) {
  const auto* hwm_proxy = manager_.GetHardwareModuleProxy(kModuleName);
  hardware_module_->SetTransitionCallback(
      [](intrinsic_fbs::TransitionRequest) { return AbortedError("aborted"); });
  EXPECT_THAT(manager_.Prepare(absl::Now() + kTransitionTimeout),
              StatusIs(absl::StatusCode::kUnavailable));

  EXPECT_EQ(hwm_proxy->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kFatallyFaulted)
      << " Actual state: "
      << intrinsic_fbs::EnumNameStateCode(
             hwm_proxy->GetHardwareModuleState()->code());
}

const absl::StatusCode kNonFatalErrorCodesToTest[] = {
    absl::StatusCode::kCancelled,          absl::StatusCode::kUnknown,
    absl::StatusCode::kInvalidArgument,    absl::StatusCode::kDeadlineExceeded,
    absl::StatusCode::kNotFound,           absl::StatusCode::kAlreadyExists,
    absl::StatusCode::kPermissionDenied,   absl::StatusCode::kResourceExhausted,
    absl::StatusCode::kFailedPrecondition, absl::StatusCode::kOutOfRange,
    absl::StatusCode::kUnimplemented,      absl::StatusCode::kInternal,
    absl::StatusCode::kUnavailable,        absl::StatusCode::kDataLoss,
    absl::StatusCode::kUnauthenticated,
};

class HardwareModuleManagerParameterizedTest
    : public HardwareModuleManagerTest,
      public ::testing::WithParamInterface<absl::StatusCode> {};

TEST_P(HardwareModuleManagerParameterizedTest,
       NonAbortedErrorLeadsToFaultedState) {
  const auto errorCode = GetParam();
  const auto* hwm_proxy = manager_.GetHardwareModuleProxy(kModuleName);

  EXPECT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.UpdateState());

  hardware_module_->SetTransitionCallback(
      [errorCode](intrinsic_fbs::TransitionRequest) {
        return RealtimeStatus(errorCode, "");
      });
  intrinsic::Thread thread([&](intrinsic::StopToken stop) {
    while (!stop.stop_requested()) {
      EXPECT_OK(manager_.UpdateState());
      EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
      absl::SleepFor(absl::Milliseconds(100));
    }
  });

  EXPECT_THAT(manager_.EnableMotionTestOnly(absl::Now() + kTransitionTimeout),
              StatusIs(absl::StatusCode::kUnavailable));
  EXPECT_EQ(hwm_proxy->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kFaulted)
      << "ErrorCode: " << absl::StatusCodeToString(errorCode)
      << ", Actual state: "
      << intrinsic_fbs::EnumNameStateCode(
             hwm_proxy->GetHardwareModuleState()->code());
}

INSTANTIATE_TEST_SUITE_P(ErrorCodes, HardwareModuleManagerParameterizedTest,
                         ::testing::ValuesIn(kNonFatalErrorCodesToTest));

TEST_F(HardwareModuleManagerTest, TransitionInProgressTest) {
  EXPECT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.UpdateState());
  // -- End of test setup --
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      auto id,
      manager_.RequestTransition(
          RealtimeStateManagerInterface::TransitionRequest::kEnableMotion));

  hardware_module_->PauseTransitions(true);

  EXPECT_OK(manager_.ProcessTransitionRequestAsync());
  EXPECT_OK(manager_.UpdateState());
  EXPECT_EQ(manager_.GetRequestState(),
            RealtimeStateManagerInterface::RequestState::kActive);

  // A transition is already in progress, requesting a new one should fail.
  EXPECT_THAT(
      manager_
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status(),
      StatusIs(absl::StatusCode::kResourceExhausted));

  // The transition is ongoing, so the state can either be deactivated (the
  // hardware module did no receive the request yet) or motion-enabling.
  EXPECT_THAT(hwm_proxy_->GetHardwareModuleState()->code(),
              ::testing::AnyOf(intrinsic_fbs::StateCode::kActivated,
                               intrinsic_fbs::StateCode::kMotionEnabling))
      << "Actual state: "
      << intrinsic_fbs::EnumNameStateCode(
             hwm_proxy_->GetHardwareModuleState()->code());

  // Let the transition complete and check that the state was correctly updated.
  hardware_module_->PauseTransitions(false);
  auto deadline = absl::Now() + absl::Seconds(10);
  while (manager_.GetRequestState() ==
         RealtimeStateManagerInterface::RequestState::kActive) {
    EXPECT_OK(manager_.ProcessTransitionRequestAsync());
    EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
    EXPECT_OK(manager_.UpdateState());
    absl::SleepFor(absl::Microseconds(100));
    if (absl::Now() > deadline) {
      FAIL() << "Timed out waiting for transition request";
    }
  }

  EXPECT_EQ(hwm_proxy_->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kMotionEnabled);

  // Clean up any finished transactions so that a new one can be started.
  EXPECT_OK(manager_.ProcessTransitionRequestAsync());
  EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.UpdateState());
  // Confirm that a new transaction can be started.
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      id,
      manager_.RequestTransition(
          RealtimeStateManagerInterface::TransitionRequest::kDisableMotion));

  // Let the transition complete and check that the state was correctly updated.
  do {
    EXPECT_OK(manager_.ProcessTransitionRequestAsync());
    EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
    EXPECT_OK(manager_.UpdateState());
    absl::SleepFor(absl::Microseconds(100));
    ASSERT_THAT(absl::Now(), Lt(deadline))
        << "Timed out waiting for transition request";
  } while (manager_.GetRequestState() !=
           RealtimeStateManagerInterface::RequestState::kDone);
  EXPECT_EQ(hwm_proxy_->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kActivated);
}

TEST(HardwareModuleManagerTestNonFixture, FailingInitTest) {
  std::string memory_namespace = UniqueMemoryNamespace();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, kModuleName));
  ASSERT_OK_AND_ASSIGN(
      auto runtime,
      HardwareModuleRuntime::Create(
          std::move(shm_manager), GetModuleInstance(
                                      /*memory_namespace=*/memory_namespace)));
  auto hardware_module = dynamic_cast<TestHardwareModule*>(
      runtime->GetHardwareModule().instance.get());
  hardware_module->SetInitStatusOverride(absl::AbortedError("aborted"));
  grpc::ServerBuilder server_builder;
  EXPECT_THAT(runtime->Run(server_builder),
              StatusIs(absl::StatusCode::kAborted, "aborted"));
  auto module_config = GetModuleConfig(/*memory_namespace=*/memory_namespace);
  ASSERT_OK_AND_ASSIGN(auto hardware_module_proxy,
                       HardwareModuleProxy::Create(module_config));
  EXPECT_OK(hardware_module_proxy.Connect(absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy.IsConnected());
  absl::string_view proxy_name = hardware_module_proxy.Name();
  HardwareModuleManager manager;
  EXPECT_OK(manager.Add(std::move(hardware_module_proxy),
                        /*proxy_drives_clock=*/false,
                        intrinsic_proto::icon::HardwareConfig{}));
  const auto* hwm_proxy = manager.GetHardwareModuleProxy(proxy_name);

  // -- End of test setup --

  EXPECT_EQ(hwm_proxy->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kInitFailed);
}

TEST(HardwareModuleManagerMockTest, FunctionOrderTest) {
  std::string memory_namespace = UniqueMemoryNamespace();

  HardwareModule module;
  module.config = GetModuleConfig(/*memory_namespace=*/memory_namespace);
  module.instance = std::make_unique<MockHardwareModule>();
  auto hwm = dynamic_cast<MockHardwareModule*>(module.instance.get());
  ASSERT_NE(hwm, nullptr);
  {
    InSequence seq;
    EXPECT_CALL(*hwm, Init).WillOnce(Return(absl::OkStatus()));
    EXPECT_CALL(*hwm, Prepare).WillOnce(Return(absl::OkStatus()));
    EXPECT_CALL(*hwm, Activate).WillOnce(Return(OkStatus()));
    EXPECT_CALL(*hwm, ReadStatus).Times(::testing::AtLeast(1));
    EXPECT_CALL(*hwm, EnableMotion).WillOnce(Return(absl::OkStatus()));
    // It's a race condition whether ReadStatus is called between EnableMotion
    // and Enabled due to the async nature of the calls.
    EXPECT_CALL(*hwm, ReadStatus).Times(::testing::AnyNumber());
    EXPECT_CALL(*hwm, Enabled).WillOnce(Return(OkStatus()));
    EXPECT_CALL(*hwm, ReadStatus).Times(::testing::AtLeast(1));
    EXPECT_CALL(*hwm, ApplyCommand).WillOnce(Return(OkStatus()));
    EXPECT_CALL(*hwm, ReadStatus).Times(::testing::AtLeast(1));
    EXPECT_CALL(*hwm, Disabled).WillOnce(Return(OkStatus()));
    // It's a race condition whether ReadStatus is called between DisableMotion
    // and Disabled due to the async nature of the calls.
    EXPECT_CALL(*hwm, ReadStatus).Times(::testing::AnyNumber());
    EXPECT_CALL(*hwm, DisableMotion).WillOnce(Return(absl::OkStatus()));
    EXPECT_CALL(*hwm, ReadStatus).Times(::testing::AtLeast(1));
    EXPECT_CALL(*hwm, Deactivate).WillOnce(Return(OkStatus()));
  }

  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, kModuleName));
  ASSERT_OK_AND_ASSIGN(
      auto runtime_1,
      HardwareModuleRuntime::Create(std::move(shm_manager), std::move(module)));
  ASSERT_NE(hwm, nullptr);
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime_1->Run(server_builder1));
  auto module_config_1 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, kModuleName);
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1.IsConnected());
  HardwareModuleManager manager_;
  ASSERT_OK(manager_.Add(std::move(hardware_module_proxy_1),
                         /*proxy_drives_clock=*/true,
                         intrinsic_proto::icon::HardwareConfig{}));

  ASSERT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  ASSERT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));

  EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
  // Won't reach the HWM since not enabled yet.
  EXPECT_OK(manager_.Write(absl::Now() + kTransitionTimeout));

  EXPECT_OK(manager_.UpdateState());
  {
    EXPECT_OK(
        manager_
            .RequestTransition(
                RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
            .status());
    while (manager_.GetRequestState() !=
           RealtimeStateManagerInterface::RequestState::kDone) {
      EXPECT_OK(manager_.Read(
          absl::Now() +
          kTransitionTimeout));  // Process the request on HWM side
      EXPECT_OK(manager_.ProcessTransitionRequestAsync());
      EXPECT_OK(manager_.UpdateState());
      absl::SleepFor(absl::Milliseconds(10));
    }
  }
  EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Write(absl::Now() + kTransitionTimeout));

  {
    EXPECT_OK(manager_
                  .RequestTransition(RealtimeStateManagerInterface::
                                         TransitionRequest::kDisableMotion)
                  .status());
    while (manager_.GetRequestState() !=
           RealtimeStateManagerInterface::RequestState::kDone) {
      EXPECT_OK(manager_.Read(
          absl::Now() +
          kTransitionTimeout));  // Process the request on HWM side
      EXPECT_OK(manager_.ProcessTransitionRequestAsync());
      EXPECT_OK(manager_.UpdateState());
      absl::SleepFor(absl::Milliseconds(10));
    }
  }

  ASSERT_OK(manager_.Deactivate(absl::Now() + kTransitionTimeout));
}

TEST_F(HardwareModuleManagerTest, ApplyCommandOnlyCalledWhenEnabled) {
  EXPECT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));

  EXPECT_EQ(hardware_module_->GetReadStatusCount(), 0);
  EXPECT_EQ(hardware_module_->GetApplyCommandCount(), 0);

  EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Write(absl::Now() + kTransitionTimeout));

  EXPECT_EQ(hardware_module_->GetReadStatusCount(), 1);
  // Not enabled so no ApplyCommand called.
  EXPECT_EQ(hardware_module_->GetApplyCommandCount(), 0);
  EXPECT_OK(manager_.UpdateState());
  EXPECT_OK(
      manager_
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());
  size_t iterations = 0;
  while (manager_.GetRequestState() !=
         RealtimeStateManagerInterface::RequestState::kDone) {
    EXPECT_OK(manager_.Read(
        absl::Now() + kTransitionTimeout));  // Process the request on HWM side
    EXPECT_OK(manager_.ProcessTransitionRequestAsync());
    EXPECT_OK(manager_.UpdateState());
    absl::SleepFor(absl::Milliseconds(10));
    iterations++;
  }
  EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Write(absl::Now() + kTransitionTimeout));

  EXPECT_EQ(hardware_module_->GetReadStatusCount(), 2 + iterations);
  EXPECT_EQ(hardware_module_->GetApplyCommandCount(), 1);
}

TEST_F(HardwareModuleManagerTest, ClearFault) {
  EXPECT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));

  hardware_module_->SetFaultReason("Out of bread");
  EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.UpdateState());
  EXPECT_EQ(manager_.GetCombinedOperationalState(),
            RealtimeOperationalState::kFaultedConnected);
  EXPECT_OK(
      manager_
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kClearFaults)
          .status());
  EXPECT_NE(manager_.GetFaultReason(), std::nullopt);

  auto deadline = absl::Now() + kTransitionTimeout;
  // Let the transition complete and check that the state was correctly updated.
  do {
    EXPECT_OK(manager_.ProcessTransitionRequestAsync());
    EXPECT_OK(manager_.Read(deadline));  // Process the request on HWM side
    EXPECT_OK(manager_.UpdateState());
    absl::SleepFor(absl::Microseconds(100));
    ASSERT_THAT(absl::Now(), Lt(deadline))
        << "Timed out waiting for transition request";
  } while (manager_.GetRequestState() !=
           RealtimeStateManagerInterface::RequestState::kDone);
  EXPECT_OK(manager_.UpdateState());
  EXPECT_EQ(manager_.GetFaultReason(), std::nullopt);
  EXPECT_EQ(manager_.GetCombinedOperationalState(),
            RealtimeOperationalState::kDisabled);
}

TEST_F(HardwareModuleManagerTest, ReportCorrectFaultReason) {
  EXPECT_OK(manager_.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager_.Activate(absl::Now() + kTransitionTimeout));

  hardware_module_->SetFaultReason("Out of bread");
  EXPECT_EQ(manager_.GetFaultReason(), std::nullopt);
  EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
  // The fault reason will only be available after UpdateState().
  EXPECT_EQ(manager_.GetFaultReason(), std::nullopt);
  EXPECT_OK(manager_.UpdateState());

  EXPECT_THAT(
      manager_.GetFaultReason(),
      Optional(AllOf(HasSubstr(kModuleName), HasSubstr("Out of bread"))));
}

TEST(HardwareModuleManagerMultiModuleTest, ReportMultipleFaultReasons) {
  std::string memory_namespace = UniqueMemoryNamespace();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, "module_1"));
  ASSERT_OK_AND_ASSIGN(
      auto runtime_1,
      HardwareModuleRuntime::Create(
          std::move(shm_manager),
          GetModuleInstance(
              /*memory_namespace=*/memory_namespace, "module_1")));
  auto hardware_module_1 = dynamic_cast<TestHardwareModule*>(
      runtime_1->GetHardwareModule().instance.get());
  ASSERT_NE(hardware_module_1, nullptr);
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime_1->Run(server_builder1));
  auto module_config_1 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "module_1");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1.IsConnected());
  // Module 2
  ASSERT_OK_AND_ASSIGN(auto shm_manager2, SharedMemoryManager::Create(
                                              memory_namespace, "module_2"));
  ASSERT_OK_AND_ASSIGN(
      auto runtime_2,
      HardwareModuleRuntime::Create(
          std::move(shm_manager2),
          GetModuleInstance(
              /*memory_namespace=*/memory_namespace, "module_2")));
  auto hardware_module_2 = dynamic_cast<TestHardwareModule*>(
      runtime_2->GetHardwareModule().instance.get());
  ASSERT_NE(hardware_module_2, nullptr);
  grpc::ServerBuilder server_builder2;
  EXPECT_OK(runtime_2->Run(server_builder2));
  auto module_config_2 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "module_2");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_2,
      HardwareModuleProxy::Attach(module_config_2, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_2.IsConnected());
  HardwareModuleManager manager;
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_1),
                        /*proxy_drives_clock=*/true,
                        intrinsic_proto::icon::HardwareConfig{}));
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_2),
                        /*proxy_drives_clock=*/false,
                        intrinsic_proto::icon::HardwareConfig{}));
  EXPECT_OK(manager.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.Activate(absl::Now() + kTransitionTimeout));

  hardware_module_1->SetFaultReason("Out of bread");
  hardware_module_2->SetFaultReason("Out of milk");
  EXPECT_EQ(manager.GetFaultReason(), std::nullopt);
  EXPECT_OK(manager.Read(absl::Now() + kTransitionTimeout));
  // The fault reason will only be available after UpdateState().
  EXPECT_EQ(manager.GetFaultReason(), std::nullopt);
  EXPECT_OK(manager.UpdateState());

  EXPECT_THAT(manager.GetFaultReason(),
              Optional(AllOf(HasSubstr("module_1"), HasSubstr("Out of bread"),
                             HasSubstr("module_2"), HasSubstr("Out of milk"))));
}

TEST(HardwareModuleManagerMultiModuleTest, TwoModules) {
  std::string memory_namespace = UniqueMemoryNamespace();
  // Module1
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, "module_1"));
  ASSERT_OK_AND_ASSIGN(
      auto runtime_1,
      HardwareModuleRuntime::Create(
          std::move(shm_manager),
          GetModuleInstance(
              /*memory_namespace=*/memory_namespace, "module_1")));
  auto hardware_module_1 = dynamic_cast<TestHardwareModule*>(
      runtime_1->GetHardwareModule().instance.get());
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime_1->Run(server_builder1));
  auto module_config_1 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "module_1");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1.IsConnected());
  // Module 2
  ASSERT_OK_AND_ASSIGN(auto shm_manager_2, SharedMemoryManager::Create(
                                               memory_namespace, "module_2"));
  ASSERT_OK_AND_ASSIGN(
      auto runtime_2,
      HardwareModuleRuntime::Create(
          std::move(shm_manager_2),
          GetModuleInstance(
              /*memory_namespace=*/memory_namespace, "module_2")));
  auto hardware_module_2 = dynamic_cast<TestHardwareModule*>(
      runtime_2->GetHardwareModule().instance.get());
  grpc::ServerBuilder server_builder2;
  EXPECT_OK(runtime_2->Run(server_builder2));
  auto module_config_2 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "module_2");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_2,
      HardwareModuleProxy::Attach(module_config_2, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_2.IsConnected());
  const intrinsic_fbs::HardwareModuleState* current_state_1 =
      hardware_module_proxy_1.GetHardwareModuleState();
  const intrinsic_fbs::HardwareModuleState* current_state_2 =
      hardware_module_proxy_2.GetHardwareModuleState();
  HardwareModuleManager manager;
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_1),
                        /*proxy_drives_clock=*/true,
                        intrinsic_proto::icon::HardwareConfig{}));
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_2),
                        /*proxy_drives_clock=*/false,
                        intrinsic_proto::icon::HardwareConfig{}));

  const HardwareModuleProxy* proxy_ptr_1 =
      manager.GetHardwareModuleProxy("module_1");
  const HardwareModuleProxy* proxy_ptr_2 =
      manager.GetHardwareModuleProxy("module_2");

  // These are updated at every ApplyCommand
  ASSERT_OK_AND_ASSIGN(
      auto position_state_interface_1,
      proxy_ptr_1->GetHardwareInterface<intrinsic_fbs::JointPositionState>(
          "joint_position_state"));
  ASSERT_OK_AND_ASSIGN(
      auto position_state_interface_2,
      proxy_ptr_2->GetHardwareInterface<intrinsic_fbs::JointPositionState>(
          "joint_position_state"));

  // These are updated at every ReadStatus
  ASSERT_OK_AND_ASSIGN(
      auto position_velocity_interface_1,
      proxy_ptr_1->GetHardwareInterface<intrinsic_fbs::JointVelocityState>(
          "joint_velocity_state"));
  ASSERT_OK_AND_ASSIGN(
      auto position_velocity_interface_2,
      proxy_ptr_2->GetHardwareInterface<intrinsic_fbs::JointVelocityState>(
          "joint_velocity_state"));
  EXPECT_OK(manager.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.Activate(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.UpdateState());
  EXPECT_OK(
      manager
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());
  size_t iterations = 0;
  while (current_state_1->code() != intrinsic_fbs::StateCode::kMotionEnabled ||
         current_state_2->code() != intrinsic_fbs::StateCode::kMotionEnabled) {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(
        absl::Now() + kTransitionTimeout));  // Process the request on HWM side.
    absl::SleepFor(absl::Milliseconds(100));
    iterations++;
  }
  // Without update state, manager.Write() won't call ApplyCommand() since the
  // HWMs are not marked as enabled yet.
  EXPECT_OK(manager.Write(absl::Now() + kTransitionTimeout));
  EXPECT_EQ(hardware_module_1->GetApplyCommandCount(), 0);
  EXPECT_EQ(hardware_module_2->GetApplyCommandCount(), 0);

  // Update internal HWM state.
  EXPECT_OK(manager.UpdateState());
  EXPECT_OK(manager.Read(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.Write(absl::Now() + kTransitionTimeout));

  // All modules ticked
  EXPECT_EQ(hardware_module_1->GetReadStatusCount(), 1 + iterations);
  EXPECT_EQ(hardware_module_2->GetReadStatusCount(), 1 + iterations);
  EXPECT_EQ(hardware_module_1->GetApplyCommandCount(), 1);
  EXPECT_EQ(hardware_module_2->GetApplyCommandCount(), 1);

  // Since module_1 drives the clock its ReadStatus should be called first and
  // the velocity_state_interface_1 should be updated before
  // velocity_state_interface_2.
  EXPECT_LT(position_velocity_interface_1.LastUpdatedTime(),
            position_velocity_interface_2.LastUpdatedTime());

  // Since module_1 drives the clock its ApplyCommand should be called last and
  // the position_state_interface_1 should be updated after
  // position_state_interface_2.
  EXPECT_GT(position_state_interface_1.LastUpdatedTime(),
            position_state_interface_2.LastUpdatedTime());
}

TEST(HardwareModuleManagerMultiModuleTest, CheckAnyInOneOfStatesWorks) {
  std::string memory_namespace = UniqueMemoryNamespace();
  // Module1
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, "module_1"));
  ASSERT_OK_AND_ASSIGN(auto runtime_1,
                       HardwareModuleRuntime::Create(
                           std::move(shm_manager),
                           GetModuleInstance(memory_namespace, "module_1")));
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime_1->Run(server_builder1));
  ModuleConfig module_config_1 = GetModuleConfig(memory_namespace, "module_1");
  ASSERT_OK_AND_ASSIGN(
      std::optional<HardwareModuleProxy> hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1->IsConnected());

  // Module 2
  std::unique_ptr<HardwareModuleRuntime> runtime_2;
  {
    ASSERT_OK_AND_ASSIGN(auto shm_manager_2,
                         SharedMemoryManager::Create(
                             memory_namespace, "my_test_hardware_module_2"));
    ASSERT_OK_AND_ASSIGN(runtime_2,
                         HardwareModuleRuntime::Create(
                             std::move(shm_manager_2),
                             GetModuleInstance(
                                 /*memory_namespace=*/"memory_namespace",
                                 "my_test_hardware_module_2")));
  }
  TestHardwareModule* hardware_module_2 = dynamic_cast<TestHardwareModule*>(
      runtime_2->GetHardwareModule().instance.get());
  grpc::ServerBuilder server_builder2;
  EXPECT_OK(runtime_2->Run(server_builder2));
  ModuleConfig module_config_2 =
      GetModuleConfig(memory_namespace, "my_test_hardware_module_2");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_2,
      HardwareModuleProxy::Attach(module_config_2, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_2.IsConnected());

  HardwareModuleProxy& hardware_module_proxy_1_ref =
      hardware_module_proxy_1.value();
  HardwareModuleProxy& hardware_module_proxy_2_ref = hardware_module_proxy_2;

  HardwareModuleManager manager;
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_1.value()),
                        /*proxy_drives_clock=*/true,
                        intrinsic_proto::icon::HardwareConfig{}));
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_2),
                        /*proxy_drives_clock=*/false,
                        intrinsic_proto::icon::HardwareConfig{}));
  EXPECT_OK(manager.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.Activate(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.UpdateState());

  hardware_module_2->PauseTransitions(true);
  EXPECT_OK(
      manager
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());
  absl::Time deadline = absl::Now() + absl::Seconds(15);
  do {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(
        absl::Now() + kTransitionTimeout));  // Process the request on HWM side.
    EXPECT_OK(manager.UpdateState());
    absl::SleepFor(absl::Milliseconds(100));
    if (absl::Now() > deadline) {
      FAIL() << "Timed out waiting for transition request";
    }
  } while (hardware_module_proxy_1_ref.GetHardwareModuleState()->code() !=
               intrinsic_fbs::StateCode::kMotionEnabled ||
           hardware_module_proxy_2_ref.GetHardwareModuleState()->code() !=
               intrinsic_fbs::StateCode::kMotionEnabling);
  EXPECT_OK(
      manager.UpdateState());  // We need to update the cached state again, so
                               // that it is up2date for the next checks.
  // -- End of test setup --

  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kOperationalHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabling},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabling},
      RealtimeStateManagerInterface::Group::kOperationalHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled,
       intrinsic_fbs::StateCode::kMotionEnabling},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled,
       intrinsic_fbs::StateCode::kActivated},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();

  EXPECT_FALSE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kActivated},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_FALSE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kDeactivated},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();

  EXPECT_FALSE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kDeactivated,
       intrinsic_fbs::StateCode::kActivated},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_FALSE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kAllHardware));

  hardware_module_2->PauseTransitions(false);
  do {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(
        absl::Now() + kTransitionTimeout));  // Process the request on HWM side.
    EXPECT_OK(manager.UpdateState());
    absl::SleepFor(absl::Microseconds(100));
    if (absl::Now() > deadline) {
      FAIL() << "Timed out waiting for transition request";
    }
  } while (manager.GetRequestState() !=
           RealtimeStateManagerInterface::RequestState::kDone);
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled,
       intrinsic_fbs::StateCode::kMotionEnabling},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
}

TEST(HardwareModuleManagerMultiModuleTest, CheckAllInOneOfStatesWorks) {
  std::string memory_namespace = UniqueMemoryNamespace();
  // Module1
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, "module_1"));
  ASSERT_OK_AND_ASSIGN(auto runtime_1,
                       HardwareModuleRuntime::Create(
                           std::move(shm_manager),
                           GetModuleInstance(memory_namespace, "module_1")));
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime_1->Run(server_builder1));
  ModuleConfig module_config_1 = GetModuleConfig(memory_namespace, "module_1");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1.IsConnected());
  // Module 2
  ASSERT_OK_AND_ASSIGN(auto shm_manager_2, SharedMemoryManager::Create(
                                               memory_namespace, "module_2"));
  ASSERT_OK_AND_ASSIGN(auto runtime_2,
                       HardwareModuleRuntime::Create(
                           std::move(shm_manager_2),
                           GetModuleInstance(memory_namespace, "module_2")));
  TestHardwareModule* hardware_module_2 = dynamic_cast<TestHardwareModule*>(
      runtime_2->GetHardwareModule().instance.get());
  grpc::ServerBuilder server_builder2;
  EXPECT_OK(runtime_2->Run(server_builder2));
  ModuleConfig module_config_2 = GetModuleConfig(memory_namespace, "module_2");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_2,
      HardwareModuleProxy::Attach(module_config_2, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_2.IsConnected());

  HardwareModuleProxy& hardware_module_proxy_1_ref = hardware_module_proxy_1;
  HardwareModuleProxy& hardware_module_proxy_2_ref = hardware_module_proxy_2;

  HardwareModuleManager manager;
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_1),
                        /*proxy_drives_clock=*/true,
                        intrinsic_proto::icon::HardwareConfig{}));
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_2),
                        /*proxy_drives_clock=*/false,
                        intrinsic_proto::icon::HardwareConfig{}));
  EXPECT_OK(manager.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.Activate(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.UpdateState());
  hardware_module_2->PauseTransitions(true);
  EXPECT_OK(
      manager
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());
  absl::Time deadline = absl::Now() + absl::Seconds(15);
  do {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(
        absl::Now() + kTransitionTimeout));  // Process the request on HWM side.
    absl::SleepFor(absl::Microseconds(1000));
    if (absl::Now() > deadline) {
      FAIL() << "Timed out waiting for transition request";
    }
  } while (hardware_module_proxy_1_ref.GetHardwareModuleState()->code() !=
               intrinsic_fbs::StateCode::kMotionEnabled ||
           hardware_module_proxy_2_ref.GetHardwareModuleState()->code() !=
               intrinsic_fbs::StateCode::kMotionEnabling);

  EXPECT_OK(manager.UpdateState());
  // -- End of test setup --

  EXPECT_FALSE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_FALSE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled,
       intrinsic_fbs::StateCode::kFaulted},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled,
       intrinsic_fbs::StateCode::kMotionEnabling},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled,
       intrinsic_fbs::StateCode::kMotionEnabling,
       intrinsic_fbs::StateCode::kFaulted},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  hardware_module_2->PauseTransitions(false);
  do {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(
        absl::Now() + kTransitionTimeout));  // Process the request on HWM side.
    EXPECT_OK(manager.UpdateState());
    absl::SleepFor(absl::Microseconds(1000));
    if (absl::Now() > deadline) {
      FAIL() << "Timed out waiting for transition request";
    }
  } while (manager.GetRequestState() !=
           RealtimeStateManagerInterface::RequestState::kDone);
  EXPECT_TRUE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled,
       intrinsic_fbs::StateCode::kActivated},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();

  EXPECT_FALSE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kDeactivated,
       intrinsic_fbs::StateCode::kActivated},
      RealtimeStateManagerInterface::Group::kAllHardware))
      << "Summary:\n"
      << manager.StateSummary();
}

TEST(HardwareModuleManagerMultiModuleTest,
     CanRequestTransitionOnHwm1WhileHwm2IsBusy) {
  std::string memory_namespace = UniqueMemoryNamespace();
  // Module1
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, "module_1"));
  ASSERT_OK_AND_ASSIGN(auto runtime_1,
                       HardwareModuleRuntime::Create(
                           std::move(shm_manager),
                           GetModuleInstance(memory_namespace, "module_1")));
  TestHardwareModule* hardware_module_1 = dynamic_cast<TestHardwareModule*>(
      runtime_1->GetHardwareModule().instance.get());
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime_1->Run(server_builder1));
  ModuleConfig module_config_1 = GetModuleConfig(memory_namespace, "module_1");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1.IsConnected());
  // Module 2
  ASSERT_OK_AND_ASSIGN(auto shm_manager_2, SharedMemoryManager::Create(
                                               memory_namespace, "module_2"));
  ASSERT_OK_AND_ASSIGN(auto runtime_2,
                       HardwareModuleRuntime::Create(
                           std::move(shm_manager_2),
                           GetModuleInstance(memory_namespace, "module_2")));
  TestHardwareModule* hardware_module_2 = dynamic_cast<TestHardwareModule*>(
      runtime_2->GetHardwareModule().instance.get());
  grpc::ServerBuilder server_builder2;
  EXPECT_OK(runtime_2->Run(server_builder2));
  ModuleConfig module_config_2 = GetModuleConfig(memory_namespace, "module_2");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_2,
      HardwareModuleProxy::Attach(module_config_2, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_2.IsConnected());

  HardwareModuleManager manager;
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_1),
                        /*proxy_drives_clock=*/true,
                        intrinsic_proto::icon::HardwareConfig{}));
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_2),
                        /*proxy_drives_clock=*/false,
                        intrinsic_proto::icon::HardwareConfig{}));
  EXPECT_OK(manager.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.Activate(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.UpdateState());

  hardware_module_2->PauseTransitions(true);
  EXPECT_OK(
      manager
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());
  absl::Time deadline = absl::Now() + absl::Seconds(15);
  do {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(
        absl::Now() + kTransitionTimeout));  // Process the request on HWM side.
    EXPECT_OK(manager.UpdateState());
    absl::SleepFor(absl::Microseconds(100));
    if (absl::Now() > deadline) {
      FAIL() << "Timed out waiting for transition request";
    }
  } while (manager.GetRequestState() !=
           RealtimeStateManagerInterface::RequestState::kPartiallyDone);
  EXPECT_OK(manager.Read(
      absl::Now() + kTransitionTimeout));  // Process ReadStatus one more time
                                           // so that Enabled() gets called.
  EXPECT_EQ(hardware_module_1->GetCurrentState(),
            TestHardwareModule::State::kMotionEnabled);
  EXPECT_OK(
      manager
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kDisableMotion)
          .status());

  hardware_module_2->PauseTransitions(false);

  do {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(
        absl::Now() + kTransitionTimeout));  // Process the request on HWM side.
    EXPECT_OK(manager.UpdateState());
    absl::SleepFor(absl::Microseconds(100));
    if (absl::Now() > deadline) {
      FAIL() << "Timed out waiting for transition request";
    }
  } while (manager.GetRequestState() !=
           RealtimeStateManagerInterface::RequestState::kDone);
  EXPECT_OK(manager.UpdateState());
  EXPECT_EQ(manager.GetCombinedOperationalState(),
            RealtimeOperationalState::kDisabled);
}

TEST(HardwareModuleManagerMultiModuleTest,
     DisableMotionSkipCellControlHardware) {
  // module_2 is cell control hardware.
  std::string memory_namespace = UniqueMemoryNamespace();
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, "module_1"));
  ASSERT_OK_AND_ASSIGN(
      auto runtime_1,
      HardwareModuleRuntime::Create(
          std::move(shm_manager),
          GetModuleInstance(
              /*memory_namespace=*/memory_namespace, "module_1")));
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime_1->Run(server_builder1));
  auto module_config_1 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "module_1");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1.IsConnected());
  ASSERT_OK_AND_ASSIGN(auto shm_manager_2, SharedMemoryManager::Create(
                                               memory_namespace, "module_2"));
  ASSERT_OK_AND_ASSIGN(
      auto runtime_2,
      HardwareModuleRuntime::Create(
          std::move(shm_manager_2),
          GetModuleInstance(
              /*memory_namespace=*/memory_namespace, "module_2")));
  grpc::ServerBuilder server_builder2;
  EXPECT_OK(runtime_2->Run(server_builder2));
  auto module_config_2 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "module_2");
  ASSERT_OK_AND_ASSIGN(
      auto hardware_module_proxy_2,
      HardwareModuleProxy::Attach(module_config_2, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_2.IsConnected());
  HardwareModuleManager manager;
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_1),
                        /*proxy_drives_clock=*/true,
                        intrinsic_proto::icon::HardwareConfig{}));
  intrinsic_proto::icon::HardwareConfig hardware_config_2;
  // Configure module_2 as cell control hardware.
  hardware_config_2.mutable_cell_control_hardware();
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_2),
                        /*proxy_drives_clock=*/false, hardware_config_2));

  const HardwareModuleProxy* proxy_ptr_1 =
      manager.GetHardwareModuleProxy("module_1");
  const HardwareModuleProxy* proxy_ptr_2 =
      manager.GetHardwareModuleProxy("module_2");

  EXPECT_OK(manager.Prepare(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.Activate(absl::Now() + kTransitionTimeout));
  EXPECT_OK(manager.UpdateState());
  EXPECT_OK(
      manager
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kEnableMotion)
          .status());

  while (manager.GetRequestState() !=
         RealtimeStateManagerInterface::RequestState::kDone) {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(absl::Now() + kTransitionTimeout));
    EXPECT_OK(manager.UpdateState());
    absl::SleepFor(absl::Milliseconds(100));
  }
  EXPECT_EQ(proxy_ptr_1->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kMotionEnabled);
  EXPECT_EQ(proxy_ptr_2->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kMotionEnabled);
  EXPECT_EQ(manager.GetCombinedOperationalState(),
            RealtimeOperationalState::kEnabled);
  EXPECT_EQ(manager.GetCellControlState(), RealtimeOperationalState::kEnabled);

  EXPECT_OK(
      manager
          .RequestTransition(RealtimeStateManagerInterface::TransitionRequest::
                                 kDisableMotionSkipCellControlHardware)
          .status());
  while (manager.GetRequestState() !=
         RealtimeStateManagerInterface::RequestState::kDone) {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(absl::Now() + kTransitionTimeout));
    EXPECT_OK(manager.UpdateState());
    absl::SleepFor(absl::Milliseconds(100));
  }
  EXPECT_EQ(proxy_ptr_1->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kActivated);
  EXPECT_EQ(proxy_ptr_2->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kMotionEnabled)
      << "cell control hardware should still be enabled";
  EXPECT_TRUE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kActivated},
      RealtimeStateManagerInterface::Group::kOperationalHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kActivated},
      RealtimeStateManagerInterface::Group::kOperationalHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAllInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kCellControlHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_TRUE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kCellControlHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_EQ(manager.GetCombinedOperationalState(),
            RealtimeOperationalState::kDisabled);
  EXPECT_EQ(manager.GetCellControlState(), RealtimeOperationalState::kEnabled)
      << "cell control hardware should still be enabled";

  EXPECT_OK(
      manager
          .RequestTransition(
              RealtimeStateManagerInterface::TransitionRequest::kDisableMotion)
          .status());
  while (manager.GetRequestState() !=
         RealtimeStateManagerInterface::RequestState::kDone) {
    EXPECT_OK(manager.ProcessTransitionRequestAsync());
    EXPECT_OK(manager.Read(absl::Now() + kTransitionTimeout));
    EXPECT_OK(manager.UpdateState());
    absl::SleepFor(absl::Milliseconds(100));
  }
  EXPECT_EQ(proxy_ptr_1->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kActivated);
  EXPECT_EQ(proxy_ptr_2->GetHardwareModuleState()->code(),
            intrinsic_fbs::StateCode::kActivated);
  EXPECT_EQ(manager.GetCombinedOperationalState(),
            RealtimeOperationalState::kDisabled);
  EXPECT_EQ(manager.GetCellControlState(), RealtimeOperationalState::kDisabled);

  EXPECT_FALSE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kOperationalHardware))
      << "Summary:\n"
      << manager.StateSummary();
  EXPECT_FALSE(manager.CheckAnyInOneOfStates(
      {intrinsic_fbs::StateCode::kMotionEnabled},
      RealtimeStateManagerInterface::Group::kCellControlHardware))
      << "Summary:\n"
      << manager.StateSummary();
}

TEST(HardwareModuleManagerMultiModuleTest,
     FailsIfAddingSecondClockDrivingProxy) {
  std::string memory_namespace = UniqueMemoryNamespace();
  // Module1
  ASSERT_OK_AND_ASSIGN(auto shm_manager, SharedMemoryManager::Create(
                                             memory_namespace, "module_1"));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<HardwareModuleRuntime> runtime_1,
      HardwareModuleRuntime::Create(
          std::move(shm_manager),
          GetModuleInstance(
              /*memory_namespace=*/memory_namespace, "module_1")));
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime_1->Run(server_builder1));
  ModuleConfig module_config_1 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "module_1");
  ASSERT_OK_AND_ASSIGN(
      HardwareModuleProxy hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1.IsConnected());
  // Module 2
  ASSERT_OK_AND_ASSIGN(auto shm_manager_2, SharedMemoryManager::Create(
                                               memory_namespace, "module_2"));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<HardwareModuleRuntime> runtime_2,
      HardwareModuleRuntime::Create(
          std::move(shm_manager_2),
          GetModuleInstance(
              /*memory_namespace=*/memory_namespace, "module_2")));
  grpc::ServerBuilder server_builder2;
  EXPECT_OK(runtime_2->Run(server_builder2));
  ModuleConfig module_config_2 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "module_2");
  ASSERT_OK_AND_ASSIGN(
      HardwareModuleProxy hardware_module_proxy_2,
      HardwareModuleProxy::Attach(module_config_2, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_2.IsConnected());

  HardwareModuleManager manager;
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_1),
                        /*proxy_drives_clock=*/true,
                        intrinsic_proto::icon::HardwareConfig{}));
  EXPECT_THAT(manager.Add(std::move(hardware_module_proxy_2),
                          /*proxy_drives_clock=*/true,
                          intrinsic_proto::icon::HardwareConfig{}),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(StrictHardwareModuleManagerTest, GetUnusedRequiredInterfaceNamesWorks) {
  std::string memory_namespace = UniqueMemoryNamespace();
  ASSERT_OK_AND_ASSIGN(
      auto shm_manager,
      SharedMemoryManager::Create(memory_namespace, "strict_module"));
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<HardwareModuleRuntime> runtime,
      HardwareModuleRuntime::Create(
          std::move(shm_manager),
          GetStrictModuleInstance(
              /*memory_namespace=*/memory_namespace, "strict_module")));
  grpc::ServerBuilder server_builder1;
  EXPECT_OK(runtime->Run(server_builder1));
  ModuleConfig module_config_1 =
      GetModuleConfig(/*memory_namespace=*/memory_namespace, "strict_module");
  ASSERT_OK_AND_ASSIGN(
      HardwareModuleProxy hardware_module_proxy_1,
      HardwareModuleProxy::Attach(module_config_1, absl::ZeroDuration()));
  EXPECT_TRUE(hardware_module_proxy_1.IsConnected());

  HardwareModuleManager manager;
  ASSERT_OK(manager.Add(std::move(hardware_module_proxy_1),
                        /*proxy_drives_clock=*/true,
                        intrinsic_proto::icon::HardwareConfig{}));
  EXPECT_THAT(
      manager.GetUnusedRequiredInterfaceNames(),
      IsOkAndHolds(UnorderedElementsAre(
          Pair("strict_module", UnorderedElementsAre("strict_dio_command")))));

  const HardwareModuleProxy* hardware_module_proxy_ptr =
      manager.GetHardwareModuleProxy("strict_module");
  EXPECT_NE(hardware_module_proxy_ptr, nullptr);

  ASSERT_OK_AND_ASSIGN(auto strict_adio_command,
                       hardware_module_proxy_ptr
                           ->GetHardwareInterface<intrinsic_fbs::DIOCommand>(
                               kStrictDioCommandInterfaceName));

  EXPECT_THAT(manager.GetUnusedRequiredInterfaceNames(),
              IsOkAndHolds(SizeIs(0)));
}

TEST_F(HardwareModuleManagerTest, ForwardsFatallyFaulted) {
  EXPECT_OK(manager_.Read(absl::Now() + kTransitionTimeout));
  runtime_->SetStateTestOnly(intrinsic_fbs::StateCode::kInitFailed,
                             "my_fault_reason");
  EXPECT_OK(manager_.UpdateState());
  EXPECT_EQ(manager_.GetCombinedOperationalState(),
            RealtimeOperationalState::kFatallyFaulted);
  EXPECT_THAT(manager_.GetFaultReason(),
              Optional(HasSubstr("my_fault_reason")));
}

}  // namespace
}  // namespace intrinsic::icon
