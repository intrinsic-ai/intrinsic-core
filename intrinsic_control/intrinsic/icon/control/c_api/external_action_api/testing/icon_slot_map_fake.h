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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_SLOT_MAP_FAKE_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_SLOT_MAP_FAKE_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/icon/control/c_api/c_realtime_slot_map.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_slot_map.h"
#include "intrinsic/icon/control/c_api/external_action_api/testing/loopback_fake_arm.h"
#include "intrinsic/icon/control/slot_types.h"
#include "intrinsic/kinematics/types/joint_limits.h"

namespace intrinsic::icon {

// Fake implementation of an ICON SlotMap that implements the C API for
// Icon(Const)RealtimeSlotMap. This also backs IconActionFactoryContextFake's
// GetSlotInfo() implementation.
//
// Use this to test
// * Your Action's Create() method (via IconActionFactoryContextFake)
// * Your Action's Sense() and Control() methods (via MakeIconRealtimeSlotMap())
class IconSlotMapFake {
 public:
  IconSlotMapFake();

  // Move-only because of the unique_ptr map below.
  IconSlotMapFake(IconSlotMapFake&& other) = default;
  IconSlotMapFake& operator=(IconSlotMapFake&& other) = default;

  IconSlotMapFake(const IconSlotMapFake&) = delete;
  IconSlotMapFake& operator=(const IconSlotMapFake&) = delete;

  // Adds a LoopbackFakeArm with the given `name` and
  // `application_limits`/`system_limits` to this slot map and returns a pointer
  // to it. Both `application_limits` and `system_limits` default to a six-DoF
  // set of unlimited JointLimits if omitted.
  //
  // Returns AlreadyExistsError if there is already a Part called `name` in this
  // slot map.
  // Returns an error if `application_limits` or `system_limits` are invalid
  // (f.i. if they do not have exactly 6 degrees of freedom).
  absl::StatusOr<LoopbackFakeArm*> AddLoopbackFakeArmSlot(
      absl::string_view slot_name,
      std::optional<JointLimits> application_limits = std::nullopt,
      std::optional<JointLimits> system_limits = std::nullopt);

  // Returns a pointer to the LoopbackFakeArm for the slot `slot_name`, or
  // nullptr if there is no such slot.
  //
  // Use this pointer to feed values to an Action under test and validate its
  // behavior.
  LoopbackFakeArm* GetFakeArmForSlot(absl::string_view slot_name);

  // Returns a pointer to the LoopbackFakeArm for the slot `slot_name`, or
  // nullptr if there is no such slot.
  //
  // Use this pointer to validate the behavior of an Action under test by
  // reading values from the LoopbackFakeArm.
  const LoopbackFakeArm* GetFakeArmForSlot(absl::string_view slot_name) const;

  // Returns the SlotInfo struct for `slot_name`, if any.
  absl::StatusOr<SlotInfo> GetSlotInfoForSlot(
      absl::string_view slot_name) const;

  // Returns the RealtimeSlotId struct for `slot_name`, if any.
  absl::StatusOr<RealtimeSlotId> GetIdForSlot(
      absl::string_view slot_name) const;

  // Creates an IconRealtimeSlotMap that is backed by this IconSlotMapFake. You
  // can then pass that object to the Control() method of an Action under test.
  IconRealtimeSlotMap MakeIconRealtimeSlotMap();

  // Creates an IconRealtimeSlotMap that is backed by this IconSlotMapFake. You
  // can then pass that object to the OnEnter() / Sense() methods of an Action
  // under test.
  IconConstRealtimeSlotMap MakeIconConstRealtimeSlotMap() const;

 private:
  struct SlotInfoAndFakeArm {
    SlotInfo slot_info;
    LoopbackFakeArm fake_arm;
  };

  static IntrinsicIconRealtimeSlotMapVtable GetCApiVtable();

  size_t next_slot_index_ = 0;
  absl::flat_hash_map<RealtimeSlotId, std::string> slot_id_to_slot_name_;
  // unique_ptr for pointer stability of values
  absl::flat_hash_map<std::string, std::unique_ptr<SlotInfoAndFakeArm>>
      slot_name_to_data_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_TESTING_ICON_SLOT_MAP_FAKE_H_
