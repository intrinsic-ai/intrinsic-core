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

#ifndef INTRINSIC_SIMULATION_SERVICE_WORLD_VISUALIZER_H_
#define INTRINSIC_SIMULATION_SERVICE_WORLD_VISUALIZER_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/simulation/world/world_updater.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_service.pb.h"

namespace intrinsic {
namespace simulation {

// The WorldVisualizer is a system for interpolating data based on the expected
// world updates produced from a sequence of skills. When the visualizer is
// invoked via a call to `Visualize()`, it will estimate the amount of time it
// takes to move the robot based on the configuration provided in
// `ObjectWorldUpdates`. The visualizer then generates updates over the course
// of this time period that provide incremental updates to the given world and
// end with the given updates.
class WorldVisualizer : public UpdateGenerator {
 public:
  static absl::StatusOr<std::unique_ptr<WorldVisualizer>> Create();

  // Runs the visualizer to animate the given updates based on the given world
  absl::Status Visualize(const world::ObjectWorldClient& world,
                         const ObjectWorldUpdates& updates) {
    return Visualize(world, updates, {});
  }

  // Runs the visualizer to animate the given updates over 'animation_time'
  // based on the given world. If 'animation_time' is not set, then the
  // implementation will estimate a time that "looks good".
  absl::Status Visualize(const world::ObjectWorldClient& world,
                         const ObjectWorldUpdates& updates,
                         std::optional<absl::Duration> animation_time);

 private:
  WorldVisualizer() = default;
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_SERVICE_WORLD_VISUALIZER_H_
