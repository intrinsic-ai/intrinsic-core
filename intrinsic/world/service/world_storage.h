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

#ifndef INTRINSIC_WORLD_SERVICE_WORLD_STORAGE_H_
#define INTRINSIC_WORLD_SERVICE_WORLD_STORAGE_H_

#include <functional>
#include <future>  // NOLINT(build/c++11)
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/util/thread/periodic.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

using ObjectWorldCompatibilityFunc = std::function<absl::Status(const World&)>;

// Manages the in-memory worlds of the world service, and takes care of syncing
// the worlds to disk to guard against crashes/restarts.
class WorldStorage {
 public:
  struct InitWorld {
    std::string world_id;
    World world;
  };

  // Whether to load worlds from provided path.
  enum class LoadWorlds {
    kYes,
    kNo,
  };

  ~WorldStorage();

  // Creates an instance with an initial set of worlds.
  //
  // Copies all GZF files under 'seed_worlds_path' to 'worlds_path', not
  // overwriting existing files, and then loads all GZF files under
  // 'worlds_path' using the file basename as world id. Any changes to worlds
  // later on will be synced out to the GZFs in 'worlds_path'.
  // Use 'init_worlds' to add additional in-memory worlds. If a world id in
  // 'init_worlds' matches a file basename in 'seed_worlds_path', the world from
  // file takes precedence.
  // `load_worlds` controls whether to load worlds from the provided path. If
  // set to kNo, the provided path is only be used to save the worlds.
  // If 'worlds_path' is empty, worlds are neither loaded from nor saved to
  // disk and only live in memory.
  // Resource instances from 'resource_set_path' will be composed into the
  // initial worlds during creation.
  static absl::StatusOr<std::unique_ptr<WorldStorage>> Create(
      absl::string_view worlds_path, GeometryLibrary& geo_lib,
      std::optional<ObjectWorldCompatibilityFunc> compat_func,
      LoadWorlds load_worlds, std::vector<InitWorld> init_worlds = {});

  static absl::StatusOr<std::unique_ptr<WorldStorage>> Create(
      absl::string_view worlds_path, std::future<GeometryLibrary*> geo_lib,
      std::optional<ObjectWorldCompatibilityFunc> compat_func,
      LoadWorlds load_worlds, std::vector<InitWorld> init_worlds = {});

  // Returns true if the passed world_id can be overwritten with a new world
  // that is copied into it.
  bool ValidCloneDestination(absl::string_view world_id) const;

  // Adds a world after this instance has been initialized. Returns a pointer to
  // the newly added, lockable world. If skip_compat_check is true, the
  // compatibility check will be skipped even if the function was supplied at
  // construction.
  absl::StatusOr<std::shared_ptr<WorldAndMutex>> AddWorld(
      absl::string_view world_id, World world, bool skip_compat_check)
      ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // Adds a world after this instance has been initialized. Returns a pointer to
  // the newly added, lockable world. The caller must hold worlds_mtx_. If
  // skip_compat_check is true, the compatibility check will be skipped even if
  // the function was supplied at construction.
  absl::StatusOr<std::shared_ptr<WorldAndMutex>> AddWorldLocked(
      absl::string_view world_id, World world, bool skip_compat_check)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(worlds_mtx_);

  // Returns the world with given id, and replaces it in storage with the given
  // world. The caller must hold worlds_mtx_. Returns NotFoundError if the given
  // world does not exist. If skip_compat_check is true, the compatibility
  // check will be skipped even if the function was supplied at construction.
  absl::StatusOr<std::shared_ptr<WorldAndMutex>> ExchangeWorldLocked(
      absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world,
      bool skip_compat_check) ABSL_EXCLUSIVE_LOCKS_REQUIRED(worlds_mtx_);

  // Swaps the worlds associated with the given ids. The caller must hold
  // worlds_mtx_. Returns NotFoundError if either world does not exist.
  absl::Status SwapWorldLocked(absl::string_view id_a, absl::string_view id_b)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(worlds_mtx_);

  // Returns the lockable world corresponding to the given world id.
  // This is a variant of GetWorld() which can be used while already holding a
  // lock on 'worlds_mtx_'.
  absl::StatusOr<std::shared_ptr<WorldAndMutex>> GetWorldLocked(
      absl::string_view world_id) const ABSL_SHARED_LOCKS_REQUIRED(worlds_mtx_);

  // Returns the lockable world corresponding to the given world id.
  absl::StatusOr<std::shared_ptr<WorldAndMutex>> GetWorld(
      absl::string_view world_id) ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // Returns all currently stored worlds and their ids.
  WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>> GetAllWorlds()
      ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // Deletes the world with the given id in-memory and on disk.
  absl::Status DeleteWorld(absl::string_view world_id)
      ABSL_LOCKS_EXCLUDED(worlds_mtx_, flush_mtx_);

  // Marks the world id to be written to disk later.
  void MarkWorldForSaving(absl::string_view world_id,
                          const std::shared_ptr<WorldAndMutex>& world)
      ABSL_SHARED_LOCKS_REQUIRED(world->mtx) ABSL_LOCKS_EXCLUDED(flush_mtx_);

