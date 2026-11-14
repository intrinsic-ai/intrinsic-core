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

#ifndef INTRINSIC_SIMULATION_WORLD_WORLD_UPDATER_H_
#define INTRINSIC_SIMULATION_WORLD_WORLD_UPDATER_H_

#include <memory>
#include <optional>

#include "absl/base/thread_annotations.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"

namespace intrinsic {
namespace simulation {

using ObjectWorldUpdate = intrinsic_proto::world::ObjectWorldUpdate;
using ObjectWorldUpdates = intrinsic_proto::world::ObjectWorldUpdates;

// An update generator is a class that manages an underlying producer/consumer
// queue. This queue can be queried by a separate thread (such as via a
// WorldUpdater) that will consume each of the added updates via sequential
// calls to UpdateGenerator::NextWorldUpdate();
//
// Users will likely want to create a subclass of UpdateGenerator that either
// runs an event loop internally or subscribes to an external event loop. In
// this loop, the user should periodically call
// UpdateGenerator::PublishUpdate(). By itself, the base class has no way of
// producing new updates.
class UpdateGenerator {
 public:
  virtual ~UpdateGenerator() = default;

  // Returns the next pending world update, if one exists. Blocks either until
  // the given deadline is reached or an update is available.
  std::optional<ObjectWorldUpdate> NextWorldUpdate(absl::Time deadline);

  // Returns the next pending world update, if one exists without blocking.
  std::optional<ObjectWorldUpdate> NextWorldUpdate() {
    return NextWorldUpdate(absl::InfinitePast());
  }

  // Returns true if there is a pending update. This is an instantaneous answer
  // and might not reflect whether or not an update has been recently written
  // or read from the underlying channel. Client code should not depend on this.
  bool HasPendingUpdate();

  // Disallow copy and assign
  UpdateGenerator(const UpdateGenerator&) = delete;
  UpdateGenerator& operator=(const UpdateGenerator&) = delete;
  UpdateGenerator(const UpdateGenerator&&) = delete;
  UpdateGenerator& operator=(const UpdateGenerator&&) = delete;

 protected:
  UpdateGenerator();  // Must be subclassed.
  void PublishUpdate(ObjectWorldUpdate update,
                     absl::Duration enqueue_timeout = absl::InfiniteDuration());

 private:
  absl::Mutex pending_update_check_mutex_;
  ConcurrentQueue<ObjectWorldUpdate> pending_updates_;
};

// A trivial update generator is one that never generates any updates.
class TrivialUpdateGenerator : public UpdateGenerator {};

// A world updater is an abstract class that will periodically provide updated
// information for a world, usually based on the results from a simulation
// instance.
//
// This class takes as an argument an UpdateGenerator, which produces a stream
// of ObjectWorldUpdate protos. This class will then call the child's
// OnWorldUpdates() with all of the most recently added updates at the frequency
// specified in the constructor.
//
// Internally, we spawn a thread to monitor whether or not any updates have been
// added. This class is therefore meant to be thread safe, so you can assume
// that OnWorldUpdates() can be called from any thread.
class WorldUpdater {
 public:
  // Takes a UpdateGenerator that is expected to outlive this object.
  explicit WorldUpdater(UpdateGenerator* generator)
      : WorldUpdater(generator, absl::Milliseconds(1)) {}
  virtual ~WorldUpdater();

  // Disallow copy and assign
  WorldUpdater(const WorldUpdater&) = delete;
  WorldUpdater& operator=(const WorldUpdater&) = delete;
  WorldUpdater(const WorldUpdater&&) = delete;
  WorldUpdater& operator=(const WorldUpdater&&) = delete;

  // Starts the thread that monitors the update generator passed to the
  // constructor. Since the thread will call a pure virtual function
  // OnWorldUpdates, Start() may be called from a constructor only if the class
  // has no derived children. Returns true if we successfully started the
  // thread.
  bool Start();

  // Stops the thread that was started via Start(). Since the thread calls a
  // pure virtual function, Stop() may be called from a derived class destructor
  // only if the class has no derived children. Returns true if we've
  // successfully stopped the thread.
  bool Stop();

  // Returns true if the monitoring thread is currently running.
  bool IsRunning() const;

  // Resets the update generator without interrupting the monitoring thread.
  void ResetUpdateGenerator(UpdateGenerator* new_generator)
      ABSL_LOCKS_EXCLUDED(reset_generator_mutex_);

 protected:
  // Child class to implement what to do with the new updates. Note, that
  // because this class uses a separate thread to monitor updates, this function
  // may be invoked at any time, including during destruction of a derived
  // class. Special care must be taken if member variables depend on the value
  // of 'updates'.
  virtual void OnWorldUpdates(const ObjectWorldUpdates& updates) = 0;

  // Constructor that allows the child class to vary the amount of time between
  // calls to OnWorldUpdates(). This is restricted to child classes, because it
  // may cause starvation if updates are configured to be too frequent (such as
  // an minimum duration of zero). 'time_between_updates' is the minimum amount
  // of time between sequential calls to OnWorldUpdates().
  WorldUpdater(UpdateGenerator* generator, absl::Duration time_between_updates);

 private:
  // The mutex that guarantees that only one thread/fiber is ever calling
  // ResetUpdateGenerator.
  absl::Mutex reset_generator_mutex_;

  // The generator associated with this updater. It may be replaced by calling
  // ResetUpdateGenerator.
  UpdateGenerator* generator_ ABSL_GUARDED_BY(generator_mutex_);
  absl::Mutex generator_mutex_;

  // Conditional variable to notify that the generator on the main thread was
  // updated.
  absl::CondVar main_thread_generator_updated_;

  const absl::Duration time_between_updates_;

  Thread update_monitor_thread_;
  void ThreadMain(StopToken stop_token);
};

// This world updater takes updates and does nothing with them. This is useful
// for providing a sink for update generators without changing the client APIs.
class NoopWorldUpdater : public WorldUpdater {
 public:
  static std::unique_ptr<NoopWorldUpdater> Create(UpdateGenerator* generator) {
    return absl::WrapUnique(new NoopWorldUpdater(generator));
  }

 private:
  explicit NoopWorldUpdater(UpdateGenerator* g) : WorldUpdater(g) {}
  void OnWorldUpdates(const ObjectWorldUpdates& updates) final {
    /* do nothing */
  }
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_WORLD_WORLD_UPDATER_H_
