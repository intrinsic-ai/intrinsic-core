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

#include "intrinsic/world/service/world_storage.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

absl::StatusOr<std::unique_ptr<WorldStorage>> WorldStorage::Create(
    std::optional<ObjectWorldCompatibilityFunc> compat_func,
    std::vector<InitWorld> init_worlds) {
  WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>> worlds;

  for (InitWorld& init_world : init_worlds) {
    if (compat_func.has_value()) {
      INTR_RETURN_IF_ERROR((*compat_func)(init_world.world))
          << "Failed to apply compatibility function to init world";
    }

    worlds[init_world.world_id] =
        std::make_unique<WorldAndMutex>(std::move(init_world.world));
  }

  return absl::WrapUnique(
      new WorldStorage(std::move(compat_func), std::move(worlds)));
}

WorldStorage::WorldStorage(
    std::optional<ObjectWorldCompatibilityFunc> compat_func,
    WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>> worlds)
    : compat_func_(std::move(compat_func)), worlds_(std::move(worlds)) {}

bool WorldStorage::ValidCloneDestination(absl::string_view world_id) const {
  // World IDs which may be passed as cloned_world_id for CloneWorld
  static const auto* const kValidCloneWorlds =
      new WorldHashSet<absl::string_view>({"world", "sim_world",
                                           "pre-execution-snapshot",
                                           "init_world", "save_monitor"});

  return kValidCloneWorlds->contains(world_id);
}

absl::StatusOr<std::shared_ptr<WorldAndMutex>> WorldStorage::AddWorld(
    absl::string_view world_id, World world, bool skip_compat_check) {
  const stats::ScopedSpan span("WorldStorage/AddWorld");

  if (!skip_compat_check && compat_func_.has_value()) {
    INTR_RETURN_IF_ERROR((*compat_func_)(world));
  }

  absl::WriterMutexLock lock(worlds_mtx_);
  std::shared_ptr<WorldAndMutex>& world_ptr = worlds_[world_id];

  if (world_ptr != nullptr) {
    return AlreadyExistsErrorBuilder().LogError()
           << "World with id '" << world_id << "' already exists";
  }

  std::shared_ptr<WorldAndMutex> result =
      std::make_unique<WorldAndMutex>(std::move(world));
  absl::MutexLock world_lock(*result->mtx);
  INTR_RETURN_IF_ERROR(MaybeInvokeOnAddCallback(world_id, result));

  // Actually performs the add to the store after the callbacks are called.
  world_ptr = result;
  return result;
}

absl::StatusOr<std::shared_ptr<WorldAndMutex>> WorldStorage::AddWorldLocked(
    absl::string_view world_id, World world, bool skip_compat_check) {
  const stats::ScopedSpan span("WorldStorage/AddWorldLocked");

  if (!skip_compat_check && compat_func_.has_value()) {
    INTR_RETURN_IF_ERROR((*compat_func_)(world));
  }

  std::shared_ptr<WorldAndMutex>& world_ptr = worlds_[world_id];

  if (world_ptr != nullptr) {
    return AlreadyExistsErrorBuilder().LogError()
           << "World with id '" << world_id << "' already exists";
  }

  std::shared_ptr<WorldAndMutex> result =
      std::make_unique<WorldAndMutex>(std::move(world));

  absl::MutexLock lock(*result->mtx);
  INTR_RETURN_IF_ERROR(MaybeInvokeOnAddCallback(world_id, result));

  // Actually performs the add to the store after the callbacks are called.
  world_ptr = result;

  return result;
}

absl::StatusOr<std::shared_ptr<WorldAndMutex>>
WorldStorage::ExchangeWorldLocked(absl::string_view world_id,
                                  const std::shared_ptr<WorldAndMutex>& world,
                                  bool skip_compat_check) {
  auto itr = worlds_.find(world_id);
  if (itr == worlds_.end()) {
    return NotFoundErrorBuilder()
           << "World with id '" << world_id << "' does not exist.";
  }

  if (!skip_compat_check && compat_func_.has_value()) {
    absl::MutexLock l(*world->mtx);
    INTR_RETURN_IF_ERROR((*compat_func_)(world->world));
  }

  return std::exchange(itr->second, world);
}

