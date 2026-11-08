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

#include "intrinsic/icon/control/fake_realtime_slot_map_impl.h"

#include "intrinsic/icon/control/parts/feature_interface_registry.h"
#include "intrinsic/icon/control/slot_types.h"

namespace intrinsic::icon {

FeatureInterfaceRegistry* FakeRealtimeSlotMapImpl::GetMutableRegistryForSlot(
    RealtimeSlotId slot_id) {
  auto registry = slot_id_to_feature_interface_registry_.find(slot_id);
  if (registry == slot_id_to_feature_interface_registry_.end()) {
    return nullptr;
  }
  return &registry->second.get();
}

const FeatureInterfaceRegistry* FakeRealtimeSlotMapImpl::GetRegistryForSlot(
    RealtimeSlotId slot_id) const {
  auto registry = slot_id_to_feature_interface_registry_.find(slot_id);
  if (registry == slot_id_to_feature_interface_registry_.end()) {
    return nullptr;
  }
  return &registry->second.get();
}

}  // namespace intrinsic::icon
