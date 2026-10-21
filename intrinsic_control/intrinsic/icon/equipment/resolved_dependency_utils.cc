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

#include "intrinsic/icon/equipment/resolved_dependency_utils.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/interface_utils.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/icon/proto/v1/service.grpc.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/util/object_reference_utils.h"

namespace intrinsic {
namespace icon {
namespace {

std::string IconApiInterfaceUri() {
  return absl::StrCat(assets::kGrpcUriPrefix,
                      intrinsic_proto::icon::v1::IconApi::service_full_name());
}

std::string FormatAvailablePositionParts(
    const IconResolvedDependency::HardwareResourceNameByPartName&
        position_parts) {
  std::vector<std::string> formatted_parts;
  formatted_parts.reserve(position_parts.size());
  for (const auto& [part_name, object_name] : position_parts) {
    formatted_parts.push_back(
        absl::StrCat("(", part_name, ", ", object_name, ")"));
  }
  std::sort(formatted_parts.begin(), formatted_parts.end());
  return absl::StrJoin(formatted_parts, ", ");
}

}  // namespace

absl::StatusOr<IconResolvedDependency> ConnectToIconResolvedDependency(
    const intrinsic_proto::assets::v1::ResolvedDependency& resolved_dependency,
    absl::Duration timeout) {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<grpc::Channel> grpc_channel,
                        assets::dependencies::Connect(
                            resolved_dependency, IconApiInterfaceUri(),
                            connect::UnlimitedMessageSizeGrpcChannelArgs()));
  INTR_RETURN_IF_ERROR(connect::WaitForChannelReady(grpc_channel, timeout));

  grpc::ClientContext ctx;
  ctx.set_deadline(absl::ToChronoTime(absl::Now() + timeout));
  auto stub = intrinsic_proto::icon::v1::IconApi::NewStub(grpc_channel);
  intrinsic_proto::icon::v1::GetConfigResponse response;
  INTR_RETURN_IF_ERROR(ToAbslStatus(stub->GetConfig(
      &ctx, intrinsic_proto::icon::v1::GetConfigRequest(), &response)));

  IconResolvedDependency out;
  out.channel = std::make_shared<intrinsic::Channel>(grpc_channel);
  for (const auto& part_config : response.part_configs()) {
    if (part_config.part_type_name().empty()) {
      return absl::InternalError(
          absl::StrCat("ICON server returned empty part_type_name for part '",
                       part_config.name(), "'."));
    }
    if (part_config.hardware_resource_name().empty()) {
      return absl::InternalError(absl::StrCat(
          "ICON server returned empty hardware_resource_name for part '",
          part_config.name(), "'."));
    }
    out.parts[part_config.part_type_name()][part_config.name()] =
        part_config.hardware_resource_name();
  }

  return out;
}

absl::StatusOr<PositionPartWithObject>
IconResolvedDependency::GetPositionPartAndKinematicObject(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::ObjectReference& arm_part) const {
  const auto it = parts.find(kHalArmPartTypeName);
  if (it == parts.end() || it->second.empty()) {
    return absl::InvalidArgumentError(
        "The given ICON instance does not have any position parts.");
  }
  const auto& position_parts = it->second;

  if (IsEmptyObjectReference(arm_part)) {
    if (position_parts.size() > 1) {
      return absl::InvalidArgumentError(absl::StrCat(
          "No arm part object reference was given, and the given ICON instance "
          "has multiple. Please specify an arm part object reference to select "
          "one part to use. Available (part_name, object_name) pairs for the "
          "selected ICON instance are: ",
          FormatAvailablePositionParts(position_parts), "."));
    }
    const auto& [part_name, hardware_resource_name] = *position_parts.begin();
    INTR_ASSIGN_OR_RETURN(
        world::KinematicObject kinematic_object,
        world.GetKinematicObject(WorldObjectName(hardware_resource_name)));
    return PositionPartWithObject{
        .part_name = part_name,
        .kinematic_object = std::move(kinematic_object),
    };
  }

  INTR_ASSIGN_OR_RETURN(world::KinematicObject kinematic_object,
                        world.GetKinematicObject(arm_part));
  const std::string object_name = kinematic_object.Name().value();
  for (const auto& [current_part_name, current_object_name] : position_parts) {
    if (current_object_name == object_name) {
      return PositionPartWithObject{
          .part_name = current_part_name,
          .kinematic_object = std::move(kinematic_object),
      };
    }
  }
  return absl::InvalidArgumentError(absl::StrCat(
      "Could not find a position part corresponding to object with name '",
      object_name, "'. Please check you have the right object and ICON ",
      "instance selected. Available (part_name, object_name) pairs for the ",
      "selected ICON instance are: ",
      FormatAvailablePositionParts(position_parts), "."));
}

}  // namespace icon
}  // namespace intrinsic
