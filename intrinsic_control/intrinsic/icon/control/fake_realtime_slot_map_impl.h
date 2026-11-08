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

#ifndef INTRINSIC_ICON_CONTROL_FAKE_REALTIME_SLOT_MAP_IMPL_H_
#define INTRINSIC_ICON_CONTROL_FAKE_REALTIME_SLOT_MAP_IMPL_H_

#include <functional>

#include "absl/container/flat_hash_map.h"
#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/realtime_slot_map.h"
#include "intrinsic/icon/control/slot_types.h"

namespace intrinsic::icon {

// Test-only implementation of RealtimeSlotMapInterface.
// Unlike MockRealtimeSlotMapImpl, the accessor methods of this class never
// allocate or free any memory.
class FakeRealtimeSlotMapImpl : public RealtimeSlotMapInterface {
 public:
  // Creates a new FakeRealtimeSlotMapImpl with the given
  // `slot_id_to_feature_interface_registry` map.
  //
  // Note that FakeRealtimeSlotMapImpl holds on to the references in the map.
  // This means the references must remain valid for the lifetime of the
  // FakeRealtimeSlotMapImpl instance.
  explicit FakeRealtimeSlotMapImpl(
      const absl::flat_hash_map<
          RealtimeSlotId, std::reference_wrapper<FeatureInterfaceRegistry>>&
          slot_id_to_feature_interface_registry = {})
      : slot_id_to_feature_interface_registry_(
            slot_id_to_feature_interface_registry) {}

  // Both of these return nullptr if there is no slot with `slot_id`.
  FeatureInterfaceRegistry* GetMutableRegistryForSlot(
      RealtimeSlotId slot_id) override;
  const FeatureInterfaceRegistry* GetRegistryForSlot(
      RealtimeSlotId slot_id) const override;

 private:
  absl::flat_hash_map<RealtimeSlotId,
                      std::reference_wrapper<FeatureInterfaceRegistry>>
      slot_id_to_feature_interface_registry_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_FAKE_REALTIME_SLOT_MAP_IMPL_H_
