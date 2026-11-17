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

#ifndef INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_EDIT_SERVICE_IMPL_H_
#define INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_EDIT_SERVICE_IMPL_H_

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_edit_service.grpc.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_edit_service.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"
#include "intrinsic/scene/service/scene_object_and_mutex.h"

namespace intrinsic {
namespace scene_object {

// Implementation of the SceneObjectEditService.
class SceneObjectEditServiceImpl final
    : public intrinsic_proto::scene_object::v1::SceneObjectEditService::
          Service {
 public:
  // Creates a new instance of the service implementation.
  static absl::StatusOr<std::unique_ptr<SceneObjectEditServiceImpl>>
  CreateService();

  grpc::Status CreateSceneObject(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::CreateSceneObjectRequest*
          request,
      intrinsic_proto::scene_object::v1::SceneObjectWithMetadata* response)
      override;

  grpc::Status GetSceneObject(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::GetSceneObjectRequest* request,
      intrinsic_proto::scene_object::v1::SceneObjectWithMetadata* response)
      override;

  grpc::Status GetSceneObjectJournal(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::GetSceneObjectJournalRequest*
          request,
      intrinsic_proto::scene_object::v1::GetSceneObjectJournalResponse*
          response) override;

  grpc::Status CloneSceneObject(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::CloneSceneObjectRequest* request,
      intrinsic_proto::scene_object::v1::SceneObjectWithMetadata* response)
      override;

  grpc::Status DeleteSceneObject(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::DeleteSceneObjectRequest*
          request,
      google::protobuf::Empty* response) override;

  grpc::Status UpdateSceneObject(
      grpc::ServerContext* context,
      const intrinsic_proto::scene_object::v1::UpdateSceneObjectRequest*
          request,
      intrinsic_proto::scene_object::v1::SceneObjectWithMetadata* response)
      override;

 private:
  SceneObjectEditServiceImpl() = default;

  absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObjectWithMetadata>
  SceneObjectToProto(absl::string_view scene_object_id,
                     service_internal::SceneObjectAndMutex& scene_object)
      ABSL_LOCKS_EXCLUDED(scene_object.mtx);

  absl::StatusOr<intrinsic_proto::scene_object::v1::SceneObjectWithMetadata>
  SceneObjectToProtoLocked(absl::string_view scene_object_id,
                           service_internal::SceneObjectAndMutex& scene_object)
      ABSL_SHARED_LOCKS_REQUIRED(scene_object.mtx);

  absl::StatusOr<std::shared_ptr<service_internal::SceneObjectAndMutex>>
  GetSceneObject(absl::string_view scene_object_id)
      ABSL_LOCKS_EXCLUDED(scene_objects_mtx_);

  absl::StatusOr<std::shared_ptr<service_internal::SceneObjectAndMutex>>
  GetSceneObjectLocked(absl::string_view scene_object_id)
      ABSL_SHARED_LOCKS_REQUIRED(scene_objects_mtx_);

  // We use shared_ptr here so that when we call get within a method we can
  // return a shared_ptr that will stay valid even if we remove the item from
  // the map in a concurrent call.
  absl::flat_hash_map<std::string,
                      std::shared_ptr<service_internal::SceneObjectAndMutex>>
      scene_objects_ ABSL_GUARDED_BY(scene_objects_mtx_);
  absl::Mutex scene_objects_mtx_;
};

}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_EDIT_SERVICE_IMPL_H_
