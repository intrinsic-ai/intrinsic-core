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

#ifndef INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_AND_MUTEX_H_
#define INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_AND_MUTEX_H_

#include <memory>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/empty.pb.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_edit_service.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_updates.pb.h"
#include "intrinsic/scene/util/scene_object_updates.h"
#include "intrinsic/util/proto/pb_hash.h"

namespace intrinsic {
namespace scene_object {
namespace service_internal {

// A holder for the scene object and it's related data, like updates and
// timestamp.
struct SceneObjectAndMutex {
  SceneObjectAndMutex(
      intrinsic_proto::scene_object::v1::SceneObject original_scene_object,
      UpdateType update_type, absl::Time last_update = absl::Now())
      : original_scene_object(original_scene_object),
        update_type(update_type),
        scene_object(original_scene_object),
        last_update(last_update) {
    UpdateRevisionToken();
  }

  SceneObjectAndMutex(
      intrinsic_proto::scene_object::v1::SceneObject original_scene_object,
      intrinsic_proto::scene_object::v1::SceneObjectUpdates
          scene_object_updates,
      intrinsic_proto::scene_object::v1::SceneObject scene_object,
      UpdateType update_type, absl::Time last_update)
      : original_scene_object(original_scene_object),
        update_type(update_type),
        scene_object_updates(scene_object_updates),
        scene_object(scene_object),
        last_update(last_update) {
    UpdateRevisionToken();
  }

  // Returns a copy of this SceneObjectAndMutex with a new UpdateType while
  // optionally keeping the journal updates.
  std::unique_ptr<SceneObjectAndMutex> Clone(bool keep_journal,
                                             UpdateType new_update_type) const
      ABSL_SHARED_LOCKS_REQUIRED(mtx) {
    if (keep_journal) {
      return std::make_unique<SceneObjectAndMutex>(
          original_scene_object, scene_object_updates, scene_object,
          new_update_type, last_update);
    } else {
      return std::make_unique<SceneObjectAndMutex>(
          scene_object, new_update_type, last_update);
    }
  }

  void UpdateTimestamp(absl::Time update_timestamp = absl::Now())
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mtx) {
    last_update = update_timestamp;
  }

  absl::Time LastUpdate() const ABSL_SHARED_LOCKS_REQUIRED(mtx) {
    return last_update;
  }

  void UpdateRevisionToken() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mtx) {
    revision_token = absl::StrCat(pb_hash{}(scene_object));
  }

  const std::string& RevisionToken() const ABSL_SHARED_LOCKS_REQUIRED(mtx) {
    return revision_token;
  }

  // The starting point for this object, defined when it was created.
  const intrinsic_proto::scene_object::v1::SceneObject original_scene_object;
  // The update type used for the duration of the lifetime of this object.
  const UpdateType update_type;

  // The updates applied to the original_scene_object since its creation.
  intrinsic_proto::scene_object::v1::SceneObjectUpdates scene_object_updates
      ABSL_GUARDED_BY(mtx);

  // The current computed scene object. Equivalent to applying
  // scene_object_updates to the original_scene_object.
  intrinsic_proto::scene_object::v1::SceneObject scene_object
      ABSL_GUARDED_BY(mtx);

  // Mutex to ensure consistent data updates.
  mutable absl::Mutex mtx;

 private:
  absl::Time last_update ABSL_GUARDED_BY(mtx);
  std::string revision_token ABSL_GUARDED_BY(mtx);
};

}  // namespace service_internal
}  // namespace scene_object
}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_SERVICE_SCENE_OBJECT_AND_MUTEX_H_
