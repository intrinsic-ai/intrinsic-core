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

#include "intrinsic/scene/service/scene_object_edit_service_impl.h"

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/scene/proto/v1/scene_object_edit_service.pb.h"
#include "intrinsic/scene/service/scene_object_and_mutex.h"
#include "intrinsic/scene/util/scene_object_updates.h"
#include "intrinsic/scene/validate/scene_object_validation.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/stats/tracing_utils.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_builder_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/unique_id.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/tracer.h"

namespace intrinsic {
namespace scene_object {

using ::intrinsic_proto::scene_object::v1::CloneSceneObjectRequest;
using ::intrinsic_proto::scene_object::v1::CreateSceneObjectRequest;
using ::intrinsic_proto::scene_object::v1::DeleteSceneObjectRequest;
using ::intrinsic_proto::scene_object::v1::GetSceneObjectJournalRequest;
using ::intrinsic_proto::scene_object::v1::GetSceneObjectJournalResponse;
using ::intrinsic_proto::scene_object::v1::GetSceneObjectRequest;
using ::intrinsic_proto::scene_object::v1::SceneObjectWithMetadata;
using ::intrinsic_proto::scene_object::v1::UpdateSceneObjectRequest;

using UpdateTypeProto = ::intrinsic_proto::scene_object::v1::UpdateType;
using UpdatePolicyProto = ::intrinsic_proto::scene_object::v1::UpdatePolicy;

namespace {

UpdateType FromProto(const UpdateTypeProto& update_type) {
  switch (update_type) {
    case UpdateTypeProto::UPDATE_TYPE_OBJECT_TYPE:
      return UpdateType::kObjectType;
    case UpdateTypeProto::UPDATE_TYPE_OBJECT_INSTANCE:
      return UpdateType::kObjectInstance;
    case UpdateTypeProto::UPDATE_TYPE_UNSPECIFIED:
      return UpdateType::kDefault;
    case UpdateTypeProto::UPDATE_TYPE_EVERYTHING:
      return UpdateType::kDefault;
    default:
      return UpdateType::kDefault;
  }
}

UpdateTypeProto ToProto(const UpdateType& update_type) {
  switch (update_type) {
    case UpdateType::kObjectType:
      return UpdateTypeProto::UPDATE_TYPE_OBJECT_TYPE;
    case UpdateType::kObjectInstance:
      return UpdateTypeProto::UPDATE_TYPE_OBJECT_INSTANCE;
    case UpdateType::kDefault:
      return UpdateTypeProto::UPDATE_TYPE_EVERYTHING;
    default:
      return UpdateTypeProto::UPDATE_TYPE_UNSPECIFIED;
  }
}

UpdatePolicy FromProto(const UpdatePolicyProto& update_policy) {
  switch (update_policy) {
    case UpdatePolicyProto::UPDATE_POLICY_UNSPECIFIED:
      return UpdatePolicy::kDefault;
    case UpdatePolicyProto::UPDATE_POLICY_APPLY_ALL_OR_FAIL:
      return UpdatePolicy::kDefault;
    case UpdatePolicyProto::UPDATE_POLICY_SKIP_FAILED:
      return UpdatePolicy::kSkipFailed;
    default:
      return UpdatePolicy::kDefault;
  }
}

}  // namespace

absl::StatusOr<std::unique_ptr<SceneObjectEditServiceImpl>>
SceneObjectEditServiceImpl::CreateService() {
  return absl::WrapUnique(new SceneObjectEditServiceImpl());
}

grpc::Status SceneObjectEditServiceImpl::CreateSceneObject(
    grpc::ServerContext* context, const CreateSceneObjectRequest* request,
    SceneObjectWithMetadata* response) {
  const stats::ScopedSpan span("SceneObjectEditServiceImpl/CreateSceneObject",
                               context);
  // Fail early if the inital SceneObject is invalid
  INTR_RETURN_IF_ERROR_GRPC(
      scene_object::ValidateSceneObject(request->scene_object()))
      .LogError();

  std::string scene_object_id = WebSafeUuid();

  absl::MutexLock lock(scene_objects_mtx_);
  std::shared_ptr<service_internal::SceneObjectAndMutex>& object_ptr =
      scene_objects_[scene_object_id];

  // Regenerate a new UUID until we have a valid and available value.
  while (object_ptr != nullptr) {
    scene_object_id = WebSafeUuid();
    object_ptr = scene_objects_[scene_object_id];
  }

  // Create the new scene object.
  object_ptr = std::make_shared<service_internal::SceneObjectAndMutex>(
      request->scene_object(), FromProto(request->update_type()));

  // Fill out the response proto.
  INTR_ASSIGN_OR_RETURN_GRPC(*response,
                             SceneObjectToProto(scene_object_id, *object_ptr));

  return grpc::Status::OK;
}

grpc::Status SceneObjectEditServiceImpl::GetSceneObject(
    grpc::ServerContext* context, const GetSceneObjectRequest* request,
    SceneObjectWithMetadata* response) {
  const stats::ScopedSpan span("SceneObjectEditServiceImpl/GetSceneObject",
                               context);
  INTR_ASSIGN_OR_RETURN_GRPC(auto object_holder,
                             GetSceneObject(request->scene_object_id()));

  // Fill out the response proto.
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response,
      SceneObjectToProto(request->scene_object_id(), *object_holder));
  return grpc::Status::OK;
}

grpc::Status SceneObjectEditServiceImpl::GetSceneObjectJournal(
    grpc::ServerContext* context, const GetSceneObjectJournalRequest* request,
    GetSceneObjectJournalResponse* response) {
  const stats::ScopedSpan span(
      "SceneObjectEditServiceImpl/GetSceneObjectJournal", context);
  INTR_ASSIGN_OR_RETURN_GRPC(auto object_holder,
                             GetSceneObject(request->scene_object_id()));

  // Fill out the response proto.
  absl::ReaderMutexLock scene_object_lock(object_holder->mtx);

  auto* metadata = response->mutable_scene_object_metadata();
  metadata->set_scene_object_id(request->scene_object_id());
  metadata->set_update_type(ToProto(object_holder->update_type));
  metadata->set_revision_token(object_holder->RevisionToken());
  INTR_RETURN_IF_ERROR_GRPC(
      intrinsic::FromAbslTime(object_holder->LastUpdate(),
                              metadata->mutable_last_update()))
      .LogError();

  *response->mutable_original_scene_object() =
      object_holder->original_scene_object;
  *response->mutable_scene_object_updates() =
      object_holder->scene_object_updates;
  *response->mutable_current_scene_object() = object_holder->scene_object;

  return grpc::Status::OK;
}

grpc::Status SceneObjectEditServiceImpl::CloneSceneObject(
    grpc::ServerContext* context, const CloneSceneObjectRequest* request,
    SceneObjectWithMetadata* response) {
  const stats::ScopedSpan span("SceneObjectEditServiceImpl/CloneSceneObject",
                               context);

  absl::MutexLock lock(scene_objects_mtx_);

  // Grab a copy of the existing scene object pointer
  INTR_ASSIGN_OR_RETURN_GRPC(auto base_object_holder,
                             GetSceneObjectLocked(request->scene_object_id()));

  std::string updated_scene_object_id = WebSafeUuid();
  std::shared_ptr<service_internal::SceneObjectAndMutex>& object_holder =
      scene_objects_[updated_scene_object_id];

  while (object_holder != nullptr) {
    updated_scene_object_id = WebSafeUuid();
    object_holder = scene_objects_[updated_scene_object_id];
  }

  absl::ReaderMutexLock base_object_lock(base_object_holder->mtx);
  if (!request->revision_token().empty() &&
      base_object_holder->RevisionToken() != request->revision_token()) {
    // Since we created the new scene object entry we need to remove it here.
    scene_objects_.erase(updated_scene_object_id);

    return AbortedErrorBuilderGrpc().LogError()
           << "Scene Object with id '" << request->scene_object_id()
           << "' does not have a matching revision_token in the request";
  }

  LOG(INFO) << "Cloning scene object: " << request->scene_object_id()
            << " into " << updated_scene_object_id;

  // Copy the object holder for the new object from the base object.
  object_holder = base_object_holder->Clone(request->keep_updates_journal(),
                                            FromProto(request->update_type()));

  if (request->keep_scene_object_in_result()) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        *response, SceneObjectToProto(updated_scene_object_id, *object_holder));
  } else {
    // In the case we don't want to return the scene object, we still return
    // full metadata in the response.
    absl::ReaderMutexLock cloned_object_lock(object_holder->mtx);
    auto* metadata = response->mutable_scene_object_metadata();
    metadata->set_scene_object_id(updated_scene_object_id);
    metadata->set_update_type(ToProto(object_holder->update_type));
    metadata->set_revision_token(object_holder->RevisionToken());

    INTR_RETURN_IF_ERROR_GRPC(
        intrinsic::FromAbslTime(object_holder->LastUpdate(),
                                metadata->mutable_last_update()))
        .LogError();
  }

  return grpc::Status::OK;
}

