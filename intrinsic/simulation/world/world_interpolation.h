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

#ifndef INTRINSIC_SIMULATION_WORLD_WORLD_INTERPOLATION_H_
#define INTRINSIC_SIMULATION_WORLD_WORLD_INTERPOLATION_H_

#include <vector>

#include "absl/status/statusor.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_service.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/world.h"

namespace intrinsic::simulation {

// t must be inside the closed interval of [0, 1].
absl::StatusOr<intrinsic_proto::world::ObjectWorldUpdate>
InterpolateObjectWorldUpdate(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::ObjectWorldUpdate& world_update, double t);

// t must be inside the closed interval of [0, 1].
absl::StatusOr<intrinsic_proto::world::ObjectWorldUpdates>
InterpolateObjectWorldUpdates(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::ObjectWorldUpdates& world_updates, double t);

// Interpolate the given world update across a sequence of time points specified
// in the `times` vector. The `times` vector must be monotonically increasing,
// with each entry lying in [0, 1]. The output vector will be the same size as
// `times` vector, with the i'th entry corresponding to the interpolation update
// at the i'th time.
absl::StatusOr<std::vector<intrinsic_proto::world::ObjectWorldUpdate>>
InterpolateObjectWorldUpdate(
    const world::ObjectWorldClient& world,
    const intrinsic_proto::world::ObjectWorldUpdate& world_update,
    const std::vector<double>& times);

}  // namespace intrinsic::simulation

#endif  // INTRINSIC_SIMULATION_WORLD_WORLD_INTERPOLATION_H_
