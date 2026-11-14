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

#ifndef INTRINSIC_ICON_SKILLS_UTIL_ANALOG_DIGITAL_IO_FACTORY_H_
#define INTRINSIC_ICON_SKILLS_UTIL_ANALOG_DIGITAL_IO_FACTORY_H_
#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/skills/util/analog_digital_io.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/proto/equipment.pb.h"

namespace intrinsic::skills {

// Creates an instance of `AnalogDigitalIOInterface` from an ICON
// `ResolvedDependency`.
absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> CreateAnalogDigitalIO(
    const intrinsic_proto::assets::v1::ResolvedDependency& resolved_dependency);

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> CreateAnalogDigitalIO(
    absl::string_view equipment_slot,
    const google::protobuf::Map<std::string,
                                intrinsic_proto::resources::ResourceHandle>&
        resource_handles,
    const icon::ChannelFactory* channel_factory = nullptr);

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> CreateAnalogDigitalIO(
    absl::string_view equipment_slot, const EquipmentPack& equipment_pack,
    const icon::ChannelFactory* channel_factory = nullptr);

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> CreateAnalogDigitalIO(
    const intrinsic_proto::resources::ResourceHandle& resource_handle,
    const icon::ChannelFactory* channel_factory = nullptr);

}  // namespace intrinsic::skills

#endif  // INTRINSIC_ICON_SKILLS_UTIL_ANALOG_DIGITAL_IO_FACTORY_H_
