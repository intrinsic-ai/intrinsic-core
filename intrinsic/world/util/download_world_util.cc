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

#include "intrinsic/world/util/download_world_util.h"

#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/geometry/storage/geometry_deserializer.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/conversion/world_from_object_proto.h"
#include "intrinsic/world/proto/collision_settings.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

using ::intrinsic::world::CreateEntityWorldFromProto;
using ::intrinsic_proto::world::CollisionSettings;
using ::intrinsic_proto::world::GetCollisionSettingsRequest;
using ::intrinsic_proto::world::ListObjectsRequest;
using ::intrinsic_proto::world::ListObjectsResponse;
using ::intrinsic_proto::world::ObjectView;
using ::intrinsic_proto::world::ObjectWorldService;

using ObjectWorldProto = ::intrinsic_proto::world::World;

absl::Status DeserializeWorldGeometries(
    World& world, const GeometryDeserializer& deserializer) {
  for (const auto& entity_id :
       world.GetTypedEntityIds<GeometryComponentType>()) {
    INTR_ASSIGN_OR_RETURN(
        auto* geo_component,
        world.GetComponentByEntityId<GeometryComponent>(entity_id));
    for (const std::string& name : geo_component->GetGeometryNames()) {
      INTR_RETURN_IF_ERROR(
          geo_component->GetGeometry(name, deserializer).status());
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<ObjectWorldProto> DownloadWorldProtoFromObjectWorldService(
    absl::string_view world_id,
    ObjectWorldService::StubInterface& object_world_service) {
  grpc::ClientContext list_ctx;
  ConfigureClientContext(&list_ctx);
  ListObjectsRequest list_objects_request;
  list_objects_request.set_world_id(std::string(world_id));
  list_objects_request.set_view(ObjectView::FULL);
  ListObjectsResponse list_objects_response;

  grpc::ClientContext col_ctx;
  ConfigureClientContext(&col_ctx);
  GetCollisionSettingsRequest get_collision_settings_request;
  get_collision_settings_request.set_world_id(std::string(world_id));
  CollisionSettings collision_settings;

  auto* async_stub = object_world_service.async();
  if (async_stub != nullptr) {
    grpc::Status list_status;
    absl::Notification list_done;
    grpc::Status col_status;
    absl::Notification col_done;

    async_stub->ListObjects(&list_ctx, &list_objects_request,
                            &list_objects_response,
                            [&list_status, &list_done](grpc::Status status) {
                              list_status = std::move(status);
                              list_done.Notify();
                            });
    async_stub->GetCollisionSettings(
        &col_ctx, &get_collision_settings_request, &collision_settings,
        [&col_status, &col_done](grpc::Status status) {
          col_status = std::move(status);
          col_done.Notify();
        });
    list_done.WaitForNotification();
    col_done.WaitForNotification();

    INTR_RETURN_IF_ERROR(ToAbslStatus(list_status));
    INTR_RETURN_IF_ERROR(ToAbslStatus(col_status));
  } else {
    INTR_RETURN_IF_ERROR(ToAbslStatus(object_world_service.ListObjects(
        &list_ctx, list_objects_request, &list_objects_response)));
    INTR_RETURN_IF_ERROR(ToAbslStatus(object_world_service.GetCollisionSettings(
        &col_ctx, get_collision_settings_request, &collision_settings)));
  }

  ObjectWorldProto world_proto;
  world_proto.mutable_world_metadata()->set_id(std::string(world_id));
  *world_proto.mutable_objects() =
      std::move(*list_objects_response.mutable_objects());
  *world_proto.mutable_collision_settings() = std::move(collision_settings);
  return world_proto;
}

absl::StatusOr<World> DownloadWorldFromObjectWorldService(
    absl::string_view world_id,
    ObjectWorldService::StubInterface& object_world_service,
    GeometryLibrary* geolib) {
  LOG(INFO) << "Downloading world proto from Object World Service...";
  INTR_ASSIGN_OR_RETURN(
      ObjectWorldProto world_proto,
      DownloadWorldProtoFromObjectWorldService(world_id, object_world_service));

  ListObjectsResponse list_objects_response;
  *list_objects_response.mutable_objects() =
      std::move(*world_proto.mutable_objects());

  LOG(INFO) << "Creating entity world from world proto...";
  INTR_ASSIGN_OR_RETURN(World world, CreateEntityWorldFromProto(
                                         list_objects_response,
                                         world_proto.collision_settings()));

  if (geolib != nullptr) {
    LOG(INFO) << "Deserializing world geometries...";
    INTR_RETURN_IF_ERROR(
        DeserializeWorldGeometries(world, geolib->Deserializer()));
  }

  return world;
}

}  // namespace intrinsic
