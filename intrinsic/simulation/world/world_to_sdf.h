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

#ifndef INTRINSIC_SIMULATION_WORLD_WORLD_TO_SDF_H_
#define INTRINSIC_SIMULATION_WORLD_WORLD_TO_SDF_H_

#include <memory>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/declare.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "boost/bimap.hpp"
#include "intrinsic/scene/sdf/separators.h"
#include "intrinsic/simulation/gazebo/gz_topic_info.h"
#include "intrinsic/simulation/gazebo/world_templates/world_template.h"
#include "intrinsic/simulation/world/world_object_plugin.h"
#include "intrinsic/world/world.h"

ABSL_DECLARE_FLAG(bool, enable_convex_decomposition);
ABSL_DECLARE_FLAG(bool, enable_auto_inertial);
ABSL_DECLARE_FLAG(int, sim_max_convex_hulls);
ABSL_DECLARE_FLAG(bool, use_sdf_update_rate_for_cameras);

namespace intrinsic {
namespace simulation {

// Converts World to an XML SDF string, and maintains a correspondence between
// the Intrinsic world object and the generated SDF.
//
// See: go/intrinsic-world-to-sdf
class WorldSdfAdapter {
 public:
  struct WorldTemplateOverrides {
    // Overrides the physics step size in the generated SDF. If specified, this
    // value must be strictly positive.
    std::optional<absl::Duration> physics_step_size;

    // Which links are simulated kinematically vs. dynamically. See
    // intrinsic_proto.simulation.gazebo.v1.GazeboConfig.SimulationOptions
    // .PhysicsMode for the semantics of each mode.
    enum class PhysicsMode {
      // Uses the simulator default (dynamic).
      kUnspecified,
      // All links dynamic.
      kDynamic,
      // All links kinematic.
      kKinematic,
    };

    // If set, configures the `<kinematic>` settings of the
    // `gz::sim::systems::Physics` plugin in the world template. Requires a
    // world template with a Physics plugin to be present.
    std::optional<PhysicsMode> physics_mode;
  };

  struct Options {
    // Local path at which to save the geometry from the world. If empty (which
    // is the default), no geometry will be saved, and the resulting SDF will
    // not contain visual or collision meshes.
    std::string mesh_savepath;

    // The name scope separator will be the string used to scope model and link
    // names from the SDF. If the separator is present in any of the SDF's
    // model or link names, the results of GetGazeboNameForEntity and
    // GetIntrinsicEntityForScopedName are undefined.
    std::string name_scope_separator =
        std::string(intrinsic::kWorldSdfDefaultSeparator);

    // If true, ensures that sensor topic names are unique by generating the
    // topic name as follows:
    // - if topic name is set: /<parent_model_name>/<topic_name>
    // - if topic name is not set: /<parent_model_name>/<sensor_name>
    // where spaces are replaced by "_" and special characters by "" to ensure a
    // valid topic name.
    bool ensure_unique_sensor_topic_names = false;

    // If true, models generated in the output SDF will be at the link and joint
    // poses corresponding to zero dof values for all joints in the models.
    // Otherwise, models will be at the same link and joint poses as the input
    // world at its current configuration, i.e. at the current dof values. Note
    // that in this case, the zero-configuration for the model will be different
    // in the world and the generated SDF.
    bool reset_models_to_zero_dof_values = true;

    // If true, sensors and related entities will be skipped in the output SDF
    // if they cannot be successfully converted from the world.
    // TODO(b/407051451): Extend to other entity types as well once conversion
    // error can be surfaced up to the user.
    bool skip_failed_sensors = false;

    // If true, we will skip unsupported collision geometries instead of
    // failing. This currently includes point clouds.
    bool skip_unsupported_collision_geos = false;

    // If true, we will enable the object interaction moderator in the output
    // SDF.
    bool enable_object_interaction_moderator = true;

