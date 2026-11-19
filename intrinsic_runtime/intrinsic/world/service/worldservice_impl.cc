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

#include "intrinsic/world/service/worldservice_impl.h"

#include <cstddef>
#include <cstdint>
#include <future>  // NOLINT(build/c++11)
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/io.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/ret_check_grpc.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/world/cartesian_kinematic_view.h"
#include "intrinsic/world/dof_kinematic_view.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/service/world_service.pb.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/util/entity_search_util.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

using ::intrinsic_proto::world::EntitySearchCriteria;
using ::intrinsic_proto::world::internal::EntityWithMetadata;
using ::intrinsic_proto::world::internal::GetIkSolutionRequest;
using ::intrinsic_proto::world::internal::SingleIKSolution;

absl::StatusOr<std::unique_ptr<WorldServiceImpl>>
WorldServiceImpl::CreateService(
    WorldStorage& world_storage,
    std::optional<ObjectWorldCompatibilityFunc> compat_func,
    GeometryLibrary& geo_lib) {
  std::future<WorldStorage*> instant_world_storage = std::async(
      std::launch::deferred, [&world_storage]() { return &world_storage; });
  std::future<GeometryLibrary*> instant_geo_lib =
      std::async(std::launch::deferred, [&geo_lib]() { return &geo_lib; });
  return absl::WrapUnique(new WorldServiceImpl(std::move(instant_world_storage),
                                               compat_func,
                                               std::move(instant_geo_lib)));
}

absl::StatusOr<std::unique_ptr<WorldServiceImpl>>
WorldServiceImpl::CreateService(
    std::future<WorldStorage*> world_storage,
    std::optional<ObjectWorldCompatibilityFunc> compat_func,
    std::future<GeometryLibrary*> geo_lib) {
  return absl::WrapUnique(new WorldServiceImpl(
      std::move(world_storage), compat_func, std::move(geo_lib)));
}

WorldStorage* WorldServiceImpl::WorldStore() {
  absl::MutexLock l(world_storage_mutex_);
  if (world_storage_ == nullptr && world_storage_future_.valid()) {
    world_storage_ = world_storage_future_.get();
    CHECK(world_storage_ != nullptr) << "WorldStorage future returned nullptr";
  }
  return world_storage_;
}

GeometryLibrary* WorldServiceImpl::GeoLib() {
  absl::MutexLock l(geo_lib_mutex_);
  if (geo_lib_ == nullptr && geo_lib_future_.valid()) {
    geo_lib_ = geo_lib_future_.get();
    CHECK(geo_lib_ != nullptr) << "GeometryLibrary future returned nullptr";
  }
  return geo_lib_;
}

absl::StatusOr<EntityWithMetadata> WorldServiceImpl::GetEntity(
    absl::string_view world_id, const EntitySearchCriteria& entity) {
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<WorldAndMutex> world_ptr,
                        WorldStore()->GetWorld(world_id));
  absl::ReaderMutexLock lock(*world_ptr->mtx);
  const World& world = world_ptr->world;

  INTR_ASSIGN_OR_RETURN(EntityId entity_id, GetSingleEntity(world, entity));
  INTR_ASSIGN_OR_RETURN(const WorldEntity* world_entity,
                        world.GetEntityById(entity_id));
  return EntityToProto(world_id, entity_id, *world_entity);
}

absl::StatusOr<EntityWithMetadata> WorldServiceImpl::EntityToProto(
    absl::string_view world_id, EntityId entity_id, const WorldEntity& entity) {
  EntityWithMetadata response;
  response.set_world_id(world_id);
  response.set_entity_id(entity_id.value());

  INTR_ASSIGN_OR_RETURN(*response.mutable_entity_data(), entity.ToProto());
  return response;
}

