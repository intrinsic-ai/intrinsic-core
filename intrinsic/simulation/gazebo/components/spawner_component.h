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

#ifndef INTRINSIC_SIMULATION_GAZEBO_COMPONENTS_SPAWNER_COMPONENT_H_
#define INTRINSIC_SIMULATION_GAZEBO_COMPONENTS_SPAWNER_COMPONENT_H_

#include <functional>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "gz/sim/components/Component.hh"
#include "gz/sim/components/Model.hh"
#include "gz/sim/components/Serialization.hh"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic {
namespace simulation {

namespace details {
class PoseSerializer {
 public:
  static std::ostream& Serialize(std::ostream& _out,
                                 const intrinsic::Pose3d& _pose) {
    static_cast<void>(
        ToProto(_pose).SerializeToOstream(&_out));  // TODO: Handle return value
    return _out;
  }

  static std::istream& Deserialize(std::istream& _in,
                                   intrinsic::Pose3d& _pose) {
    intrinsic_proto::Pose pose_proto;
    static_cast<void>(
        pose_proto.ParseFromIstream(&_in));  // TODO: Handle return value
    _pose = std::move(*intrinsic_proto::FromProtoNormalized(pose_proto));
    return _in;
  }
};

}  // namespace details

// Used by the spawner to indicate if we're going to spawn an object in the next
// iteration of the SpawnerManager's Update() loop. The command is set by
// creating a `Spawn*Cmd` component on an entity and providing a valid pose and
// product name, which will be spawned at the root level of the ECM.

// A SpawnSceneObjectCmd will spawn the corresponding scene object. The prefix
// for the newly created objects will be taken from the `name` field in the
// scene object proto itself.
using SpawnSceneObjectCmd = gz::sim::components::Component<
    intrinsic_proto::scene_object::v1::SceneObject,
    class SpawnSceneObjectCmdTag, gz::sim::serializers::MsgSerializer>;

// Optional component used by the spawner manager to determine
// - the sim world parent node to attach the spawned object to
// - the gz model and link entities to attach the spawned model entities to
struct SpawnCmdParentData {
  intrinsic_proto::world::TransformNodeReference parent_node;
  std::string parent_object_name;
  std::string parent_link_name;
};
using SpawnCmdParent =
    gz::sim::components::Component<SpawnCmdParentData, class SpawnCmdParentTag>;

// Optional component used by the spawner manager to determine the world-space
// pose with which we will spawn an object. If not present, then the object will
// be spawned at the origin.
using SpawnCmdPose =
    gz::sim::components::Component<intrinsic::Pose3d, class SpawnCmdPoseTag,
                                   details::PoseSerializer>;

// Optional component used by the spawner manager to specify the name of the
// spawned object.
using SpawnCmdName =
    gz::sim::components::Component<std::string, class SpawnCmdNameTag>;

// Optional component used by the spawner manager to notify a caller of the
// status of an object that should have been spawned. Note, it's OK to use the
// default serializer in this case, since if the simulator serializes and
// deserializes the simulator state, it's unlikely that anyone will still be
// waiting to be notified by this callback.
using SpawnCmdCallback =
    gz::sim::components::Component<std::function<void(absl::Status)>,
                                   class SpawnCmdCallbackTag>;

// Used by spawners to indicate the product (as in PPR) that's being spawned.
// Internally, this will tell the simulator to spawn the object in the world
// service, and will generate a model to be spawned by Gazebo.
using SpawnerProductName =
    gz::sim::components::Component<std::string, class SpawnerProductNameTag>;

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_COMPONENTS_SPAWNER_COMPONENT_H_