  void MarkWorldAsChanged(absl::string_view world_id,
                          const std::shared_ptr<WorldAndMutex>& world,
                          bool has_state_change)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx)
          ABSL_LOCKS_EXCLUDED(callbacks_mtx_);

  // Spins up a thread to periodically publish TF frames from the belief world.
  absl::Status StartTfPublisher();

  // The OnAddWorldCallback callback is called when a world is added to this
  // store. If this returns a non-OK status, the world will not be added to the
  // store.
  //
  // Must not lock any mutex on WorldStorage, it will be called while holding an
  // exclusive lock on the given WorldAndMutex.
  using OnAddWorldCallback = absl::AnyInvocable<absl::Status(
      absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world)>;

  // Sets the callback to be used when a world is added to the store. If
  // call_for_existing is true, then the callback is called for each existing
  // world before returning from the method.
  absl::Status SetOnAddWorldCallback(OnAddWorldCallback cb,
                                     bool call_for_existing = false)
      ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // Clears the callback set by SetOnAddWorldCallback, if any.
  void ClearOnAddWorldCallback() ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // The OnDeleteWorldCallback callback is called when a world is deleted from
  // this store. Once the callback returns the shared_ptr to the world will be
  // released from the store.
  //
  // Must not lock any mutex on WorldStorage, it will be called while holding an
  // exclusive lock on the given WorldAndMutex.
  using OnDeleteWorldCallback = absl::AnyInvocable<void(
      absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world)>;

  // Sets the callback to be used when a world is deleted from the store.
  void SetOnDeleteWorldCallback(OnDeleteWorldCallback cb)
      ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // Clears the callback set by SetOnDeleteWorldCallback, if any.
  void ClearOnDeleteWorldCallback() ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // The OnWorldChangeCallback callback is called when a modification is made to
  // a world held by this store. This occurs through the MarkWorldForSaving
  // method call. The bool has_state_change will be set if this world
  // modification incorporates a state change like modifying the dof or pose
  // values of entities.
  //
  // Must not lock any mutex on WorldStorage, it will be called while holding a
  // shared lock on the given WorldAndMutex.
  using OnWorldChangeCallback = absl::AnyInvocable<void(
      absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world,
      bool has_state_change)>;

  // Sets the callback to be used when a world is changed.
  void SetOnWorldChangeCallback(OnWorldChangeCallback cb)
      ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // Clears the callback set by SetOnWorldChangeCallback, if any.
  void ClearOnWorldChangeCallback() ABSL_LOCKS_EXCLUDED(worlds_mtx_);

  // TODO(stoyang): This should really be private.
  // Returns the file path based on 'worlds_path_' for the given world_id.
  std::string GetFilepathForWorldId(absl::string_view world_id) const;

 private:
  // Returns the file path based on 'worlds_path_' for the given world_id.
  std::string GetProtoFilepathForWorldId(absl::string_view world_id) const;

  // Returns true if worlds are persisted to disk, i.e., a worlds path is set
  // and saving is enabled via flag.
  bool ShouldSaveToDisk() const;

  // Performs a batch flush of all the pending saves
  void FlushImpl() ABSL_LOCKS_EXCLUDED(flush_mtx_, worlds_mtx_);

  WorldStorage(
      absl::string_view worlds_path, GeometryLibrary& geo_lib,
      std::optional<ObjectWorldCompatibilityFunc> compat_func,
      WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>> worlds);

  // The path to use when loading worlds.
  std::string worlds_path_;

  // Geometry storage to use when flushing protos to disk.
  GeometryLibrary& geo_lib_;

  // Callback for checking if a World instance is compatible with the object
  // world view.
  std::optional<ObjectWorldCompatibilityFunc> compat_func_;

 public:
  // CAUTION: Avoid adding new accesses from the outside. The public
  // accessibility of this mutex is a leftover from when WorldStorage was
  // inlined into WorldServiceImpl.  Eventually, we have to reduce locks on this
  // mutex so that WorldStorage can become self-contained, e.g., by not locking
  // this mutex for the duration of entire requests.
  // Note that we cannot have a public getter since this would prevent the use
  // of absl::MutexLock with the thread-annotated member functions above.
  absl::Mutex worlds_mtx_;

 private:
  WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>> worlds_
      ABSL_GUARDED_BY(worlds_mtx_);

  absl::Mutex flush_mtx_;
  WorldHashSet<std::string> ids_to_flush_ ABSL_GUARDED_BY(flush_mtx_);
  WorldHashSet<std::string> ids_to_delete_ ABSL_GUARDED_BY(flush_mtx_);
  intrinsic::PeriodicOperation world_flusher_;

  absl::Mutex callbacks_mtx_;

  absl::Status MaybeInvokeOnAddCallback(
      absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx)
          ABSL_LOCKS_EXCLUDED(callbacks_mtx_);
  OnAddWorldCallback on_add_cb_ ABSL_GUARDED_BY(callbacks_mtx_);

  void MaybeInvokeOnDeleteCallback(absl::string_view world_id,
                                   const std::shared_ptr<WorldAndMutex>& world)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx)
          ABSL_LOCKS_EXCLUDED(callbacks_mtx_);
  OnDeleteWorldCallback on_delete_cb_ ABSL_GUARDED_BY(callbacks_mtx_);

  void MaybeInvokeOnChangeCallback(absl::string_view world_id,
                                   const std::shared_ptr<WorldAndMutex>& world,
                                   bool has_state_change)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(world->mtx)
          ABSL_LOCKS_EXCLUDED(callbacks_mtx_);
  OnWorldChangeCallback on_change_cb_ ABSL_GUARDED_BY(callbacks_mtx_);
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_WORLD_STORAGE_H_
