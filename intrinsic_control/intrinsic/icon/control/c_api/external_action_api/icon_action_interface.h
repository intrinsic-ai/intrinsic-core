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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_ACTION_INTERFACE_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_ACTION_INTERFACE_H_

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_action_factory_context.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_signal_access.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_slot_map.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_streaming_io_access.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/utils/realtime_status.h"

namespace intrinsic::icon {

// This class defines the API for ICON realtime Actions. To create a custom
// Action, inherit from this class and implement all virtual functions.
//
// In addition, you will need to add the following to your Action class:
//
// 1. A type alias `ParameterProto` that maps to the proto message type for its
//    fixed parameters (i.e.
//    `using ParameterProto = my_proto_namespace::MyActionParameters`).
// 2. A static absl::string_view constant `kName` that contains the Action type
//    name. ICON API clients use this to create instances of your Action.
// 3. A static `Create()` function that implements
//    IconActionInterface::Factory<ActionT::ParameterProto> (see below).
// 4. A static `GetSignature()` function that implements
//    IconActionInterface::GetSignatureProto (see below).
class IconActionInterface {
 public:
  // Each class that inherits from IconActionInterface must define a static
  // factory function called `Create()` with this signature. ICON calls this
  // function in a non-realtime thread to create an instance of the Action type.
  template <class ParameterProto>
  using Factory =
      std::function<absl::StatusOr<std::unique_ptr<IconActionInterface>>(
          const ParameterProto& parameters, IconActionFactoryContext& context)>;

  // Each class that inherits from IconActionInterface must define a static
  // function called `GetSignature()` with this signature. ICON calls this
  // function on startup to generate the ActionSignature proto that it returns
  // to ICON API clients asking about an Action type.
  using GetSignatureProto =
      std::function<::intrinsic_proto::icon::v1::ActionSignature()>;

  virtual ~IconActionInterface() = default;

  // ICON calls this method whenever your Action becomes active. An Action can
  // become active in one of two ways:
  // 1. A user of the ICON client API explicitly calls StartAction(action_id).
  // 2. A realtime Reaction switches to the Action from another.
  //
  // In both cases, you should reset any internal state in this function.
  //
  // If this returns a non-OK status, ICON ends the corresponding Session and
  // disables the hardware. Any error messages are made available to the ICON
  // client API.
  virtual RealtimeStatus OnEnter(const IconConstRealtimeSlotMap& slot_map) = 0;

  // ICON calls this once on every tick that the Action is active (including the
  // tick when it calls `OnEnter()`!).
  //
  // Use `slot_map` to read any part state your Action cares about, and
  // `io_access` to read from any streaming inputs / write to any streaming
  // outputs.
  //
  // You should also update the internal representation for any state variables
  // in this function, since ICON may call `GetStateVariable()` in between
  // `Sense()` and `Control()`.
  //
  // If this returns a non-OK status, ICON ends the corresponding Session and
  // disables the robot. Any error messages are made available to the ICON
  // client API.
  virtual RealtimeStatus Sense(const IconConstRealtimeSlotMap& slot_map,
                               IconStreamingIoAccess& io_access,
                               IconRealtimeSignalAccess& signal_access) = 0;

  // ICON calls this once on every tick that the Action is active.
  //
  // Use `slot_map` to send commands to the parts your Action controls.
  //
  // If this returns a non-OK status, ICON ends the corresponding Session and
  // disables the robot. Any error messages are made available to the ICON
  // client API.
  virtual RealtimeStatus Control(IconRealtimeSlotMap& slot_map) = 0;

  // ICON may call this any number of times per cycle *after* it has called
  // `Sense()`, to check the current values of state variables.
  //
  // State variables can trigger realtime reactions. If a reaction happens, ICON
  // may switch to another Action and *not* call `Control()`!
  virtual RealtimeStatusOr<StateVariableValue> GetStateVariable(
      absl::string_view name) const = 0;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_ACTION_INTERFACE_H_
