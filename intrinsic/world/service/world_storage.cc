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

#include <algorithm>
#include <filesystem>
#include <functional>
#include <future>  // NOLINT(build/c++11)
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/flags/flag.h"
#include "absl/functional/any_invocable.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_serializer.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/periodic.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/world.h"
#include "intrinsic/world/world.pb.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/helpers.h"
#include "ortools/base/path.h"

ABSL_FLAG(bool, save_proto_worlds, true,
          "If set to true, will save only world (binary) protos to disk, "
          "otherwise we will save full gzf files to disk.");
ABSL_FLAG(bool, save_worlds_to_disk, true,
          "If set to true, will save worlds to disk. Turning this off may "
          "result in loss of data in the event of a service restart/crash.");

namespace intrinsic {

namespace {

absl::StatusOr<std::vector<std::string>> FilesIn(absl::string_view path,
                                                 absl::string_view extension) {
  std::vector<std::string> files;
  if (path.empty()) {
    return files;
  }
  INTR_RETURN_IF_ERROR(file::Match(absl::StrFormat("%s/*.%s", path, extension),
                                   &files, file::Defaults()));
  LOG(INFO) << "Found " << files.size() << " " << extension
            << " files in path: " << path << "\n\t"
            << absl::StrJoin(files, "\n\t");
  return files;
}

void SaveWorldToDiskAsGZF(absl::string_view world_id,
                          absl::string_view filename,
                          const GeometryDeserializer& deserializer,
                          WorldAndMutex& world_ptr) {
  auto gzfile = GZFile::Create(filename);
  if (!gzfile.ok()) {
    LOG(ERROR) << "Tried to save '" << world_id
               << "' but failed to create gzfile: " << gzfile.status();
    return;
  }

  {
    absl::ReaderMutexLock lock(*world_ptr.mtx);
    auto write_status = world_ptr.world.ToFile(gzfile->get(), deserializer);
    if (!write_status.ok()) {
      LOG(ERROR) << "Tried to save '" << world_id
                 << "' but failed to serialize world: " << write_status;
      return;
    }
  }

  auto flush_status = (*gzfile)->Flush();
  if (!flush_status.ok()) {
    LOG(ERROR) << "Tried to save '" << world_id
               << "' but failed to flush the file: " << flush_status;
    return;
  }
}

void SaveWorldToDiskAsProto(absl::string_view world_id,
                            absl::string_view filename,
                            GeometrySerializer& serializer,
                            WorldAndMutex& world_ptr) {
  intrinsic_proto::world::internal::World world_proto;
  {
    absl::ReaderMutexLock lock(*world_ptr.mtx);
    auto proto_or = world_ptr.world.Serialize();
    if (!proto_or.ok()) {
      proto_or = world_ptr.world.Serialize(serializer);
      if (!proto_or.ok()) {
        LOG(ERROR) << "Tried to save '" << world_id
                   << "' but failed to serialize world: " << proto_or.status();
        return;
      }
    }

    world_proto = std::move(proto_or).value();
  }

  // Writes to a temporary file and renames to the desired file on success. This
  // reduces the chances of corrupting the previously saved file.
  std::string temp_filename = absl::StrCat(filename, ".tmp");
  auto flush_status =
      file::SetBinaryProto(temp_filename, world_proto, file::Defaults());
  if (!flush_status.ok()) {
    LOG(ERROR) << "Tried to save '" << world_id
               << "' but failed to flush the file: " << flush_status;
    return;
  }

  std::error_code error;
  std::filesystem::rename(temp_filename, std::string(filename), error);
  if (error) {
    LOG(ERROR) << "Tried to save '" << world_id
               << "' but failed to rename temp file to " << filename << ": "
               << error.message();
  }
}

absl::Status InsertOrUpdateWorldsFromPath(
    const absl::string_view worlds_path,
    std::optional<ObjectWorldCompatibilityFunc> compat_func,
    WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>>& worlds) {
  // Precreate the worlds based on the existing files on disk
  INTR_ASSIGN_OR_RETURN(std::vector<std::string> gzf_files,
                        FilesIn(worlds_path, "gzf"));
  if (!gzf_files.empty()) {
    return absl::FailedPreconditionError(
        "Worlds stored as GZF cannot be recovered!");
  }

  // Precreate the worlds based on the existing files on disk
  INTR_ASSIGN_OR_RETURN(std::vector<std::string> proto_files,
                        FilesIn(worlds_path, "pbbin"));
  for (const std::string& filename : proto_files) {
    absl::string_view world_id = file::Stem(filename);

    // Load the world gzf file and store it in the cache.s
    intrinsic_proto::world::internal::World world_proto;
    INTR_RETURN_IF_ERROR(
        file::GetBinaryProto(filename, &world_proto, file::Defaults()))
        << "Failed to load world proto from file: " << filename;

    INTR_ASSIGN_OR_RETURN(
        auto world, World::Deserialize(world_proto),
        _ << "Failed to deserialize world proto from file: " << filename);

    if (compat_func.has_value()) {
      INTR_RETURN_IF_ERROR((*compat_func)(world))
          << "Failed compatibility check for world generated from proto file: "
          << filename;
    }

    // Finally lets save the world instance to the map
    worlds[world_id] = std::make_unique<WorldAndMutex>(std::move(world));
    LOG(INFO) << "Loaded " << filename << " with local ID " << world_id;
  }

  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<WorldStorage>> WorldStorage::Create(
    absl::string_view worlds_path, GeometryLibrary& geo_lib,
    std::optional<ObjectWorldCompatibilityFunc> compat_func,
    LoadWorlds should_load_worlds, std::vector<InitWorld> init_worlds) {
  std::future<GeometryLibrary*> instant_geo_lib =
      std::async(std::launch::deferred, [&geo_lib]() { return &geo_lib; });
  return Create(worlds_path, std::move(instant_geo_lib), compat_func,
                should_load_worlds, std::move(init_worlds));
}

absl::StatusOr<std::unique_ptr<WorldStorage>> WorldStorage::Create(
    absl::string_view worlds_path, std::future<GeometryLibrary*> geo_lib_future,
    std::optional<ObjectWorldCompatibilityFunc> compat_func,
    LoadWorlds should_load_worlds, std::vector<InitWorld> init_worlds) {
  WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>> worlds;

  // Prefill the worlds from the given in-memory worlds (e.g. worlds that were
  // loaded from a database). If we're recovering from a crash, we might
  // override these worlds with edited GZFs in the next loop.
  for (InitWorld& init_world : init_worlds) {
    if (compat_func.has_value()) {
      INTR_RETURN_IF_ERROR((*compat_func)(init_world.world))
          << "Failed to apply compatibility function to init world";
    }

    worlds[init_world.world_id] =
        std::make_unique<WorldAndMutex>(std::move(init_world.world));
  }

  // Loads the worlds from given paths if the flag is set.
  GeometryLibrary* geo_lib = geo_lib_future.get();
  if (geo_lib == nullptr) {
    return absl::InternalError("Geometry library is null");
  }
  switch (should_load_worlds) {
    case LoadWorlds::kYes:
      LOG(INFO) << "Loading worlds from path: " << worlds_path;
      INTR_RETURN_IF_ERROR(
          InsertOrUpdateWorldsFromPath(worlds_path, compat_func, worlds));
      break;
    case LoadWorlds::kNo:
      LOG(INFO) << "Not loading worlds from path: " << worlds_path;
      break;
  }

  return absl::WrapUnique(
      new WorldStorage(worlds_path, *geo_lib, compat_func, std::move(worlds)));
}

WorldStorage::WorldStorage(
    absl::string_view worlds_path, GeometryLibrary& geo_lib,
    std::optional<ObjectWorldCompatibilityFunc> compat_func,
    WorldHashMap<std::string, std::shared_ptr<WorldAndMutex>> worlds)
    : worlds_path_(worlds_path),
      geo_lib_(geo_lib),
      compat_func_(compat_func),
      worlds_(std::move(worlds)),
      world_flusher_([this]() { FlushImpl(); }, absl::Milliseconds(100)) {
  CHECK_OK(world_flusher_.Start());
}

WorldStorage::~WorldStorage() {
  // Make sure that we've flushed everything
  {
    absl::MutexLock l(flush_mtx_);
    flush_mtx_.Await(absl::Condition(
        +[](const WorldStorage* ws)
             ABSL_EXCLUSIVE_LOCKS_REQUIRED(ws->flush_mtx_) {
               return ws->ids_to_flush_.empty() && ws->ids_to_delete_.empty();
             },
        this));
  }
  CHECK_OK(world_flusher_.Stop());

  LOG(INFO) << "WorldStorage: flushed the worlds.";
}

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
  MarkWorldForSaving(world_id, result);

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
  MarkWorldForSaving(world_id, result);

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
    if (!worlds_.contains(world_id)) {
      return absl::NotFoundError(
          absl::StrFormat("World '%s' does not exist", world_id));
    }

    // Drop the map entry for the given world id and return if we do not save
    // worlds to disk.
    if (absl::GetFlag(FLAGS_save_worlds_to_disk)) {
      LOG(INFO) << "Marking for deletion world with id: " << world_id;
      absl::MutexLock flush_lock(flush_mtx_);
      ids_to_delete_.emplace(world_id);
      ids_to_flush_.erase(world_id);
    } else {
      LOG(INFO) << "Deleting world with id: " << world_id;
    }

    deleted_world = worlds_[world_id];
    worlds_.erase(world_id);
  }

