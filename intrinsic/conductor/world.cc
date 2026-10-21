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

#include "intrinsic/conductor/world.h"

#include <string>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/empty.pb.h"
#include "google/rpc/status.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/proto/v1alpha1/asset_info_internal.grpc.pb.h"
#include "intrinsic/assets/proto/v1alpha1/asset_info_internal.pb.h"
#include "intrinsic/conductor/proto/wrapper_bridge.pb.h"
#include "intrinsic/resources/proto/geometric_resource_data.pb.h"
#include "intrinsic/util/status/status.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"

namespace intrinsic::conductor {

namespace {

using ::intrinsic_proto::world::CreateWorldFromResourceSetDataRequest;
using ::intrinsic_proto::world::CreateWorldFromResourceSetDataResponse;

// Converts the status of a composition error into the bridge status proto.
void SetError(const google::rpc::Status& status,
              intrinsic_proto::StatusProto& error) {
  error.set_code(status.code());
  error.set_message(status.message());
}

}  // namespace

absl::StatusOr<internal::UpdateWorldFromResourceSetResult> CreateOrUpdateWorld(
    absl::string_view world_id,
    intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface&
        asset_info_internal,
    intrinsic_proto::world::ObjectWorldService::Service& world_service,
    bool skip_invalid_world_updates,
    absl::Duration asset_info_internal_timeout) {
  intrinsic_proto::resources::GeometricResourceSetData resource_data;
  {
    grpc::ClientContext context;
    if (asset_info_internal_timeout != absl::InfiniteDuration()) {
      context.set_deadline(
          absl::ToChronoTime(absl::Now() + asset_info_internal_timeout));
    }
    INTR_RETURN_IF_ERROR(
        intrinsic::ToAbslStatus(asset_info_internal.GetGeometricResourceSetData(
            &context,
            intrinsic_proto::assets::v1alpha1::
                GetGeometricResourceSetDataRequest(),
            &resource_data)))
        << "GetGeometricResourceSetData failed";
  }

  CreateWorldFromResourceSetDataRequest create_request;
  *create_request.mutable_resource_instance_data() =
      std::move(*resource_data.mutable_instance_data());
  *create_request.mutable_update_world_resources() =
      std::move(*resource_data.mutable_updates());
  create_request.set_update_policy(skip_invalid_world_updates
                                       ? CreateWorldFromResourceSetDataRequest::
                                             UPDATE_POLICY_SKIP_FAILED_UPDATES
                                       : CreateWorldFromResourceSetDataRequest::
                                             UPDATE_POLICY_FAIL_ON_FIRST_ERROR);

  CreateWorldFromResourceSetDataResponse create_response;
  {
    grpc::ServerContext context;
    INTR_RETURN_IF_ERROR(
        intrinsic::ToAbslStatus(world_service.CreateWorldFromResourceSetData(
            &context, &create_request, &create_response)))
        << "CreateWorldFromResourceSetData failed";
  }

  const std::string& temp_world_id = create_response.metadata().id();
  if (temp_world_id.empty()) {
    return absl::InternalError(
        "CreateWorldFromResourceSetData returned an empty world ID");
  }

  // Deletes the temporary world on every exit path.
  absl::Cleanup delete_temp_world = [&world_service, &temp_world_id]() {
    grpc::ServerContext context;
    intrinsic_proto::world::DeleteWorldRequest request;
    request.set_world_id(temp_world_id);
    google::protobuf::Empty response;
    if (grpc::Status status =
            world_service.DeleteWorld(&context, &request, &response);
        !status.ok()) {
      LOG(WARNING) << "Failed to delete temporary world '" << temp_world_id
                   << "': " << status.error_message();
    }
  };

  internal::UpdateWorldFromResourceSetResult result;
  for (const auto& composition_error : create_response.composition_errors()) {
    switch (composition_error.problem_case()) {
      case CreateWorldFromResourceSetDataResponse::CompositionError::
          kProblemUpdate: {
        LOG(INFO) << "Skipped world update at index "
                  << composition_error.problem_update().index()
                  << " for world '" << world_id
                  << "': " << composition_error.status().message();
        auto* skipped = result.add_skipped_updates();
        *skipped->mutable_update() =
            composition_error.problem_update().update();
        SetError(composition_error.status(), *skipped->mutable_error());
        break;
      }
      case CreateWorldFromResourceSetDataResponse::CompositionError::
          kProblemResourceName: {
        LOG(INFO) << "Skipped scene object '"
                  << composition_error.problem_resource_name()
                  << "' for world '" << world_id
                  << "': " << composition_error.status().message();
        auto* skipped = result.add_skipped_scene_objects();
        skipped->set_scene_object_name(
            composition_error.problem_resource_name());
        SetError(composition_error.status(), *skipped->mutable_error());
        break;
      }
      case CreateWorldFromResourceSetDataResponse::CompositionError::
          PROBLEM_NOT_SET:
        return absl::InternalError(absl::StrCat(
            "CreateWorldFromResourceSetData returned a composition error "
            "without a problem: ",
            composition_error.status().message()));
    }
  }

  {
    grpc::ServerContext context;
    intrinsic_proto::world::CloneWorldRequest request;
    request.set_world_id(temp_world_id);
    request.set_cloned_world_id(world_id);
    request.set_allow_overwrite(true);
    intrinsic_proto::world::WorldMetadata response;
    INTR_RETURN_IF_ERROR(intrinsic::ToAbslStatus(
        world_service.CloneWorld(&context, &request, &response)))
        << "CloneWorld from '" << temp_world_id << "' to '" << world_id
        << "' failed";
  }

  return result;
}

}  // namespace intrinsic::conductor