    // SDF World template to inject. Plugins, lights and other specified world
    // elements in the template will be injected verbatim into the output sdf.
    //
    // Note: The WorldSdfAdapter may override a "/physics/max_step_size"
    // specified in the template if provided in `world_template_overrides`.
    std::optional<WorldTemplate> world_template = std::nullopt;

    // Overrides to apply to the injected world template or generated SDF (e.g.,
    // physics step size).
    WorldTemplateOverrides world_template_overrides;

    // A map from collection entity local name to gripper spec.
    // The specified gripper plugin will be applied when generating SDF for
    // the corresponding collection entity.
    absl::flat_hash_map<std::string, WorldObjectPlugin::GripperSpec>
        collection_entity_local_name_to_gripper_spec;

    // A map from collection entity local name to Gazebo joint plugins spec.
    // The specified Gazebo plugins will be applied when generating SDF for
    // the corresponding collection entity.
    absl::flat_hash_map<std::string, WorldObjectPlugin::GzPluginsSpec>
        collection_entity_local_name_to_gz_plugins_spec;

    // The flat hash set of collections entity names that represent hardware
    // modules.
    absl::flat_hash_set<std::string> hardware_module_objects;

    // A map from collections entity local name to camera spec override.
    absl::flat_hash_map<std::string, WorldObjectPlugin::CameraSpec>
        collection_entity_local_name_to_camera_spec;
  };

  static absl::StatusOr<std::unique_ptr<WorldSdfAdapter>> Create(
      const World& world, const Options& options);

  static absl::StatusOr<std::unique_ptr<WorldSdfAdapter>> Create(
      const World& world) {
    return Create(world, Options());
  }

  // Returns the fully-scoped name for the gazebo entity that corresponds to
  // this entity ID. Returns empty if the given entity ID does not match to any
  // gazebo entities. Internally, the separator used is the name_scope_separator
  // passed via options, but can be replaced with a custom separator instead.
  // This is useful when querying gazebo directly by using kSdfNameSeparator.
  std::optional<std::string> GetGazeboNameForEntity(
      EntityId entity_id, absl::string_view separator) const;

  std::optional<std::string> GetGazeboNameForEntity(EntityId entity_id) const {
    return GetGazeboNameForEntity(entity_id, name_scope_separator_);
  }

  // The separator used internally for scoped names from the provided SDF.
  absl::string_view GetNameScopeSeparator() const {
    return name_scope_separator_;
  }

  // Returns the entity ID for the given scoped name in the SDF. Returns empty
  // if the given name does not match any intrinsic entity ID. The returned
  // scoped name is expected to use name_scope_separator_ to separate scoped
  // names.
  std::optional<EntityId> GetIntrinsicEntityForScopedName(
      absl::string_view name) const;

  // Returns the serialized SDF for this world.
  const std::string& GetSDF() const { return sdf_; }

  // Returns the assigned topic name for a sensor entity, if the given entity is
  // a sensor and has a publish topic assigned to it, std::nullopt otherwise.
  std::optional<std::string> GetSensorTopicNameForEntity(
      EntityId entity_id) const;

  // Returns the sim step size that is specified in the SDF.
  absl::Duration GetSimStepSize() const { return sim_step_size_; }

  // Returns the mapping of object name to its topics (plugins and sensors)
  // computed during World-to-SDF conversion. std::map is used to preserve
  // deterministic ordering.
  // Objects without any associated topics are omitted in the result.
  const std::map<std::string, std::vector<GzTopicInfo>>& GetObjectTopics()
      const {
    return object_topics_;
  }

  // Disallow copy and assign.
  WorldSdfAdapter(const WorldSdfAdapter&) = delete;
  WorldSdfAdapter& operator=(const WorldSdfAdapter&) = delete;

 private:
  WorldSdfAdapter() = default;

  boost::bimap<EntityId, std::string> entity_id_to_scoped_name_;
  std::map<std::string, std::vector<GzTopicInfo>> object_topics_;
  std::string sdf_;
  std::string name_scope_separator_;
  absl::Duration sim_step_size_;
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_WORLD_WORLD_TO_SDF_H_
