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

#ifndef INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_REALTIME_SLOT_MAP_H_
#define INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_REALTIME_SLOT_MAP_H_

#include <optional>
#include <utility>

#include "intrinsic/icon/control/c_api/c_feature_interfaces.h"
#include "intrinsic/icon/control/c_api/c_realtime_slot_map.h"
#include "intrinsic/icon/control/c_api/c_rtcl_action.h"
#include "intrinsic/icon/control/c_api/external_action_api/icon_feature_interfaces.h"
#include "intrinsic/icon/control/slot_types.h"

namespace intrinsic::icon {

class IconRealtimeSlotMap {
 public:
  IconRealtimeSlotMap(
      IntrinsicIconRealtimeSlotMap* realtime_slot_map,
      IntrinsicIconRealtimeSlotMapVtable realtime_slot_map_vtable,
      IntrinsicIconFeatureInterfaceVtable feature_interfaces_vtable)
      : realtime_slot_map_(realtime_slot_map),
        realtime_slot_map_vtable_(std::move(realtime_slot_map_vtable)),
        feature_interfaces_vtable_(std::move(feature_interfaces_vtable)) {}

  // Returns const FeatureInterfaces for `slot_id`.
  //
  // Returns a struct with all members set to std::nullopt for invalid
  // `slot_id`s.
  IconConstFeatureInterfaces FeatureInterfacesForSlot(
      RealtimeSlotId slot_id) const;

  // Returns the FeatureInterfaces for `slot_id`.
  //
  // Returns a struct with all members set to std::nullopt for invalid
  // `slot_id`s.
  IconFeatureInterfaces MutableFeatureInterfacesForSlot(RealtimeSlotId slot_id);

 private:
  IntrinsicIconRealtimeSlotMap* realtime_slot_map_ = nullptr;
  IntrinsicIconRealtimeSlotMapVtable realtime_slot_map_vtable_;
  IntrinsicIconFeatureInterfaceVtable feature_interfaces_vtable_;
};

class IconConstRealtimeSlotMap {
 public:
  IconConstRealtimeSlotMap(
      const IntrinsicIconRealtimeSlotMap* realtime_slot_map,
      const IntrinsicIconRealtimeSlotMapVtable realtime_slot_map_vtable,
      const IntrinsicIconFeatureInterfaceVtable feature_interfaces_vtable)
      : realtime_slot_map_(realtime_slot_map),
        realtime_slot_map_vtable_(std::move(realtime_slot_map_vtable)),
        feature_interfaces_vtable_(std::move(feature_interfaces_vtable)) {}

  // Returns the FeatureInterfaces for `slot_id`.
  //
  // Returns a struct with all members set to std::nullopt for invalid
  // `slot_id`s.
  IconConstFeatureInterfaces FeatureInterfacesForSlot(
      RealtimeSlotId slot_id) const;

 private:
  const IntrinsicIconRealtimeSlotMap* realtime_slot_map_ = nullptr;
  const IntrinsicIconRealtimeSlotMapVtable realtime_slot_map_vtable_;
  const IntrinsicIconFeatureInterfaceVtable feature_interfaces_vtable_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_CONTROL_C_API_EXTERNAL_ACTION_API_ICON_REALTIME_SLOT_MAP_H_