grpc::Status WorldServiceImpl::GetIkSolution(
    grpc::ServerContext* context, const GetIkSolutionRequest* request,
    SingleIKSolution* response) {
  const stats::ScopedSpan span("WorldService/GetIkSolution", context);

  INTR_ASSIGN_OR_RETURN_GRPC(std::shared_ptr<WorldAndMutex> world_ptr,
                             WorldStore()->GetWorld(request->world_id()));
  absl::WriterMutexLock lock(*world_ptr->mtx);

  INTR_ASSIGN_OR_RETURN_GRPC(
      AttachmentEntityId root_id,
      (*world_ptr)
          ->ValidateEntity<AttachmentEntityId>(EntityId(request->root_id())),
      _.LogError() << " for root entity");

  INTR_ASSIGN_OR_RETURN_GRPC(
      AttachmentEntityId tip_id,
      (*world_ptr)
          ->ValidateEntity<AttachmentEntityId>(EntityId(request->tip_id())),
      _.LogError() << " for tip entity");

  INTR_ASSIGN_OR_RETURN_GRPC(
      std::vector<std::unique_ptr<CartesianKinematicView>> cart_views,
      (*world_ptr)->GetCartesianKinematicViews(root_id, tip_id));
  response->set_world_id(request->world_id());

  INTR_ASSIGN_OR_RETURN_GRPC(Pose3d pose, FromProto(request->root_t_tip()),
                             _.LogError());

  // Loop through all the possible cart views and check each one for a possible
  // solution to the inverse kinematics based on the given solvers. The first
  // answer is the one we will return.
  for (const std::unique_ptr<CartesianKinematicView>& cart_view : cart_views) {
    if (cart_view == nullptr) {
      continue;
    }

    absl::StatusOr<std::string> solver_key = cart_view->GetIKSolverKey();
    if (!solver_key.ok() || solver_key->empty()) {
      if (cart_views.size() == 1) {
        return NotFoundErrorBuilderGrpc().LogError()
               << "No solver for given chain";
      }
      continue;
    }

    std::shared_ptr<DofKinematicView> dof_view = cart_view->GetDofView();
    if (dof_view == nullptr) {
      return InternalErrorBuilderGrpc().LogError()
             << "CartesianKinematicView has null DofKinematicView";
    }

    auto ik_solutions =
        cart_view->GetIkSolutions(pose, dof_view->GetDofValues());

    if (!ik_solutions) {
      if (cart_views.size() == 1) {
        return NotFoundErrorBuilderGrpc().LogError()
               << "Could not find viable IK solution";
      }
      continue;
    }

    std::optional<eigenmath::VectorXd> single_solution =
        ik_solutions->GetSingleSolution();
    if (!single_solution.has_value()) {
      if (cart_views.size() == 1) {
        return NotFoundErrorBuilderGrpc().LogError()
               << "Could not find viable IK solution";
      }
      continue;
    }

    const eigenmath::VectorXd& dof_vals = *single_solution;
    if (request->update_robot_to_solution()) {
      LOG(INFO) << "Robot joint values updated to IK solution.";
      // Set the IK solution to the dof view to jog the robot via FK.
      INTR_RETURN_IF_ERROR_GRPC(dof_view->SetDofValues(dof_vals, true));
      world_ptr->UpdateTimestamp();

      WorldStore()->MarkWorldAsChanged(request->world_id(), world_ptr,
                                       /*has_state_change=*/true);
    }

    // Set the result.
    const std::vector<JointEntityId>& dofs = dof_view->GetJointEntityIds();
    if (static_cast<int>(dofs.size()) != dof_vals.size()) {
      return InternalErrorBuilderGrpc().LogError()
             << "Mismatch between DOF count (" << dofs.size()
             << ") and IK solution vector size (" << dof_vals.size() << ")";
    }
    for (size_t i = 0; i < dofs.size(); ++i) {
      (*response->mutable_joint_vals())[dofs[i].value()] = dof_vals[i];
    }
    return grpc::Status::OK;
  }

  return NotFoundErrorBuilderGrpc().LogError()
         << "Could not find viable IK solution";
}

}  // namespace intrinsic