absl::Status WorldStorage::SwapWorldLocked(absl::string_view id_a,
                                           absl::string_view id_b) {
  auto iter_a = worlds_.find(id_a);
  if (iter_a == worlds_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Could not find world with id: " << id_a;
  }
  auto iter_b = worlds_.find(id_b);
  if (iter_b == worlds_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Could not find world with id: " << id_b;
  }
  if (iter_a != iter_b) {
    std::swap(iter_a->second, iter_b->second);
  }

  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<WorldAndMutex>> WorldStorage::GetWorldLocked(
    absl::string_view world_id) const {
  auto itr = worlds_.find(world_id);
  if (itr == worlds_.end()) {
    return intrinsic::NotFoundErrorBuilder()
           << "Could not find world with id: " << world_id;
  }

  return itr->second;
}

absl::StatusOr<std::shared_ptr<WorldAndMutex>> WorldStorage::GetWorld(
    absl::string_view world_id) {
  absl::ReaderMutexLock lock(worlds_mtx_);
  return GetWorldLocked(world_id);
}

WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>>
WorldStorage::GetAllWorlds() {
  absl::ReaderMutexLock lock(worlds_mtx_);
  return worlds_;
}

absl::Status WorldStorage::DeleteWorld(absl::string_view world_id) {
  std::shared_ptr<WorldAndMutex> deleted_world;

  {
    // We lock at the top to ensure that no one is making modifications while we
    // delete a world.
    absl::WriterMutexLock world_lock(worlds_mtx_);
    auto it = worlds_.find(world_id);
    if (it == worlds_.end()) {
      return absl::NotFoundError(
          absl::StrFormat("World '%s' does not exist", world_id));
    }

    LOG(INFO) << "Deleting world with id: " << world_id;
    deleted_world = std::move(it->second);
    worlds_.erase(it);
  }

  // Make sure we don't hold worlds_mtx_ when the callback is called.
  absl::MutexLock lock(*deleted_world->mtx);
  MaybeInvokeOnDeleteCallback(world_id, deleted_world);
  return absl::OkStatus();
}

void WorldStorage::MarkWorldAsChanged(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world,
    bool has_state_change) {
  MaybeInvokeOnChangeCallback(world_id, world, has_state_change);
}

absl::Status WorldStorage::SetOnAddWorldCallback(OnAddWorldCallback cb,
                                                 bool call_for_existing) {
  const stats::ScopedSpan span("WorldStorage/SetOnAddWorldCallback");

  {
    absl::MutexLock l(callbacks_mtx_);
    on_add_cb_ = std::move(cb);
  }

  if (call_for_existing) {
    absl::ReaderMutexLock l(worlds_mtx_);
    for (const auto& world : worlds_) {
      absl::MutexLock world_lock(*world.second->mtx);
      INTR_RETURN_IF_ERROR(MaybeInvokeOnAddCallback(world.first, world.second));
    }
  }

  return absl::OkStatus();
}

void WorldStorage::ClearOnAddWorldCallback() {
  absl::MutexLock l(callbacks_mtx_);
  on_add_cb_ = nullptr;
}

absl::Status WorldStorage::MaybeInvokeOnAddCallback(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world) {
  const stats::ScopedSpan span("WorldStorage/MaybeInvokeOnAddCallback");

  absl::ReaderMutexLock l(callbacks_mtx_);
  if (on_add_cb_) {
    INTR_RETURN_IF_ERROR(on_add_cb_(world_id, world));
  }
  return absl::OkStatus();
}

void WorldStorage::SetOnDeleteWorldCallback(OnDeleteWorldCallback cb) {
  absl::MutexLock l(callbacks_mtx_);
  on_delete_cb_ = std::move(cb);
}

void WorldStorage::ClearOnDeleteWorldCallback() {
  absl::MutexLock l(callbacks_mtx_);
  on_delete_cb_ = nullptr;
}

void WorldStorage::MaybeInvokeOnDeleteCallback(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world) {
  const stats::ScopedSpan span("WorldStorage/MaybeInvokeOnDeleteCallback");

  absl::ReaderMutexLock l(callbacks_mtx_);
  if (on_delete_cb_) {
    on_delete_cb_(world_id, world);
  }
}

void WorldStorage::SetOnWorldChangeCallback(OnWorldChangeCallback cb) {
  absl::MutexLock l(callbacks_mtx_);
  on_change_cb_ = std::move(cb);
}

void WorldStorage::ClearOnWorldChangeCallback() {
  absl::MutexLock l(callbacks_mtx_);
  on_change_cb_ = nullptr;
}

void WorldStorage::MaybeInvokeOnChangeCallback(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world,
    bool has_state_change) {
  const stats::ScopedSpan span("WorldStorage/MaybeInvokeOnChangeCallback");

  absl::ReaderMutexLock l(callbacks_mtx_);
  if (on_change_cb_) {
    on_change_cb_(world_id, world, has_state_change);
  }
}

}  // namespace intrinsic