grpc::Status SceneObjectEditServiceImpl::DeleteSceneObject(
    grpc::ServerContext* context, const DeleteSceneObjectRequest* request,
    google::protobuf::Empty* response) {
  const stats::ScopedSpan span("SceneObjectEditServiceImpl/DeleteSceneObject",
                               context);
  absl::MutexLock lock(scene_objects_mtx_);
  if (!scene_objects_.contains(request->scene_object_id())) {
    return NotFoundErrorBuilderGrpc().LogError()
           << "Scene Object with id '" << request->scene_object_id()
           << "' does not exist";
  }

  if (!request->revision_token().empty()) {
    auto object_holder = scene_objects_[request->scene_object_id()];
    absl::MutexLock object_lock(object_holder->mtx);
    if (object_holder->RevisionToken() != request->revision_token()) {
      return AbortedErrorBuilderGrpc().LogError()
             << "Scene Object with id '" << request->scene_object_id()
             << "' does not have a matching revision_token in the request";
    }
  }

  LOG(INFO) << "Deleting scene object with id: " << request->scene_object_id();
  scene_objects_.erase(request->scene_object_id());
  return grpc::Status::OK;
}

grpc::Status SceneObjectEditServiceImpl::UpdateSceneObject(
    grpc::ServerContext* context, const UpdateSceneObjectRequest* request,
    SceneObjectWithMetadata* response) {
  const stats::ScopedSpan span("SceneObjectEditServiceImpl/UpdateSceneObject",
                               context);

  if (request->revision_token().empty()) {
    return AbortedErrorBuilderGrpc().LogError()
           << "UpdateSceneObjectRequest requires a revision_token";
  }

  INTR_ASSIGN_OR_RETURN_GRPC(auto object_holder,
                             GetSceneObject(request->scene_object_id()));

  std::shared_ptr<opentelemetry::trace::Span> get_mutex_span =
      intrinsic::stats::GetTracer()->StartSpan(
          "SceneObjectEditServiceImpl/UpdateSceneObject/get_mutex");
  absl::MutexLock scene_object_lock(object_holder->mtx);
  get_mutex_span->End();

  if (object_holder->RevisionToken() != request->revision_token()) {
    return AbortedErrorBuilderGrpc().LogError()
           << "Scene Object with id '" << request->scene_object_id()
           << "' does not have a matching revision_token in the request";
  }

  SceneObjectUpdateResult result;

  {  // Apply the actual updates to the scene object.
    intrinsic::stats::ScopedSpan apply_span(
        "SceneObjectEditServiceImpl/UpdateSceneObject/apply_updates",
        span.span());
    SceneObjectUpdateOptions options = {
        .update_policy = FromProto(request->update_policy()),
        .update_type = object_holder->update_type,
    };
    INTR_ASSIGN_OR_RETURN_GRPC(
        result,
        ProcessSceneObjectUpdates(object_holder->scene_object,
                                  request->scene_object_updates(), options),
        _.LogError());
  }

  std::set<size_t> bad_update_indices;
  for (const auto& error : result.update_errors) {
    bad_update_indices.insert(error.index);
    VLOG(1) << "Skipping update because of a non fatal error for update["
            << error.index << "]: " << error.status;
  }

  // Update the current object and timestamp
  object_holder->scene_object = std::move(result.result);
  object_holder->UpdateTimestamp();
  object_holder->UpdateRevisionToken();

  // Add onto the journal of updates.
  const size_t num_updates = request->scene_object_updates().updates_size();
  for (int i = 0; i < num_updates; ++i) {
    if (!bad_update_indices.contains(i)) {
      *object_holder->scene_object_updates.add_updates() =
          request->scene_object_updates().updates(i);
    }
  }

  // Fill out the response proto.
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response,
      SceneObjectToProtoLocked(request->scene_object_id(), *object_holder));
  return grpc::Status::OK;
}

