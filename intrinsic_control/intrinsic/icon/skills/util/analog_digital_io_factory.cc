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

#include "intrinsic/icon/skills/util/analog_digital_io_factory.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/equipment/resolved_dependency_utils.h"
#include "intrinsic/icon/skills/util/analog_digital_io.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::skills {

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> CreateAnalogDigitalIO(
    const intrinsic_proto::assets::v1::ResolvedDependency&
        resolved_dependency) {
  INTR_ASSIGN_OR_RETURN(
      icon::IconResolvedDependency adio_resolved_dependency,
      icon::ConnectToIconResolvedDependency(resolved_dependency));
  INTR_ASSIGN_OR_RETURN(std::vector<std::string> adio_part_names,
                        adio_resolved_dependency.GetAdioPartNames());
  return IconAnalogDigitalIO::Create(
      adio_part_names, std::move(adio_resolved_dependency.channel));
}

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> CreateAnalogDigitalIO(
    absl::string_view equipment_slot,
    const google::protobuf::Map<std::string,
                                intrinsic_proto::resources::ResourceHandle>&
        resource_handles,
    const icon::ChannelFactory* channel_factory) {
  auto handle_it = resource_handles.find(equipment_slot);
  if (handle_it == resource_handles.end()) {
    return absl::NotFoundError(absl::StrFormat(
        "No resource handle for slot `%s` found.", equipment_slot));
  }

  const auto& handle = handle_it->second;

  return CreateAnalogDigitalIO(handle, channel_factory);
}

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> CreateAnalogDigitalIO(
    absl::string_view equipment_slot, const EquipmentPack& equipment_pack,
    const icon::ChannelFactory* channel_factory) {
  INTR_ASSIGN_OR_RETURN(const auto& handle,
                        equipment_pack.GetHandle(equipment_slot));

  return CreateAnalogDigitalIO(handle, channel_factory);
}

absl::StatusOr<std::unique_ptr<AnalogDigitalIOInterface>> CreateAnalogDigitalIO(
    const intrinsic_proto::resources::ResourceHandle& resource_handle,
    const icon::ChannelFactory* channel_factory) {
  // Find the data
  auto config_it =
      resource_handle.resource_data().find(icon::kIcon2AdioPartKey);
  if (config_it == resource_handle.resource_data().end()) {
    return absl::NotFoundError("ADIO config not found for resource handle.");
  }
  const auto& equipment_config = config_it->second;
  intrinsic_proto::icon::Icon2AdioPart adio_equipment_config;
  if (!equipment_config.contents().UnpackTo(&adio_equipment_config)) {
    return absl::InvalidArgumentError("Failed to unpack equipment config.");
  }

  switch (adio_equipment_config.target_case()) {
    case intrinsic_proto::icon::Icon2AdioPart::TargetCase::kIconTarget: {
      if (!channel_factory) {
        return absl::InvalidArgumentError(
            "Attempted to create IconAnalogDigitalIO with null channel "
            "factory.");
      }
      return IconAnalogDigitalIO::Create(
          adio_equipment_config, *channel_factory,
          resource_handle.has_connection_info()
              ? &resource_handle.connection_info()
              : nullptr);
    }
    case intrinsic_proto::icon::Icon2AdioPart::TargetCase::kGpioServiceTarget: {
      if (!channel_factory) {
        return absl::InvalidArgumentError(
            "Attempted to create IconAnalogDigitalIO with null channel "
            "factory.");
      }
      return GpioAnalogDigitalIO::Create(
          adio_equipment_config.gpio_service_target(), resource_handle.name());
    }
    case intrinsic_proto::icon::Icon2AdioPart::TargetCase::TARGET_NOT_SET:
      return absl::InvalidArgumentError(
          "Attempted to create IconAnalogDigitalIO with no target.");
  }
}

}  // namespace intrinsic::skills
