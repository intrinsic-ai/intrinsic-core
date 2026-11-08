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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ACTION_TEST_HELPER_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ACTION_TEST_HELPER_H_

#include <memory>
#include <type_traits>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_action_interface.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_action_factory_context_fake.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_realtime_signal_access_and_map_fake.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_slot_map_fake.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/icon_streaming_io_registry_fake.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"

namespace intrinsic::icon {

// Encapsulates the fakes required to test a custom ICON Action.
class ActionTestHelper {
 public:
  // Creates a new ActionTestHelper with a ServerConfig that contains the given
  // `control_frequency_hz` and `server_name`.
  //
  // NOTE: ActionTestHelper does not have any Slots by default. To add Slots,
  // call `slot_map().AddLoopbackFakeArmSlot("my_slot_name")` *before*
  // attempting to build an Action.
  explicit ActionTestHelper(
      double control_frequency_hz,
      const ::intrinsic_proto::icon::v1::ActionSignature& signature,
      absl::string_view server_name = "");

  // Invokes ActionT::Create() with the given `params`, as well as an
  // `IconActionFactoryContext` object that exposes all of the Slots you've
  // added via `slot_map()`.
  //
  // You can access any streaming IOs your Action registers in its Create()
  // method via `streaming_io_registry()`.
  //
  // Forwards any errors from the Action's Create() function.
  template <typename ActionT, typename ParameterT,
            typename = std::enable_if_t<
                std::is_base_of_v<IconActionInterface, ActionT>>,
            typename = std::enable_if_t<
                std::is_base_of_v<::google::protobuf::Message, ParameterT>>>
  absl::StatusOr<std::unique_ptr<ActionT>> CreateAction(
      const ParameterT& params) {
    IconActionFactoryContextFake fake_context(server_config_, slot_map_,
                                              streaming_io_registry_,
                                              signal_access_and_map_);
    auto context = fake_context.MakeIconActionFactoryContext();
    return ActionT::Create(params, context);
  }

  IconSlotMapFake& slot_map() { return slot_map_; }
  IconStreamingIoRegistryFake& streaming_io_registry() {
    return streaming_io_registry_;
  }

  // Invokes `action.OnEnter()` with an `IconConstRealtimeSlotMap` that is
  // backed by `slot_map()`.
  RealtimeStatus EnterAction(IconActionInterface& action);

  // Invokes `action.Sense()` and `action.Control()` with an
  // `IconConstRealtimeSlotMap` or `IconRealtimeSlotMap` that is backed by
  // `slot_map()`, and an `IconStreamingIoAccess` that is backed by
  // `streaming_io_registry()`.
  //
  // You can validate the outputs of your action by inspecting `slot_map()` and
  // `streaming_io_registry()`.
  // You can also write streaming input values via `streaming_io_registry()`!
  RealtimeStatus SenseAndControlAction(IconActionInterface& action);

 private:
  IconSlotMapFake slot_map_;
  IconStreamingIoRegistryFake streaming_io_registry_;
  IconRealtimeSignalAccessAndMapFake signal_access_and_map_;
  ::intrinsic_proto::icon::v1::ServerConfig server_config_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ACTION_TEST_HELPER_H_
