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

#ifndef INTRINSIC_WORLD_SERVICE_WORLD_MUTEX_H_
#define INTRINSIC_WORLD_SERVICE_WORLD_MUTEX_H_

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/thread_annotations.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/unique_id.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

// A wrapper holding a World pointer and a mutex that must be held when
// accessing it.
struct WorldAndMutex {
  explicit WorldAndMutex(World world)
      : mtx(std::make_shared<absl::Mutex>()),
        world(std::move(world)),
        world_structure_hash(intrinsic::WebSafeUuid()),
        last_update(absl::UnixEpoch()) {}

  explicit WorldAndMutex(World world, std::shared_ptr<absl::Mutex> mtx)
      : mtx(std::move(mtx)),
        world(std::move(world)),
        world_structure_hash(intrinsic::WebSafeUuid()),
        last_update(absl::UnixEpoch()) {}

  World* operator->() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mtx) { return &world; }
  const World* operator->() const ABSL_SHARED_LOCKS_REQUIRED(mtx) {
    return &world;
  }

  World& operator*() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mtx) { return world; }
  const World& operator*() const ABSL_SHARED_LOCKS_REQUIRED(mtx) {
    return world;
  }

  // TODO(stoyang): Ideally this should not require a write lock when we already
  // have the view and the user only intends to read from the view.
  absl::StatusOr<std::shared_ptr<object_world::ObjectWorld>> GetObjectWorld()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mtx) {
    if (object_world == nullptr) {
      INTR_ASSIGN_OR_RETURN(object_world,
                            object_world::ObjectWorld::CreateView(world));
    }

    return object_world;
  }

  // We clear the object world when we know that something in the entity world
  // was changed directly and not through the object world. This ensures that
  // the interface of the object world is consistent with the entity world.
  void ClearObjectWorld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mtx) {
    object_world.reset();
  }

  void UpdateStructureHash() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mtx) {
    world_structure_hash = intrinsic::WebSafeUuid();
  }

  // Updates the last update timestamp of the world. The timestamp is guaranteed
  // to be monotonic (i.e., it will not be updated to an earlier time).
  void UpdateTimestamp(absl::Time update_timestamp = absl::Now())
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mtx) {
    last_update = std::max(last_update, update_timestamp);
  }

  absl::Time LastUpdate() const ABSL_SHARED_LOCKS_REQUIRED(mtx) {
    return last_update;
  }

  std::shared_ptr<absl::Mutex> mtx;
  World world ABSL_GUARDED_BY(mtx);
  std::string world_structure_hash ABSL_GUARDED_BY(mtx);
  std::string user_tag ABSL_GUARDED_BY(mtx);
  std::unique_ptr<Publisher> publisher ABSL_GUARDED_BY(mtx);

 private:
  absl::Time last_update ABSL_GUARDED_BY(mtx);
  std::shared_ptr<object_world::ObjectWorld> object_world ABSL_GUARDED_BY(mtx);
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_WORLD_MUTEX_H_
