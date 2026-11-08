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

#include "intrinsic/icon/control/c_api/external_action_api/icon_realtime_slot_map.h"

#include "intrinsic/icon/control/c_api/c_realtime_slot_map.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_feature_interfaces.h"
#include "intrinsic/icon/control/slot_types.h"

namespace intrinsic::icon {

IconConstFeatureInterfaces IconRealtimeSlotMap::FeatureInterfacesForSlot(
    RealtimeSlotId slot_id) const {
  auto feature_interfaces =
      realtime_slot_map_vtable_.get_feature_interfaces_for_slot(
          realtime_slot_map_, slot_id.value());
  return FromCApiFeatureInterfaces(feature_interfaces,
                                   feature_interfaces_vtable_);
}

IconFeatureInterfaces IconRealtimeSlotMap::MutableFeatureInterfacesForSlot(
    RealtimeSlotId slot_id) {
  auto feature_interfaces =
      realtime_slot_map_vtable_.get_mutable_feature_interfaces_for_slot(
          realtime_slot_map_, slot_id.value());
  return FromCApiFeatureInterfaces(feature_interfaces,
                                   feature_interfaces_vtable_);
}

IconConstFeatureInterfaces IconConstRealtimeSlotMap::FeatureInterfacesForSlot(
    RealtimeSlotId slot_id) const {
  auto feature_interfaces =
      realtime_slot_map_vtable_.get_feature_interfaces_for_slot(
          realtime_slot_map_, slot_id.value());
  return FromCApiFeatureInterfaces(feature_interfaces,
                                   feature_interfaces_vtable_);
}

}  // namespace intrinsic::icon