absl::StatusOr<std::shared_ptr<service_internal::SceneObjectAndMutex>>
SceneObjectEditServiceImpl::GetSceneObject(absl::string_view scene_object_id) {
  absl::ReaderMutexLock lock(scene_objects_mtx_);
  return GetSceneObjectLocked(scene_object_id);
}

absl::StatusOr<std::shared_ptr<service_internal::SceneObjectAndMutex>>
SceneObjectEditServiceImpl::GetSceneObjectLocked(
    absl::string_view scene_object_id) {
  if (scene_object_id.empty()) {
    return absl::InvalidArgumentError("scene_object_id cannot be empty");
  }

  auto itr = scene_objects_.find(scene_object_id);
  if (itr == scene_objects_.end()) {
    return intrinsic::NotFoundErrorBuilderGrpc().LogError()
           << "Could not find scene object with id: " << scene_object_id;
  }
  return itr->second;
}

absl::StatusOr<SceneObjectWithMetadata>
SceneObjectEditServiceImpl::SceneObjectToProto(
    absl::string_view scene_object_id,
    service_internal::SceneObjectAndMutex& scene_object) {
  SceneObjectWithMetadata response;

  auto* metadata = response.mutable_scene_object_metadata();
  metadata->set_scene_object_id(scene_object_id);
  metadata->set_update_type(ToProto(scene_object.update_type));

  {
    absl::ReaderMutexLock lock(scene_object.mtx);
    metadata->set_revision_token(scene_object.RevisionToken());

    INTR_RETURN_IF_ERROR(
        intrinsic::FromAbslTime(scene_object.LastUpdate(),
                                metadata->mutable_last_update()))
        .LogError();
    *response.mutable_scene_object() = scene_object.scene_object;
  }

  return response;
}

absl::StatusOr<SceneObjectWithMetadata>
SceneObjectEditServiceImpl::SceneObjectToProtoLocked(
    absl::string_view scene_object_id,
    service_internal::SceneObjectAndMutex& scene_object) {
  SceneObjectWithMetadata response;

  auto* metadata = response.mutable_scene_object_metadata();
  metadata->set_scene_object_id(scene_object_id);
  metadata->set_update_type(ToProto(scene_object.update_type));
  metadata->set_revision_token(scene_object.RevisionToken());

  INTR_RETURN_IF_ERROR(intrinsic::FromAbslTime(scene_object.LastUpdate(),
                                               metadata->mutable_last_update()))
      .LogError();
  *response.mutable_scene_object() = scene_object.scene_object;
  return response;
}

}  // namespace scene_object
}  // namespace intrinsic