  // Make sure we don't hold worlds_mtx_ when the callback is called.
  absl::MutexLock lock(*deleted_world->mtx);
  MaybeInvokeOnDeleteCallback(world_id, deleted_world);
  return absl::OkStatus();
}

void WorldStorage::MarkWorldForSaving(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world) {
  // Don't save or schedule worlds for saving if the flag disables it.
  if (!absl::GetFlag(FLAGS_save_worlds_to_disk)) {
    return;
  }

  absl::MutexLock lock(flush_mtx_);
  if (ids_to_delete_.contains(world_id)) {
    // If we plan to delete this world, don't mark it for saving.
    return;
  }
  ids_to_flush_.emplace(world_id);
}

void WorldStorage::MarkWorldAsChanged(
    absl::string_view world_id, const std::shared_ptr<WorldAndMutex>& world,
    bool has_state_change) {
  MaybeInvokeOnChangeCallback(world_id, world, has_state_change);
  MarkWorldForSaving(world_id, world);
}

void WorldStorage::FlushImpl() {
  // Don't save worlds to disk if the flag disables it.
  if (!absl::GetFlag(FLAGS_save_worlds_to_disk)) {
    return;
  }

  WorldHashSet<std::string> ids_to_flush;
  WorldHashSet<std::string> ids_to_delete;
  {
    absl::MutexLock lock(flush_mtx_);
    std::swap(ids_to_flush, ids_to_flush_);
    std::swap(ids_to_delete, ids_to_delete_);
  }

  const bool save_protos_only = absl::GetFlag(FLAGS_save_proto_worlds);
  for (const auto& world_id : ids_to_flush) {
    if (ids_to_delete.contains(world_id)) {
      // Don't flush ids we intend to delete.
      continue;
    }

    std::shared_ptr<WorldAndMutex> world_ptr;
    {  // Grab the world_ptr from the worlds map
      absl::ReaderMutexLock lock(worlds_mtx_);
      if (worlds_.contains(world_id)) {
        world_ptr = worlds_[world_id];
      }
    }

    if (world_ptr == nullptr) {
      // TODO(stoyang): This shouldn't happen anymore, need to investigate the
      // timings a bit further.
      LOG(WARNING) << "Tried to save missing world: " << world_id
                   << " maybe it was already deleted";
      continue;
    }

    // Only write the proto files if the flag is set.
    if (save_protos_only) {
      const std::string filename = GetProtoFilepathForWorldId(world_id);
      SaveWorldToDiskAsProto(world_id, filename, geo_lib_.Serializer(),
                             *world_ptr);
    } else {
      const std::string filename = GetFilepathForWorldId(world_id);
      SaveWorldToDiskAsGZF(world_id, filename, geo_lib_.Deserializer(),
                           *world_ptr);
    }
  }

  // Clear the worlds from disk that were marked for deletion
  for (const auto& world_id : ids_to_delete) {
    LOG(INFO) << "Deleting world with id: " << world_id;
    const std::string filepath = save_protos_only
                                     ? GetProtoFilepathForWorldId(world_id)
                                     : GetFilepathForWorldId(world_id);

    // Deletes the on disk world. A missing file is not an error since worlds
    // that are deleted before the next flush, e.g., temporary worlds, are never
    // written to disk.
    std::error_code error;
    std::filesystem::remove(filepath, error);
    LOG_IF(WARNING, error) << "Failed to delete world file " << filepath << ": "
                           << error.message();
  }
}

std::string WorldStorage::GetFilepathForWorldId(
    absl::string_view world_id) const {
  return file::JoinPath(worlds_path_, absl::StrCat(world_id, ".gzf"));
}

std::string WorldStorage::GetProtoFilepathForWorldId(
    absl::string_view world_id) const {
  return file::JoinPath(worlds_path_, absl::StrCat(world_id, ".pbbin"));
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
