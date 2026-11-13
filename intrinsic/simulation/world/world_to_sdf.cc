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

#include "intrinsic/simulation/world/world_to_sdf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/time/time.h"
#include "boost/bimap.hpp"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/struct.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "gz/transport/TopicUtils.hh"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/geometry/api/exact_geometry.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/geometry_fingerprint.h"
#include "intrinsic/geometry/api/is_convex.h"
#include "intrinsic/geometry/api/renderable_generation.h"
#include "intrinsic/geometry/internal/legacy/mesh/io/save_mesh_to_stl_file.h"
#include "intrinsic/geometry/internal/util/scale_shape.h"
#include "intrinsic/geometry/shapes/shape_base.h"
#include "intrinsic/geometry/shapes/shapes.h"
#include "intrinsic/icon/hal/proto/v1/digital_input_output.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/scene/sdf/convert_to_sdf.h"
#include "intrinsic/scene/sdf/sdf_sensor_pose.h"
#include "intrinsic/scene/sdf/sdf_util.h"
#include "intrinsic/scene/user_data_keys.h"
#include "intrinsic/simulation/gazebo/gz_topic_constants.h"
#include "intrinsic/simulation/gazebo/plugins/world_model_config_plugin_constants.h"
#include "intrinsic/simulation/gazebo/world_templates/world_template.h"
#include "intrinsic/simulation/world/gen_node.h"
#include "intrinsic/simulation/world/generate_collision_bitmasks.h"
#include "intrinsic/simulation/world/gripper_plugin_spec.h"
#include "intrinsic/simulation/world/inlined_plugins_util.h"
#include "intrinsic/simulation/world/multi_camera_plugin_spec.h"
#include "intrinsic/simulation/world/sim_plugins.h"
#include "intrinsic/simulation/world/world_object_plugin.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/stats/tracing_utils.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/full_precision.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/component/attachment_component.h"
#include "intrinsic/world/component/collections_component.h"
#include "intrinsic/world/component/collections_member_component.h"
#include "intrinsic/world/component/collision_component.h"
#include "intrinsic/world/component/geometry_component.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/component/physics_component.h"
#include "intrinsic/world/component/ppr_component.h"
#include "intrinsic/world/component/projector_component.h"
#include "intrinsic/world/component/robot_component.h"
#include "intrinsic/world/component/sensor_component.h"
#include "intrinsic/world/component/simulation_component.h"
#include "intrinsic/world/component/user_data_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/geometry_types.h"
#include "intrinsic/world/hashing/hashing.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_creation_utils.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/proto/generic_action.pb.h"
#include "intrinsic/world/proto/kinematics_component.pb.h"
#include "intrinsic/world/proto/robot_component.pb.h"
#include "intrinsic/world/proto/sensor_component.pb.h"
#include "intrinsic/world/world.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/tracer.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/options.h"
#include "ortools/base/path.h"
#include "sdf/Element.hh"
#include "sdf/Error.hh"
#include "sdf/ParserConfig.hh"
#include "sdf/Physics.hh"
#include "sdf/Root.hh"
#include "sdf/Sensor.hh"
#include "sdf/World.hh"
#include "sdf/parser.hh"
#include "third_party/thorough_hash/thorough_hash.h"
#include "tinyxml2.h"

ABSL_FLAG(bool, enable_convex_decomposition, false,
          "Enable convex decomposition for collision meshes.");

ABSL_FLAG(bool, enable_auto_inertial, true,
          "Enable auto-inertial calaculation for links.");

ABSL_FLAG(int, sim_max_convex_hulls, 32,
          "Maximum number of convex hulls for collision mesh convex "
          "deomposition.");

ABSL_FLAG(bool, force_trigger_cameras, false,
          "If true, cameras will be set to trigger mode instead of streaming "
          "at the update rate specified in the world.");

namespace intrinsic {
namespace simulation {
namespace {

using details::GenNode;
using ::intrinsic::shapes::Box;
using ::intrinsic::shapes::Capsule;
using ::intrinsic::shapes::Cylinder;
using ::intrinsic::shapes::Ellipsoid;
using ::intrinsic::shapes::ShapeType;
using ::intrinsic::shapes::Sphere;

using ::intrinsic::sdf::CommonCameraPropertiesToString;
using ::intrinsic::sdf::FixSDFNameForReservedCharacters;
using ::intrinsic::sdf::ForceTorqueSpecToString;
using ::intrinsic::sdf::InertialOptions;
using ::intrinsic::sdf::InertialProperties;
using ::intrinsic::sdf::InertialSpecToString;
using ::intrinsic::sdf::JointProperties;
using ::intrinsic::sdf::JointSpecToString;
using ::intrinsic::sdf::LidarSpecToString;
using ::intrinsic::sdf::MotionTypeToString;
using ::intrinsic::sdf::Pose3ToPoseString;
using ::intrinsic::sdf::Vec3ToString;

using CameraPluginSpec =
    intrinsic_proto::world::SensorComponent::CameraPluginSpec;
using IconSimDevice = intrinsic_proto::world::RobotComponent::IconSimDevice;
using GenericActionPluginSpec =
    intrinsic_proto::world::generic_action::GenericActionPluginSpec;
using DigitalInputOutput = intrinsic_proto::icon::v1::DigitalInputOutput;
using EntityIdToScopedNameBimap = boost::bimap<EntityId, std::string>;

constexpr absl::string_view kDioInputTopicPrefix = "dio_in_";
constexpr absl::string_view kDioOutputTopicPrefix = "dio_out_";

// Convex decomposition parameters for collision meshes that are already
// convex. A single hull represents a convex mesh, and a coarse voxelization
// keeps the decomposition computed by Gazebo on world load cheap.
constexpr uint32_t kConvexMeshMaxConvexHulls = 1;
constexpr uint32_t kConvexMeshVoxelResolution = 10000;

// The list of primitive types supported by Gazebo that we can export.
constexpr std::array<ShapeType, 5> kPrimitiveTypesSupportedByGazebo = {
    ShapeType::BOX, ShapeType::CYLINDER, ShapeType::SPHERE,
    ShapeType::ELLIPSOID, ShapeType::CAPSULE};

struct JointParams {
  std::string joint_name;
  std::string parent_name;
  std::string child_name;
};

// Returns a DigitalInputOutput proto for `entity`.
//
// * If `entity` does not have a UserDataComponent, or if it does and the
//   UserDataProtos map doesn't have an entry for the key `sdf::kDioData`, this
//   returns std::nullopt.
// * If the map *has* an entry for that key, but it does not contain a
//   DigitalInputOutput proto, this returns an InvalidArgumentError
// * If the map has an entry with the correct type, then this returns that
//   entry.
absl::StatusOr<std::optional<DigitalInputOutput>> GetIconDioData(
    const WorldEntity& entity) {
  auto user_data = entity.GetComponent<UserDataComponent>();
  if (!user_data.ok()) {
    return std::nullopt;
  }
  const WorldHashMap<std::string, ::google::protobuf::Any>& user_data_map =
      (*user_data)->UserDataProtos();
  auto itr = user_data_map.find(sdf::kDioData);
  if (itr == user_data_map.end()) {
    return std::nullopt;
  }

  DigitalInputOutput dio_data;
  if (!itr->second.UnpackTo(&dio_data)) {
    // Try to parse the type URL from the Any into a full name.
    std::string actual_user_data_type_name;
    if (!::google::protobuf::Any::ParseAnyTypeUrl(
            itr->second.type_url(), &actual_user_data_type_name)) {
      actual_user_data_type_name = itr->second.type_url();
    }
    return absl::InvalidArgumentError(absl::StrCat(
        "The user data proto map for entity ", entity.GetAlias(),
        " has an entry for digital inputs and outputs (DIOs), but that "
        "entry has the wrong type. Should be '",
        DigitalInputOutput::GetDescriptor()->full_name(), "', but is '",
        actual_user_data_type_name, "'"));
  }

  return dio_data;
}

// Returns a custom joint sdf map for a collection entity.
//
// * If `entity` does not have a UserDataComponent, or if it does and the
//   UserDataProtos map doesn't have an entry for the key
//   `sdf::kGazeboCustomJoint`, this returns an empty custom joint sdf map.
// * If the user data map has an entry with the correct type, then this returns
//   that entry. The entry is a custom joint sdf map with key = joint name and
//   value  = joint SDF string.
absl::flat_hash_map<std::string, std::string> GetCustomJointSdfData(
    const WorldEntity& entity) {
  auto user_data = entity.GetComponent<UserDataComponent>();
  if (!user_data.ok()) {
    return {};
  }
  const WorldHashMap<std::string, ::google::protobuf::Any>& user_data_map =
      (*user_data)->UserDataProtos();
  auto itr = user_data_map.find(sdf::kGazeboCustomJoint);
  if (itr == user_data_map.end()) {
    return {};
  }

  absl::flat_hash_map<std::string, std::string> custom_joint_sdf;
  const google::protobuf::Any& any_data = itr->second;
  // TODO(b/501519360) Use a dedicated proto file for storing custom joint
  // sdf strings instead of Struct.
  google::protobuf::Struct struct_msg;
  if (!any_data.UnpackTo(&struct_msg)) {
    return {};
  }
  for (const auto& [key, val] : struct_msg.fields()) {
    if (!key.empty() &&
        val.kind_case() == google::protobuf::Value::kStringValue &&
        !val.string_value().empty()) {
      custom_joint_sdf[key] = val.string_value();
    }
  }

  return custom_joint_sdf;
}

std::string CreateScopedName(absl::string_view parent, absl::string_view child,
                             absl::string_view separator) {
  LOG_IF(WARNING, absl::StrContains(parent, separator))
      << "Parent name [" << parent << "] contains scope separator '"
      << separator << "'";
  LOG_IF(WARNING, absl::StrContains(child, separator))
      << "Child name [" << child << "] contains scope separator '" << separator
      << "'";
  return absl::StrCat(parent, separator, child);
}

absl::StatusOr<std::string> GetEntityName(const World& world,
                                          const EntityId entity_id,
                                          absl::string_view type_string,
                                          bool prepend_entity_id) {
  INTR_ASSIGN_OR_RETURN(const auto* entity, world.GetEntityById(entity_id));
  std::string name = entity->GetLocalName();
  if (sdf::IsReservedSDFName(name)) {
    LOG(WARNING) << "Local entity name '" << name
                 << "' is reserved in sdformat, prepending entity id "
                 << entity_id << " to create a valid name.";
    prepend_entity_id = true;
  }

  if (prepend_entity_id) {
    name = absl::StrCat(entity_id.value(), kSdfNamePartsSeparator, name);
  }

  if (name.empty()) {
    name = absl::StrFormat("world_%s_%d", type_string, entity_id.value());
    LOG(WARNING) << "World " << type_string << " with id " << entity_id
                 << " has empty local name. Using '" << name << "'";
  }

  return FixSDFNameForReservedCharacters(name);
}

const GeometryComponent* ValidateGeometry(const WorldEntity& entity) {
  auto geo_component = entity.GetComponent<GeometryComponent>();
  if (!geo_component.ok()) {
    return nullptr;
  }
  if ((*geo_component)->GetGeometryNames().empty()) {
    return nullptr;
  }
  return *geo_component;
}

absl::StatusOr<std::string> CreateTopic(const WorldEntity* entity,
                                        const std::string& suffix) {
  std::string topic = absl::StrCat("/", entity->GetLocalName(), "/", suffix);
  if (!::gz::transport::TopicUtils::IsValidTopic(topic)) {
    return FailedPreconditionErrorBuilder()
           << "Unable to generate a topic for entity with "
              "local name ["
           << entity->GetLocalName() << "] with a topic suffix of [" << suffix
           << "]. Topic [" << topic
           << "] is not a valid gazebo transport topic.";
  }
  return topic;
}

template <typename TypedEntityId>
absl::StatusOr<TypedEntityId> GetTypedEntityInCollectionWithLocalName(
    const World& world, const WorldEntity& collection_entity,
    std::string_view local_name) {
  INTR_ASSIGN_OR_RETURN(auto coll_comp,
                        collection_entity.GetComponent<CollectionsComponent>());
  intrinsic_proto::world::CollectionsComponent::CollectionType member_type =
      CollectionsComponent::kLinks;
  if constexpr (std::is_same_v<TypedEntityId, JointEntityId>) {
    member_type = CollectionsComponent::kJoints;
  }
  const auto& members = coll_comp->GetCollectionMembers(member_type);
  std::vector<TypedEntityId> matches;
  for (CollectionsMemberEntityId id : members) {
    if (world.GetLocalNameForEntityById(id) == local_name) {
      auto validated = world.ValidateEntity<TypedEntityId>(id);
      if (validated.ok()) {
        matches.push_back(*validated);
      }
    }
  }
  if (matches.size() == 1) {
    return matches.front();
  }
  if (matches.size() > 1) {
    return absl::FailedPreconditionError(absl::Substitute(
        "Found multiple entities matching name '$0' in collection '$1'",
        local_name, collection_entity.GetLocalName()));
  }
  return absl::NotFoundError(
      absl::Substitute("Cannot find entity with name '$0' in collection '$1'",
                       local_name, collection_entity.GetLocalName()));
}

absl::StatusOr<std::string> GetGripperPlugins(
    const WorldEntity* entity, const World& world,
    const WorldObjectPlugin::GripperSpec& spec) {
  CHECK(entity != nullptr);
  std::string result;

  if (std::holds_alternative<ActuatedGripperPluginSpec>(spec)) {
    auto actuated_spec = std::get<ActuatedGripperPluginSpec>(spec);
    // The output joint and link names in the SDF may have been updated during
    // conversion, so update the plugin spec to match the new names so that the
    // joints/links can be found in the simulator.
    for (std::string& joint_name : actuated_spec.joint_names) {
      INTR_ASSIGN_OR_RETURN(
          JointEntityId joint_id,
          GetTypedEntityInCollectionWithLocalName<JointEntityId>(world, *entity,
                                                                 joint_name));
      INTR_ASSIGN_OR_RETURN(
          joint_name, GetEntityName(world, joint_id, /*type_string=*/"joint",
                                    /*prepend_entity_id=*/false));
    }
    if (!actuated_spec.sticky_link_name.empty()) {
      INTR_ASSIGN_OR_RETURN(
          AttachmentEntityId sticky_link_id,
          GetTypedEntityInCollectionWithLocalName<AttachmentEntityId>(
              world, *entity, actuated_spec.sticky_link_name));
      INTR_ASSIGN_OR_RETURN(actuated_spec.sticky_link_name,
                            GetEntityName(world, sticky_link_id,
                                          /*type_string=*/"link",
                                          /*prepend_entity_id=*/false));
    }
    absl::StrAppend(&result, actuated_spec.ToSdformatXmlString());
  } else if (std::holds_alternative<FixedJointGripperPluginSpec>(spec)) {
    auto fixed_spec = std::get<FixedJointGripperPluginSpec>(spec);
    // The output link names in the SDF may have been updated during conversion,
    // so update the plugin spec to match the new names so that the link can be
    // found in the simulator.
    if (!fixed_spec.gripper_link_name.empty()) {
      INTR_ASSIGN_OR_RETURN(
          AttachmentEntityId gripper_link_id,
          GetTypedEntityInCollectionWithLocalName<AttachmentEntityId>(
              world, *entity, fixed_spec.gripper_link_name));
      INTR_ASSIGN_OR_RETURN(
          fixed_spec.gripper_link_name,
          GetEntityName(world, gripper_link_id, /*type_string=*/"link",
                        /*prepend_entity_id=*/false));
    }
    absl::StrAppend(&result, fixed_spec.ToSdformatXmlString());
  }

  return result;
}

absl::StatusOr<std::string> GetMultiCameraPlugins(
    MultiCameraPluginSpec multi_cam_plugin_spec,
    std::string_view parent_model_name,
    const WorldSdfAdapter::Options& options) {
  const auto it = options.collection_entity_local_name_to_camera_spec.find(
      parent_model_name);
  if (it != options.collection_entity_local_name_to_camera_spec.end()) {
    multi_cam_plugin_spec.camera_identifier_proto =
        it->second.camera_identifier_proto;
  }
  return multi_cam_plugin_spec.ToString();
}

absl::StatusOr<std::string> GetSensorPlugins(
    const WorldEntity* entity, std::string_view parent_model_name,
    const WorldSdfAdapter::Options& options) {
  std::string result;
  auto sensor = entity->GetComponent<SensorComponent>();
  if (sensor.ok()) {
    // Find camera spec overrides for this sensor to get
    // camera_identifier_proto.
    std::string camera_identifier_proto_override;
    auto camera_spec_it =
        options.collection_entity_local_name_to_camera_spec.find(
            parent_model_name);
    if (camera_spec_it !=
        options.collection_entity_local_name_to_camera_spec.end()) {
      camera_identifier_proto_override =
          camera_spec_it->second.camera_identifier_proto;
    }

    switch ((*sensor)->GetType()) {
      case SensorType::kCamera: {
        INTR_ASSIGN_OR_RETURN(auto camera_spec, (*sensor)->GetCameraSpec());
        if (camera_spec.has_camera_plugin()) {
          auto* plugin_spec = camera_spec.mutable_camera_plugin();
          if (!camera_identifier_proto_override.empty()) {
            plugin_spec->set_camera_identifier_proto(
                camera_identifier_proto_override);
          }
          absl::StrAppend(&result, PluginSdfTrait<CameraPluginSpec>::ToString(
                                       *plugin_spec, /*index=*/0));
        }
        break;
      }
      case SensorType::kDepthCamera: {
        INTR_ASSIGN_OR_RETURN(auto depth_spec, (*sensor)->GetDepthCameraSpec());
        if (depth_spec.has_camera_plugin()) {
          auto* plugin_spec = depth_spec.mutable_camera_plugin();
          if (!camera_identifier_proto_override.empty()) {
            plugin_spec->set_camera_identifier_proto(
                camera_identifier_proto_override);
          }
          absl::StrAppend(&result, PluginSdfTrait<CameraPluginSpec>::ToString(
                                       *plugin_spec, /*index=*/0));
        }
        break;
      }
      default:
        break;
    }
  }
  return result;
}

absl::StatusOr<std::string> GetGenericActionPlugins(const WorldEntity* entity) {
  auto robot = entity->GetComponent<RobotComponent>();
  if (robot.ok()) {
    INTR_ASSIGN_OR_RETURN(const auto generic_action_plugin_spec,
                          (*robot)->GetGenericActionPluginSpec());
    if (generic_action_plugin_spec) {
      return PluginSdfTrait<GenericActionPluginSpec>::ToString(
          generic_action_plugin_spec.value());
    }
  }
  return "";
}

// Constructs a `<plugin>` tag for the DigitalInputOutput system
// (intrinsic/simulation/gazebo/plugins/digital_input_output.h).
//
// For now, this extracts legacy `IconSimDevice`s from `robot`, and adds DIOs
// for those that have one of these two types:
// * `EL1008`: 8-bit input
// * `EP2338`: 8-bit input *and* 8-bit output
//
// If `robot` is nullptr, or if there are no `IconSimDevice`s in it, this
// function instead reads DIO information from `dio_data`.
//
// Returns InvalidArgumentError if `robot` has DIO devices *and* `dio_data` is
// set.
// Returns an empty string if `robot` has no DIO devices (or is nullptr) *and*
// `dio_data` is unset.
//
// Returns an XML string containing the `<plugin>` tag for `DigitalInputOutput`
// otherwise.
absl::StatusOr<std::string> GetDigitalInputOutputPlugin(
    const WorldEntity& entity, const RobotComponent* absl_nullable robot,
    const std::optional<DigitalInputOutput>& dio_data) {
  std::vector<IconSimDevice> dio_devices;
  if (robot != nullptr) {
    INTR_ASSIGN_OR_RETURN(std::vector<IconSimDevice> sim_devices,
                          robot->GetIconSimDevices());

    // There are potentially more DIO devices that the old sim system
    // supports, but in practice these two are the only ones in use.
    std::ranges::copy_if(
        sim_devices, std::back_inserter(dio_devices),
        [](const IconSimDevice& device) {
          return (device.type() == "EL1008" || device.type() == "EP2338");
        });
  }
  if (!dio_devices.empty() && dio_data.has_value()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "World entity '", entity.GetAlias(),
        "' has a RobotComponent with both a DigitalInputOutput plugin and DIO "
        "devices in the DeviceContainer plugin. Please remove the DIO devices "
        "(",
        absl::StrJoin(dio_devices, ", ",
                      [](std::string* out, const IconSimDevice& device) {
                        out->append(device.name());
                      }),
        ") from your DeviceContainer and add them to DigitalInputOutput if "
        "necessary"));
  }
  if (dio_devices.empty() && !dio_data.has_value()) {
    // No plugin tag if there are no DIO devices.
    return "";
  }
  std::string plugin = "";
  if (!dio_devices.empty()) {
    // If there *are* DIO devices, add the DigitalInputOutput plugin and
    // configure it accordingly.
    plugin = R"xml(
    <plugin filename="static://intrinsic::simulation::DigitalInputOutput"
            name="DigitalInputOutput">)xml";
    for (const IconSimDevice& device : dio_devices) {
      // EL1008 is input-only, EP2338 is input/output.
      bool supports_input =
          device.type() == "EL1008" || device.type() == "EP2338";
      bool supports_output = device.type() == "EP2338";

      if (supports_input) {
        INTR_ASSIGN_OR_RETURN(
            const std::string input_topic,
            CreateTopic(&entity,
                        absl::StrCat(kDioInputTopicPrefix, device.name())));
        absl::StrAppend(&plugin, absl::Substitute(
                                     R"xml(
      <input_block name="$0"
                   num_bits="$1"
                   topic_name="$2"
                   is_legacy="true" />)xml",
                                     device.name(), 8, input_topic));
      }
      if (supports_output) {
        INTR_ASSIGN_OR_RETURN(
            const std::string output_topic,
            CreateTopic(&entity,
                        absl::StrCat(kDioOutputTopicPrefix, device.name())));

        absl::StrAppend(&plugin, absl::Substitute(
                                     R"xml(
      <output_block name="$0"
                    num_bits="$1"
                    topic_name="$2"
                    is_legacy="true" />)xml",
                                     device.name(), 8, output_topic));
      }
    }

    absl::StrAppend(&plugin, R"xml(
    </plugin>
      )xml");
  } else if (dio_data.has_value()) {
    // Make a copy of the DigitalInputOutput spec, so we can adjust the topic
    // names.
    DigitalInputOutput dios = *dio_data;
    // We also need to build device name->topic name maps
    absl::flat_hash_map<std::string, std::string> dio_output_topics;
    absl::flat_hash_map<std::string, std::string> dio_input_topics;
    for (auto& [name, input_block] : *(dios.mutable_digital_input_blocks())) {
      INTR_ASSIGN_OR_RETURN(
          const std::string input_topic,
          CreateTopic(&entity, absl::StrCat(kDioInputTopicPrefix, name)));
      dio_input_topics.emplace(name, input_topic);
    }
    for (auto& [name, output_block] : *(dios.mutable_digital_output_blocks())) {
      INTR_ASSIGN_OR_RETURN(
          const std::string output_topic,
          CreateTopic(&entity, absl::StrCat(kDioOutputTopicPrefix, name)));
      dio_output_topics.emplace(name, output_topic);
    }
    INTR_ASSIGN_OR_RETURN(std::string dio_plugin_string,
                          PluginSdfTrait<DigitalInputOutput>::ToString(
                              dios, dio_output_topics, dio_input_topics));
    absl::StrAppend(&plugin, dio_plugin_string);
  }

  return plugin;
}

struct GzPluginsResult {
  std::string plugins_sdf;
  std::vector<GzTopicInfo> topic_infos;
};

absl::StatusOr<GzPluginsResult> GetGzPlugins(
    const WorldEntity* entity, const World& world,
    const WorldObjectPlugin::GzPluginsSpec& gz_plugins_spec,
    const absl::flat_hash_map<std::string, double>& dof_local_name_to_value) {
  constexpr char kJointPositionControllerPluginName[] =
      "gz::sim::systems::JointPositionController";
  constexpr char kJointPositionControllerPluginFilename[] =
      "static://gz::sim::systems::JointPositionController";
  constexpr char kJointStatePublisherPluginName[] =
      "gz::sim::systems::JointStatePublisher";
  constexpr char kJointStatePublisherPluginFilename[] =
      "static://gz::sim::systems::JointStatePublisher";

  auto make_joint_control_topic_info =
      [&kJointPositionControllerPluginName,
       &kJointPositionControllerPluginFilename](const std::string& topic_name,
                                                const std::string& joint_name,
                                                uint32_t axis_index) {
        return GzTopicInfo{
            .topic_name = topic_name,
            .plugin_info =
                GzTopicInfo::PluginInfo{
                    .name = kJointPositionControllerPluginName,
                    .filename = kJointPositionControllerPluginFilename,
                },
            .joint_info =
                GzTopicInfo::JointInfo{
                    .name = joint_name,
                    .axis_index = axis_index,
                },
            .metadata = std::string(kGzTopicInfoJointPositionControlMetadata),
        };
      };

  auto make_joint_state_topic_info =
      [&kJointStatePublisherPluginName, &kJointStatePublisherPluginFilename](
          const std::string& topic_name, const std::string& joint_name,
          uint32_t axis_index) {
        return GzTopicInfo{
            .topic_name = topic_name,
            .plugin_info =
                GzTopicInfo::PluginInfo{
                    .name = kJointStatePublisherPluginName,
                    .filename = kJointStatePublisherPluginFilename,
                },
            .joint_info =
                GzTopicInfo::JointInfo{
                    .name = joint_name,
                    .axis_index = axis_index,
                },
            .is_sim_server_advertised = true,
            .metadata = std::string(kGzTopicInfoJointStateMetadata),
        };
      };

  auto make_joint_control_plugin_xml =
      [&kJointPositionControllerPluginFilename,
       &kJointPositionControllerPluginName](
          const std::string& joint_name, const std::string& topic_name,
          uint32_t axis_index, double initial_position) -> std::string {
    return absl::StrCat(R"(<plugin filename=")",
                        kJointPositionControllerPluginFilename,
                        R"("
                  name=")",
                        kJointPositionControllerPluginName, R"(">
                 <use_velocity_commands>true</use_velocity_commands>
                 <joint_name>)",
                        joint_name, R"(</joint_name>
                 <initial_position>)",
                        initial_position, R"(</initial_position>
                 <topic>)",
                        topic_name, R"(</topic>
                 <joint_index>)",
                        axis_index, R"(</joint_index>
               </plugin>)");
  };

  auto make_joint_state_plugin_xml =
      [&kJointStatePublisherPluginFilename, &kJointStatePublisherPluginName](
          const std::string& joint_name,
          const std::string& topic_name) -> std::string {
    return absl::StrCat(R"(<plugin filename=")",
                        kJointStatePublisherPluginFilename,
                        R"("
                  name=")",
                        kJointStatePublisherPluginName, R"(">
                 <joint_name>)",
                        joint_name, R"(</joint_name>
                 <topic>)",
                        topic_name, R"(</topic>
               </plugin>)");
  };

  GzPluginsResult result;
  for (const auto& gz_plugin : gz_plugins_spec) {
    if (std::holds_alternative<WorldObjectPlugin::GzJointControl>(gz_plugin)) {
      const WorldObjectPlugin::GzJointControl& joint_control =
          std::get<WorldObjectPlugin::GzJointControl>(gz_plugin);
      const std::string& joint_name = joint_control.joint_name;
      const std::string& topic = joint_control.topic;
      const uint32_t axis_index = joint_control.axis_index;
      if (joint_name.empty() || topic.empty()) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "Unable to attach JointPositionController plugin to entity "
               << entity->GetAlias() << ". Joint name or topic is empty.";
      }
      result.topic_infos.push_back(
          make_joint_control_topic_info(topic, joint_name, axis_index));
      auto dof_local_name_to_value_it =
          dof_local_name_to_value.find(joint_name);
      double initial_position = 0.0;
      if (dof_local_name_to_value_it != dof_local_name_to_value.end()) {
        initial_position = dof_local_name_to_value_it->second;
      } else {
        LOG(WARNING) << "Joint '" << joint_name
                     << "' not found in dof_local_name_to_value map. Initial "
                        "commanded position for JointPositionController may "
                        "not be correct.";
      }
      absl::StrAppend(&result.plugins_sdf,
                      make_joint_control_plugin_xml(
                          joint_name, topic, axis_index, initial_position));
    } else if (std::holds_alternative<WorldObjectPlugin::GzJointState>(
                   gz_plugin)) {
      const WorldObjectPlugin::GzJointState& joint_state =
          std::get<WorldObjectPlugin::GzJointState>(gz_plugin);
      const std::string& joint_name = joint_state.joint_name;
      const std::string& topic = joint_state.topic;
      if (joint_name.empty() || topic.empty()) {
        return ::intrinsic::InvalidArgumentErrorBuilder()
               << "Unable to attach JointStatePublisher plugin to entity "
               << entity->GetAlias() << ". Joint name or topic is empty.";
      }
      result.topic_infos.push_back(make_joint_state_topic_info(
          topic, joint_name, joint_state.axis_index));
      absl::StrAppend(&result.plugins_sdf,
                      make_joint_state_plugin_xml(joint_name, topic));
    } else {
      return ::intrinsic::InvalidArgumentErrorBuilder()
             << "Unknown gz plugin type for entity: " << entity->GetAlias();
    }
  }
  return result;
}

// Returns the plugins to make a simulated hardware module work for the given
// robot entity with Gazebo.
//
// This includes
// * A DigitalInputOutput plugin to set up simulated DIOs
//
// Returns an empty string if `entity` is not a robot (i.e. doesn't have a
// `RobotComponent`).
absl::StatusOr<std::string> GetSimulatedHardwareModulePlugins(
    const WorldEntity* absl_nonnull entity,
    const std::optional<DigitalInputOutput>& extracted_dios_proto) {
  const auto* robot = entity->GetComponent<RobotComponent>().value_or(nullptr);

  // Prefers DIO protos extracted from inlined gazebo plugins. That said, dio
  // data should only be set as inlined gazebo plugin string or DIO proto but
  // not both.
  std::optional<DigitalInputOutput> dio_data = extracted_dios_proto;
  if (!dio_data.has_value()) {
    // Fallback for older assets where DIO data is present in `kDioData` user
    // data key.
    INTR_ASSIGN_OR_RETURN(dio_data, GetIconDioData(*entity));
  }

  return GetDigitalInputOutputPlugin(*entity, robot, dio_data);
}

// Sets the initial joint positions from dof_local_name_to_value
absl::StatusOr<std::string> GetSetModelState(
    const absl::flat_hash_map<std::string, double>& dof_local_name_to_value) {
  std::string result;
  if (dof_local_name_to_value.empty()) {
    return result;
  }

  absl::StrAppend(&result, R"xml(
    <plugin filename="static://intrinsic::simulation::SetModelState"
            name="SetModelState">
      <model_state>)xml");

  for (const auto& [joint_name, value] : dof_local_name_to_value) {
    absl::StrAppend(&result, absl::Substitute(R"xml(
        <joint_state name="$0">
          <axis_state>
            <position>$1</position>
          </axis_state>
        </joint_state>)xml",
                                              joint_name, value));
  }

  absl::StrAppend(&result, R"xml(
      </model_state>
    </plugin>
      )xml");

  return result;
}

struct SimulatorPluginsResult {
  std::string plugins_sdf;
  std::vector<GzTopicInfo> topic_infos;
};

absl::StatusOr<SimulatorPluginsResult> GetSimulatorPlugins(
    const WorldEntity* entity, std::string_view parent_model_name,
    const WorldSdfAdapter::Options& options, const World& world,
    const absl::flat_hash_map<std::string, double>& dof_local_name_to_value =
        {}) {
  SimulatorPluginsResult result;
  std::string plugins;
  const std::string& local_name = entity->GetLocalName();
  // TODO(b/538734403): Remove check to look for DIO plugin data in kDioData key
  // after the SceneObject migration tool handles this for us.
  INTR_ASSIGN_OR_RETURN(InlinedPluginsInfo inlined_plugins_info,
                        ParseInlinedPlugins(entity));
  INTR_ASSIGN_OR_RETURN(plugins, GetSimulatedHardwareModulePlugins(
                                     entity, inlined_plugins_info.dios_proto));
  absl::StrAppend(&result.plugins_sdf, plugins);
  INTR_ASSIGN_OR_RETURN(plugins, GetSetModelState(dof_local_name_to_value));
  absl::StrAppend(&result.plugins_sdf, plugins);

  if (auto it =
          options.collection_entity_local_name_to_gripper_spec.find(local_name);
      it != options.collection_entity_local_name_to_gripper_spec.end()) {
    INTR_ASSIGN_OR_RETURN(plugins,
                          GetGripperPlugins(entity, world, it->second));
    absl::StrAppend(&result.plugins_sdf, plugins);
  }

  INTR_ASSIGN_OR_RETURN(plugins,
                        GetSensorPlugins(entity, parent_model_name, options));
  absl::StrAppend(&result.plugins_sdf, plugins);
  if (inlined_plugins_info.multi_camera_plugin_spec.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        plugins,
        GetMultiCameraPlugins(*inlined_plugins_info.multi_camera_plugin_spec,
                              parent_model_name, options));
    absl::StrAppend(&result.plugins_sdf, plugins);
  }
  INTR_ASSIGN_OR_RETURN(plugins, GetGenericActionPlugins(entity));
  absl::StrAppend(&result.plugins_sdf, plugins);

  {
    // Only add Gz joint plugins if the collection entity is not a gripper or
    // hardware module.
    const bool is_gripper =
        options.collection_entity_local_name_to_gripper_spec.contains(
            local_name);
    const bool is_hardware_module =
        options.hardware_module_objects.contains(local_name);
    if (!is_gripper && !is_hardware_module) {
      if (auto it =
              options.collection_entity_local_name_to_gz_plugins_spec.find(
                  local_name);
          it != options.collection_entity_local_name_to_gz_plugins_spec.end()) {
        INTR_ASSIGN_OR_RETURN(
            GzPluginsResult gz_plugins_result,
            GetGzPlugins(entity, world, it->second, dof_local_name_to_value));
        absl::StrAppend(&result.plugins_sdf, gz_plugins_result.plugins_sdf);
        result.topic_infos = std::move(gz_plugins_result.topic_infos);
      }
    }
  }

  if (!inlined_plugins_info.unparsed_inlined_plugins.empty()) {
    absl::StrAppend(&result.plugins_sdf,
                    inlined_plugins_info.unparsed_inlined_plugins);
  }
  return result;
}

struct GeneratedSensorInfo {
  std::string sdf_string;
  std::vector<EntityId> sensor_ids;
  std::vector<GzTopicInfo> topic_infos;
};

// Returns a sequence of <sensor> elements. If no sensor in children returns
// empty string.
// If options.ensure_unique_sensor_topic_names is set true, then the sensor
// topic name is set as follows:
// - if topic name is set: /<parent_model_name>/<topic_name>
// - if topic name is not set: /<parent_model_name>/<sensor_name>
// where spaces are replaced by "_" and special characters by "" to ensure a
// valid topic name.
absl::StatusOr<GeneratedSensorInfo> GetSensorsInImmediateAttachmentChildren(
    const World& world, AttachmentEntityId attachment_id,
    std::string_view parent_model_name,
    const WorldSdfAdapter::Options& conversion_options) {
  auto children = world.GetChildrenOf(attachment_id);
  std::vector<AttachmentEntityId> children_with_sensor_comp;
  for (const auto& id : children) {
    INTR_ASSIGN_OR_RETURN(const auto* entity, world.GetEntityById(id));
    if (entity->HasComponent<SensorComponent>()) {
      children_with_sensor_comp.push_back(id);
    }
  }
  std::string result;
  std::vector<EntityId> sensor_ids;
  std::vector<GzTopicInfo> topic_infos;
  for (const auto& id : children_with_sensor_comp) {
    INTR_ASSIGN_OR_RETURN(const auto* entity, world.GetEntityById(id));
    INTR_ASSIGN_OR_RETURN(const auto* sensor,
                          entity->GetComponent<SensorComponent>());
    ::sdf::SensorType sdf_sensor_type;
    switch (sensor->GetType()) {
      case SensorType::kInvalid:
        LOG(WARNING) << "Sensor named '" << entity->GetAlias()
                     << "' has type unspecified; skipping.";
        continue;
      case SensorType::kCamera:
        sdf_sensor_type = ::sdf::SensorType::CAMERA;
        break;
      case SensorType::kDepthCamera:
        sdf_sensor_type = ::sdf::SensorType::RGBD_CAMERA;
        break;
      case SensorType::kForceTorque:
        sdf_sensor_type = ::sdf::SensorType::FORCE_TORQUE;
        break;
      case SensorType::kLidar:
        sdf_sensor_type = ::sdf::SensorType::GPU_LIDAR;
        break;
    }

    INTR_ASSIGN_OR_RETURN(std::string sensor_name,
                          GetEntityName(world, id, /*type_string=*/"sensor",
                                        /*prepend_entity_id=*/false));

    bool is_triggered_camera =
        absl::GetFlag(FLAGS_force_trigger_cameras) &&
        (sdf_sensor_type == ::sdf::SensorType::CAMERA ||
         sdf_sensor_type == ::sdf::SensorType::RGBD_CAMERA);

    // TODO(b/337935234) Override update_rate to 0 only for F/T sensors used for
    // robot control.
    // Setting update rate to 0 forces Gazebo to update the sensor measurement
    // at each time step.
    double update_rate = sensor->GetUpdateRate();
    if (sdf_sensor_type == ::sdf::SensorType::FORCE_TORQUE && update_rate > 0) {
      LOG(INFO)
          << "Forcing update_rate for ForceTorque sensor to be 0 so that "
             "the sensor is updated at each timestep in sim, was previously "
          << update_rate << "Hz. "
          << "This is required for ICON controllers using this F/T sensor "
             "to work correctly.";
      update_rate = 0;
    }

    // Find camera spec overrides for this sensor.
    const WorldObjectPlugin::CameraSpec::SensorProperties*
        camera_prop_override = nullptr;
    if (auto camera_spec_it =
            conversion_options.collection_entity_local_name_to_camera_spec.find(
                parent_model_name);
        camera_spec_it !=
        conversion_options.collection_entity_local_name_to_camera_spec.end()) {
      auto prop_it =
          camera_spec_it->second.sensor_properties_overrides.find(sensor_name);
      if (prop_it != camera_spec_it->second.sensor_properties_overrides.end()) {
        camera_prop_override = &prop_it->second;
      }
    }

    Pose3d sensor_pose = world.GetTransform(attachment_id, id);
    if (camera_prop_override != nullptr &&
        camera_prop_override->parent_t_sensor.has_value()) {
      sensor_pose = *camera_prop_override->parent_t_sensor;
    }

    absl::StrAppend(&result, "<sensor name='", sensor_name, "' type='",
                    sdf::GetSensorTypeString(sdf_sensor_type), "'>");
    absl::StrAppend(&result, Pose3ToPoseString(sdf::SensorPoseToSdf(
                                 sensor_pose, sdf_sensor_type)));
    absl::StrAppend(&result, "<update_rate>", FullPrecision(update_rate),
                    "</update_rate>");
    std::string topic = sensor->GetTopic();
    if (conversion_options.ensure_unique_sensor_topic_names) {
      CHECK(!parent_model_name.empty());
      if (!topic.empty()) {
        std::string topic_without_starting_slash =
            (topic.starts_with('/') ? topic.substr(1) : topic);
        topic = absl::Substitute("/$0/$1", parent_model_name,
                                 topic_without_starting_slash);
      } else {
        topic = absl::Substitute("/$0/$1", parent_model_name, sensor_name);
      }
      topic = gz::transport::TopicUtils::AsValidTopic(topic);
    }
    std::optional<std::string> trigger_topic = std::nullopt;
    if (!topic.empty()) {
      absl::StrAppend(&result, "<topic>", topic, "</topic>");
      sensor_ids.push_back(id);
      topic_infos.push_back(GzTopicInfo{
          .topic_name = topic,
          .sensor_info =
              GzTopicInfo::SensorInfo{
                  .name = sensor_name,
                  .type = sdf::GetSensorTypeString(sdf_sensor_type),
                  .is_trigger_topic = false,
                  .update_rate = static_cast<int64_t>(update_rate),
              },
          .is_sim_server_advertised = true,
      });
      if (is_triggered_camera) {
        trigger_topic = absl::StrCat(topic, "/trigger");
        topic_infos.push_back(GzTopicInfo{
            .topic_name = *trigger_topic,
            .sensor_info =
                GzTopicInfo::SensorInfo{
                    .name = sensor_name,
                    .type = sdf::GetSensorTypeString(sdf_sensor_type),
                    .is_trigger_topic = true,
                    .update_rate = static_cast<int64_t>(update_rate),
                },
            .is_sim_server_advertised = false,
        });
      }
    }

    if (sdf_sensor_type == ::sdf::SensorType::CAMERA) {
      INTR_ASSIGN_OR_RETURN(
          intrinsic_proto::world::SensorComponent::Camera camera_spec,
          sensor->GetCameraSpec());
      auto& properties = *camera_spec.mutable_properties();
      if (camera_prop_override != nullptr &&
          camera_prop_override->intrinsics.has_value()) {
        const auto& override_params = *camera_prop_override->intrinsics;
        *properties.mutable_intrinsics() = override_params.intrinsics;
        properties.mutable_image()->set_width(override_params.image_width);
        properties.mutable_image()->set_height(override_params.image_height);
        properties.set_horizontal_fov(override_params.horizontal_fov);
      }
      INTR_ASSIGN_OR_RETURN(std::string camera,
                            CommonCameraPropertiesToString(
                                properties, sensor_name, trigger_topic));
      absl::StrAppend(&result, camera);
    } else if (sdf_sensor_type == ::sdf::SensorType::RGBD_CAMERA) {
      INTR_ASSIGN_OR_RETURN(
          intrinsic_proto::world::SensorComponent::DepthCamera depth_spec,
          sensor->GetDepthCameraSpec());
      auto& properties = *depth_spec.mutable_properties();
      if (camera_prop_override != nullptr &&
          camera_prop_override->intrinsics.has_value()) {
        const auto& override_params = *camera_prop_override->intrinsics;
        *properties.mutable_intrinsics() = override_params.intrinsics;
        properties.mutable_image()->set_width(override_params.image_width);
        properties.mutable_image()->set_height(override_params.image_height);
        properties.set_horizontal_fov(override_params.horizontal_fov);
      }
      INTR_ASSIGN_OR_RETURN(std::string camera,
                            CommonCameraPropertiesToString(
                                properties, sensor_name, trigger_topic));
      absl::StrAppend(&result, camera);
    } else if (sdf_sensor_type == ::sdf::SensorType::FORCE_TORQUE) {
      INTR_ASSIGN_OR_RETURN(
          intrinsic_proto::world::SensorComponent::ForceTorque spec,
          sensor->GetForceTorqueSpec());
      INTR_ASSIGN_OR_RETURN(std::string force_torque,
                            ForceTorqueSpecToString(spec, sensor_name));
      absl::StrAppend(&result, force_torque);
    } else if (sdf_sensor_type == ::sdf::SensorType::GPU_LIDAR) {
      INTR_ASSIGN_OR_RETURN(intrinsic_proto::world::SensorComponent::Lidar spec,
                            sensor->GetLidarSpec());
      INTR_ASSIGN_OR_RETURN(std::string lidar, LidarSpecToString(spec));
      absl::StrAppend(&result, lidar);
    }
    INTR_ASSIGN_OR_RETURN(SimulatorPluginsResult sim_plugins_result,
                          GetSimulatorPlugins(entity, parent_model_name,
                                              conversion_options, world));
    absl::StrAppend(&result, sim_plugins_result.plugins_sdf);
    absl::StrAppend(&result, "</sensor>");

    topic_infos.reserve(topic_infos.size() +
                        sim_plugins_result.topic_infos.size());
    std::ranges::move(sim_plugins_result.topic_infos,
                      std::back_inserter(topic_infos));
  }
  return GeneratedSensorInfo{.sdf_string = result,
                             .sensor_ids = std::move(sensor_ids),
                             .topic_infos = std::move(topic_infos)};
}

struct GeneratedProjectorInfo {
  std::string sdf_string;
};

absl::StatusOr<GeneratedProjectorInfo>
GetProjectorsInImmediateAttachmentChildren(
    const World& world, AttachmentEntityId attachment_id,
    const WorldSdfAdapter::Options& conversion_options) {
  auto children = world.GetChildrenOf(attachment_id);
  std::vector<AttachmentEntityId> children_with_projector_comp;
  for (const auto& id : children) {
    INTR_ASSIGN_OR_RETURN(const auto* entity, world.GetEntityById(id));
    if (entity->HasComponent<ProjectorComponent>()) {
      children_with_projector_comp.push_back(id);
    }
  }
  std::string result;
  for (const auto& id : children_with_projector_comp) {
    INTR_ASSIGN_OR_RETURN(const auto* entity, world.GetEntityById(id));
    INTR_ASSIGN_OR_RETURN(const auto* projector,
                          entity->GetComponent<ProjectorComponent>());
    INTR_ASSIGN_OR_RETURN(std::string projector_name,
                          GetEntityName(world, id,
                                        /*type_string=*/"projector",
                                        /*prepend_entity_id=*/false));
    absl::StrAppend(&result, "<projector name='", projector_name, "'>");
    absl::StrAppend(&result, "<fov>",
                    FullPrecision(projector->GetHorizontalFov()), "</fov>");
    absl::StrAppend(&result, "<near_clip>",
                    FullPrecision(projector->GetNearClip()), "</near_clip>");
    absl::StrAppend(&result, "<far_clip>",
                    FullPrecision(projector->GetFarClip()), "</far_clip>");
    absl::StrAppend(&result, "<visibility_flags>",
                    projector->GetVisibilityFlags(), "</visibility_flags>");

    // Save texture to same path as mesh for now
    absl::string_view texture_savepath = conversion_options.mesh_savepath;
    std::string full_texture_path;
    if (texture_savepath.empty()) {
      INTR_ASSIGN_OR_RETURN(
          std::string link_name,
          GetEntityName(world, attachment_id, /*type_string=*/"link",
                        /*prepend_entity_id=*/false));
      LOG(WARNING) << "mesh_savepath is not set -- projector texture will be "
                   << "empty for link '" << link_name << "'";
    } else {
      const Texture& texture = projector->GetTexture();
      const std::string fingerprint = std::to_string(MixTwoUInt64(
          ThoroughHash(texture.data.data(), texture.data.size()),
          ThoroughHash(texture.format.data(), texture.format.size())));
      std::string texture_filename =
          absl::StrCat(fingerprint, ".", texture.format);
      full_texture_path = file::JoinPath(texture_savepath, texture_filename);
      if (!file::Exists(texture_filename, file::Defaults()).ok()) {
        INTR_RETURN_IF_ERROR(file::SetContents(full_texture_path, texture.data,
                                               file::Defaults()));
      }
    }
    absl::StrAppend(&result, "<texture>", full_texture_path, "</texture>");
    absl::StrAppend(&result, "</projector>");
  }
  return GeneratedProjectorInfo{.sdf_string = result};
}

struct GeneratedLinkInfo {
  std::string name;
  std::string sdf_string;
  std::vector<EntityId> sensor_ids;
  std::vector<GzTopicInfo> topic_infos;
};

absl::StatusOr<std::string> GetInertial(
    const World& world, AttachmentEntityId attachment_id,
    const WorldSdfAdapter::Options& conversion_options,
    const bool has_collision_geo) {
  INTR_ASSIGN_OR_RETURN(const auto* entity, world.GetEntityById(attachment_id));
  const PhysicsComponent* physics =
      entity->GetComponent<PhysicsComponent>().value_or(nullptr);
  if (physics == nullptr) {
    return "";
  }

  InertialOptions options = {.enable_auto_inertial =
                                 absl::GetFlag(FLAGS_enable_auto_inertial)};

  InertialProperties properties;
  properties.has_collision_geo = has_collision_geo;
  if (options.enable_auto_inertial) {
    // Set //inertial/@auto to true if a link is non-static, has at least
    // one collision geometry, and the physics component contains
    // default unit inertial values, i.e. mass = 1kg and unit inertia matrix.
    INTR_ASSIGN_OR_RETURN(const auto* attachment,
                          entity->GetComponent<AttachmentComponent>());
    INTR_ASSIGN_OR_RETURN(auto* parent_entity,
                          world.GetEntityById(attachment->GetParentId()));
    if (parent_entity->HasComponent<SimulationComponent>()) {
      INTR_ASSIGN_OR_RETURN(const auto* sim_component,
                            parent_entity->GetComponent<SimulationComponent>());
      properties.is_static = sim_component->IsStatic();
    }
  }

  INTR_ASSIGN_OR_RETURN(auto physics_proto, physics->ToProto());
  auto result = InertialSpecToString(physics_proto, options, properties);
  if (!result.ok()) {
    INTR_ASSIGN_OR_RETURN(
        std::string link_name,
        GetEntityName(world, attachment_id, /*type_string=*/"link",
                      /*prepend_entity_id=*/false));
    return absl::Status(
        result.status().code(),
        absl::StrCat(result.status().message(), " for link ", link_name));
  }
  return result;
}

absl::StatusOr<GeneratedLinkInfo> WorldEntityToLink(
    const std::shared_ptr<opentelemetry::trace::Span>& parent_span,
    const World& world, AttachmentEntityId attachment_id,
    AttachmentEntityId pose_reference_id, std::string_view parent_model_name,
    const WorldSdfAdapter::Options& conversion_options,
    std::optional<uint32_t> collision_mask) {
  intrinsic::stats::ScopedSpan entity_span(
      "WorldToSdfConverter/WorldEntityToLink", parent_span);
  INTR_ASSIGN_OR_RETURN(const auto* entity, world.GetEntityById(attachment_id));

  std::string result;
  INTR_ASSIGN_OR_RETURN(
      std::string link_name,
      GetEntityName(world, attachment_id, /*type_string=*/"link",
                    /*prepend_entity_id=*/false));

  absl::StrAppend(&result, "<link name='", link_name, "'>");
  absl::StrAppend(&result, Pose3ToPoseString(world.GetTransform(
                               pose_reference_id, attachment_id)));

  bool has_collision_geo = false;

  const GeometryComponent* geometry = ValidateGeometry(*entity);
  absl::string_view mesh_savepath = conversion_options.mesh_savepath;
  if (mesh_savepath.empty()) {
    // TODO(shameek): We might still have the ability to output primitives here.
    LOG(WARNING) << "mesh_savepath is not set -- skipping geometry for link '"
                 << link_name << "'";
  } else if (geometry != nullptr) {
    if (const auto& visual_geo = geometry->GetGeometry(kKindVisualGeometry);
        visual_geo.ok()) {
      for (const auto& [name, tg] : *visual_geo) {
        const Geometry& geo = tg.shape();
        INTR_ASSIGN_OR_RETURN(auto renderable,
                              GenerateRenderableWithMaterialOverrides(geo));
        INTR_RET_CHECK(renderable != nullptr);
        const std::string renderable_fingerprint =
            GenerateFingerprint(*renderable);

        const std::string extension = "glb";

        std::shared_ptr<opentelemetry::trace::Span> write_geo_span =
            stats::GetTracer()->StartSpan(
                "WorldToSdfConverter/WorldEntityToLink/WriteGeometry");
        write_geo_span->SetAttribute("renderable_data_filename",
                                     "renderable.glb");
        const std::string mesh_filename =
            absl::StrCat(renderable_fingerprint, ".", extension);
        const std::string full_mesh_path =
            file::JoinPath(mesh_savepath, mesh_filename);
        if (!file::Exists(mesh_filename, file::Defaults()).ok()) {
          INTR_RETURN_IF_ERROR(file::SetContents(
              full_mesh_path, renderable->GetGLBString(), file::Defaults()));
          write_geo_span->SetAttribute("file_exists", false);
        }

        write_geo_span->SetAttribute("mesh_filename", mesh_filename);
        write_geo_span->End();

        absl::StrAppend(&result, "<visual name='", name, "'>");
        INTR_ASSIGN_OR_RETURN((auto [ref_t_shape, scale]),
                              matrixToPoseAndScale(tg.ref_t_shape()));
        absl::StrAppend(&result, Pose3ToPoseString(ref_t_shape));
        absl::StrAppend(&result, "<geometry>");
        absl::StrAppend(&result, "<mesh>");
        absl::StrAppend(&result, "<uri>model://", mesh_filename, "</uri>");
        if (!scale.isApprox(eigenmath::Vector3d::Ones())) {
          absl::StrAppend(&result, "<scale>", Vec3ToString(scale.array()),
                          "</scale>");
        }
        absl::StrAppend(&result, "</mesh>");
        absl::StrAppend(&result, "</geometry></visual>");
      }
    } else {
      LOG_IF(WARNING, !visual_geo.ok())
          << "Expected visual geometry for link '" << link_name << "' but none "
          << "found: " << visual_geo.status();
    }

    // Get collision surface user data, if present.
    std::string collision_surface;
    const UserDataComponent* user_data =
        entity->GetComponent<UserDataComponent>().value_or(nullptr);
    const PhysicsComponent* physics =
        entity->GetComponent<PhysicsComponent>().value_or(nullptr);
    if (physics != nullptr) {
      absl::StrAppend(&collision_surface, "<surface>");
      std::string contact = "<contact>";
      {
        if (physics->GetContactKp().has_value() ||
            physics->GetContactKd().has_value()) {
          std::string kp_kd;
          if (physics->GetContactKp().has_value()) {
            absl::StrAppend(&kp_kd, "<kp>",
                            FullPrecision(*physics->GetContactKp()), "</kp>");
          }
          if (physics->GetContactKd().has_value()) {
            absl::StrAppend(&kp_kd, "<kd>",
                            FullPrecision(*physics->GetContactKd()), "</kd>");
          }
          absl::StrAppend(&contact,
                          absl::Substitute("<bullet>$0</bullet>", kp_kd));
        }

        // If this entity has a collision component, then we should use it to
        // see if we have a collision response
        auto maybe_collision_component =
            entity->GetComponent<CollisionComponent>();
        if (maybe_collision_component.ok() &&
            !(maybe_collision_component.value()->HasCollisionResponse())) {
          absl::StrAppend(
              &contact,
              "<collide_without_contact>true</collide_without_contact>");
        } else {
          absl::StrAppend(
              &contact,
              "<collide_without_contact>false</collide_without_contact>");
        }
        std::string collision_bitmask = absl::Substitute(
            "<collide_without_contact_bitmask>$0"
            "</collide_without_contact_bitmask>"
            "<collide_bitmask>$0</collide_bitmask>",
            collision_mask.value_or(kDefaultCollisionBitMask));
        absl::StrAppend(&contact, collision_bitmask);
      }
      absl::StrAppend(&contact, "</contact>");
      absl::StrAppend(&collision_surface, contact);
      std::string ode_friction = absl::Substitute(
          "<ode><mu>$0</mu><mu2>$1</mu2><slip1>$2</slip1><slip2>$3</"
          "slip2><fdir1>$4 $5 $6</fdir1></ode>",
          FullPrecision(physics->GetSurfaceFriction().mu()),
          FullPrecision(physics->GetSurfaceFriction().mu2()),
          FullPrecision(physics->GetSurfaceFriction().slip1()),
          FullPrecision(physics->GetSurfaceFriction().slip2()),
          FullPrecision(physics->GetSurfaceFriction().fdir1().x()),
          FullPrecision(physics->GetSurfaceFriction().fdir1().y()),
          FullPrecision(physics->GetSurfaceFriction().fdir1().z()));
      std::string bullet_friction = absl::Substitute(
          "<bullet><friction>$0</friction><friction2>$1</friction2><fdir1>"
          "$2 $3 $4</fdir1></bullet>",
          FullPrecision(physics->GetSurfaceFriction().mu()),
          FullPrecision(physics->GetSurfaceFriction().mu2()),
          FullPrecision(physics->GetSurfaceFriction().fdir1().x()),
          FullPrecision(physics->GetSurfaceFriction().fdir1().y()),
          FullPrecision(physics->GetSurfaceFriction().fdir1().z()));
      std::string torsional = absl::Substitute(
          "<torsional><coefficient>$0</coefficient><patch_radius>$1</"
          "patch_radius><surface_radius>$2</"
          "surface_radius><use_patch_radius>$3</"
          "use_patch_radius><ode><slip>$4</slip></ode></torsional>",
          FullPrecision(physics->GetTorsional().coefficient()),
          FullPrecision(physics->GetTorsional().patch_radius()),
          FullPrecision(physics->GetTorsional().surface_radius()),
          static_cast<int>(physics->GetTorsional().use_patch_radius()),
          FullPrecision(physics->GetTorsional().ode_slip()));
      absl::StrAppend(&collision_surface, "<friction>", ode_friction,
                      bullet_friction, torsional, "</friction>");
      absl::StrAppend(&collision_surface, "</surface>");
    } else if (user_data != nullptr) {
      auto itr = user_data->UserDataMap().find(
          ::intrinsic::sdf::kGazeboCollisionSurface);
      if (itr != user_data->UserDataMap().end()) {
        collision_surface = itr->second;
      }

      LOG_IF(WARNING, collision_mask.has_value())
          << "For link '" << link_name << "': Setting collision bitmasks for "
          << "custom collision surfaces is unsupported.";
    }

    if (const auto collision_geo =
            geometry->GetGeometry(kKindCollisionGeometry);
        collision_geo.ok()) {
      for (const auto& [name, tg] : *collision_geo) {
        std::string collision_str;

        absl::StrAppend(&collision_str, "<collision name='", name, "'>");
        INTR_ASSIGN_OR_RETURN((auto [ref_t_shape, scale]),
                              matrixToPoseAndScale(tg.ref_t_shape()));
        absl::StrAppend(&collision_str, Pose3ToPoseString(ref_t_shape));
        absl::StrAppend(&collision_str, "<geometry>");
        const Geometry& geo = tg.shape();
        const auto& shape = geo.GetExactGeometry();
        const auto original_blue_shapes = shape.GetPrimitiveShapes();

        // Check the blue shape types to see if we can output them.
        bool should_output_primitives = !original_blue_shapes.empty();

        std::vector<geo::PrimitiveShapePtr> scaled_shapes;
        for (const auto& blue_shape : original_blue_shapes) {
          if (!blue_shape.ref_t_shape().isApprox(
                  eigenmath::Matrix4d::Identity())) {
            // TODO(stoyang): This should be easy to improve with <pose> tags
            // and using TransformedPrimitiveShapePtr.
            LOG(WARNING)
                << "Non-identity transform for collision geometry primitive: "
                << blue_shape.ref_t_shape();
            should_output_primitives = false;
            break;
          }

          // Attempt to scale the blue shape.
          auto scaled_shape = ScaleShape(*blue_shape.shape(), scale);
          if (!scaled_shape.ok()) {
            should_output_primitives = false;
            break;
          }

          // Find out if the scaled shape is a supported type.
          if (std::ranges::find(kPrimitiveTypesSupportedByGazebo,
                                (*scaled_shape)->getType()) ==
              kPrimitiveTypesSupportedByGazebo.end()) {
            should_output_primitives = false;
            break;
          }

          // Once we know that the shape was scaled successfully and is a
          // supported type, we can add it to the list of shapes to output.
          scaled_shapes.emplace_back(std::move(scaled_shape).value());
        }

        if (should_output_primitives) {
          for (const auto& blue_shape : scaled_shapes) {
            switch (blue_shape->getType()) {
              case ShapeType::BOX: {
                const auto& box = blue_shape->get<Box>();
                absl::StrAppend(&collision_str, "<box><size>",
                                Vec3ToString(box.getSize()), "</size></box>");
                break;
              }
              case ShapeType::CYLINDER: {
                const auto& cylinder = blue_shape->get<Cylinder>();
                absl::StrAppend(&collision_str, "<cylinder>");
                absl::StrAppend(&collision_str, "<radius>",
                                FullPrecision(cylinder.getRadius()),
                                "</radius>");
                absl::StrAppend(&collision_str, "<length>",
                                FullPrecision(cylinder.getLength()),
                                "</length>");
                absl::StrAppend(&collision_str, "</cylinder>");
                break;
              }
              case ShapeType::SPHERE: {
                const auto& sphere = blue_shape->get<Sphere>();
                absl::StrAppend(&collision_str, "<sphere>");
                absl::StrAppend(&collision_str, "<radius>",
                                FullPrecision(sphere.getRadius()), "</radius>");
                absl::StrAppend(&collision_str, "</sphere>");
                break;
              }
              case ShapeType::ELLIPSOID: {
                const auto& ellipsoid = blue_shape->get<Ellipsoid>();
                absl::StrAppend(&collision_str, "<ellipsoid><radii>",
                                Vec3ToString(ellipsoid.getRadii()),
                                "</radii></ellipsoid>");
                break;
              }
              case ShapeType::CAPSULE: {
                const auto& capsule = blue_shape->get<Capsule>();
                absl::StrAppend(&collision_str, "<capsule>");
                absl::StrAppend(&collision_str, "<radius>",
                                FullPrecision(capsule.getRadius()),
                                "</radius>");
                absl::StrAppend(&collision_str, "<length>",
                                FullPrecision(capsule.getLength()),
                                "</length>");
                absl::StrAppend(&collision_str, "</capsule>");
                break;
              }
              default: {
                return intrinsic::InternalErrorBuilder().LogError()
                       << "Unsupported blue shape type: "
                       << shapes::ToString(blue_shape->getType());
              }
            }
          }
        } else {
          std::string mesh_filename;
          intrinsic::stats::ScopedSpan collision_geo_span(
              "WorldToSdfConverter/WorldEntityToLink/WriteCollisionGeometry",
              parent_span);

          // Convex decomposition is computed by Gazebo when loading the world
          // and is expensive. For a convex mesh, a fine-grained decomposition
          // does not improve the collision geometry, so a single hull at a
          // coarse voxel resolution is requested instead. Convex
          // decomposition is still requested (rather than a plain mesh) so
          // that the physics engine represents the mesh as a convex hull
          // shape, which is more stable and efficient than a triangle mesh.
          bool is_convex_mesh = false;
          if (shape.HasMesh()) {
            if (absl::GetFlag(FLAGS_enable_convex_decomposition)) {
              // Failing to determine convexity falls back to the full convex
              // decomposition, which is always correct.
              if (const absl::StatusOr<bool> is_convex = IsConvex(geo);
                  is_convex.ok()) {
                collision_geo_span.AddAttribute("is_convex", *is_convex);
                is_convex_mesh = *is_convex;
              } else {
                LOG(WARNING) << "Failed to determine convexity of collision "
                                "geometry for link '"
                             << link_name << "': " << is_convex.status();
                collision_geo_span.AddAttribute("is_convex_error", true);
              }
            }
            INTR_ASSIGN_OR_RETURN(auto fingerprint, GenerateFingerprint(geo));
            mesh_filename = absl::StrCat(fingerprint, ".stl");
            const std::string full_mesh_path =
                file::JoinPath(mesh_savepath, mesh_filename);

            const bool file_exists =
                file::Exists(full_mesh_path, file::Defaults()).ok();
            collision_geo_span.AddAttribute("file_exists", file_exists);

            if (!file_exists) {
              INTR_ASSIGN_OR_RETURN(auto mesh_ref, shape.GetMesh());
              INTR_RETURN_IF_ERROR(geo::legacy::SaveMeshToBinaryStlFile(
                  full_mesh_path, mesh_ref.Value()));
            }
          } else {
            // TODO(b/413736464): Add support for point cloud collision geo.
            if (conversion_options.skip_unsupported_collision_geos) {
              LOG(WARNING) << "Unsupported geometry for link '" << link_name
                           << "'.";
              collision_geo_span.AddAttribute("geo_skipped", true);
              continue;
            } else {
              return intrinsic::InvalidArgumentErrorBuilder().LogError()
                     << "Unsupported geometry for link '" << link_name << "'.";
            }
          }

          collision_geo_span.AddAttribute("mesh_filename", mesh_filename);
          if (absl::GetFlag(FLAGS_enable_convex_decomposition)) {
            const std::optional<uint32_t> voxel_resolution =
                geo.GetExactGeometry()
                    .options()
                    .simulation_convex_decomposition_resolution;
            absl::StrAppend(&collision_str,
                            "<mesh optimization='convex_decomposition'>");
            absl::StrAppend(&collision_str, "<convex_decomposition>");
            if (is_convex_mesh) {
              absl::StrAppend(&collision_str, "<max_convex_hulls>",
                              kConvexMeshMaxConvexHulls, "</max_convex_hulls>",
                              "<voxel_resolution>",
                              std::min(voxel_resolution.value_or(
                                           kConvexMeshVoxelResolution),
                                       kConvexMeshVoxelResolution),
                              "</voxel_resolution>");
            } else {
              absl::StrAppend(&collision_str, "<max_convex_hulls>",
                              absl::GetFlag(FLAGS_sim_max_convex_hulls),
                              "</max_convex_hulls>");
              if (voxel_resolution.has_value()) {
                absl::StrAppend(&collision_str, "<voxel_resolution>",
                                *voxel_resolution, "</voxel_resolution>");
              }
            }
            absl::StrAppend(&collision_str, "</convex_decomposition>");
          } else {
            absl::StrAppend(&collision_str, "<mesh>");
          }
          absl::StrAppend(&collision_str, "<uri>model://", mesh_filename,
                          "</uri>");
          absl::StrAppend(&collision_str, "<scale>", Vec3ToString(scale),
                          "</scale>");
          absl::StrAppend(&collision_str, "</mesh>");
        }
        absl::StrAppend(&collision_str, "</geometry>");
        absl::StrAppend(&collision_str, collision_surface);
        absl::StrAppend(&collision_str, "</collision>");

        absl::StrAppend(&result, collision_str);
        has_collision_geo = true;
      }
    } else {
      LOG_IF(WARNING, !collision_geo.ok())
          << "Expected collision geometry for link '" << link_name
          << "' but none found: " << collision_geo.status();
    }
  }

  INTR_ASSIGN_OR_RETURN(
      const std::string inertial_str,
      GetInertial(world, attachment_id, conversion_options, has_collision_geo));
  absl::StrAppend(&result, inertial_str);

  auto sensors_or = GetSensorsInImmediateAttachmentChildren(
      world, attachment_id, parent_model_name, conversion_options);
  std::vector<EntityId> sensor_ids;
  std::vector<GzTopicInfo> topic_infos;
  if (sensors_or.ok()) {
    absl::StrAppend(&result, sensors_or->sdf_string);
    sensor_ids = std::move(sensors_or->sensor_ids);
    topic_infos = std::move(sensors_or->topic_infos);
  } else {
    if (conversion_options.skip_failed_sensors) {
      LOG(WARNING)
          << "Something went wrong when trying to find sensors under link '"
          << entity->GetLocalName() << "': " << sensors_or.status();
    } else {
      return sensors_or.status();
    }
  }

  auto projectors_or = GetProjectorsInImmediateAttachmentChildren(
      world, attachment_id, conversion_options);
  if (projectors_or.ok()) {
    absl::StrAppend(&result, projectors_or->sdf_string);
  } else {
    if (conversion_options.skip_failed_sensors) {
      LOG(WARNING)
          << "Something went wrong when trying to find projectors under link '"
          << entity->GetLocalName() << "': " << projectors_or.status();
    } else {
      return projectors_or.status();
    }
  }

  absl::StrAppend(&result, "</link>");
  return GeneratedLinkInfo{.name = link_name,
                           .sdf_string = result,
                           .sensor_ids = std::move(sensor_ids),
                           .topic_infos = std::move(topic_infos)};
}

// Removes exclusions between links that are in the same model and returns a
// mapping of collision entities to bitmasks that can be used to define
// collision exclusions according to the SDF spec.
absl::StatusOr<absl::flat_hash_map<CollisionEntityId, uint32_t>>
ProcessCollisionExclusions(World& world) {
  intrinsic::stats::ScopedSpan collision_exclusion_span(
      "WorldToSdfConverter/ProcessCollisionExclusions");

  // Collect collision exclusion graph locally first.
  std::vector<CollisionEntityId> collision_ids =
      world.GetTypedEntityIds<CollisionEntityId>();
  std::sort(collision_ids.begin(), collision_ids.end());

  // If a collision_id is also a collections member, then we can remove
  // all other collections members from the collision exclusion set, since
  // models don't self-intersect by default.
  for (auto coll_id : collision_ids) {
    auto member_id = world.ValidateEntity<CollectionsMemberEntityId>(coll_id);
    if (!member_id.ok()) {
      continue;
    }

    INTR_ASSIGN_OR_RETURN(
        auto* collections_member,
        world.GetComponentByEntityId<CollectionsMemberComponent>(*member_id));

    // We only collide against links...
    INTR_ASSIGN_OR_RETURN(CollectionsEntityId parent_collection_id,
                          collections_member->FindParentCollectionAmongTypes(
                              {CollectionsComponent::kLinks}));
    INTR_ASSIGN_OR_RETURN(auto* parent_collection,
                          world.GetComponentByEntityId<CollectionsComponent>(
                              parent_collection_id));
    std::vector<CollectionsMemberEntityId> siblings =  // "siblinks"
        parent_collection->GetCollectionMembers(CollectionsComponent::kLinks);

    // Remove siblings from collision exclusions.
    INTR_ASSIGN_OR_RETURN(
        auto* collision,
        world.GetComponentByEntityId<CollisionComponent>(coll_id));
    for (auto sibling : siblings) {
      INTR_ASSIGN_OR_RETURN(
          auto physical_sibling_id,
          world.ValidateEntity<PhysicalEntityId>(sibling),
          _ << "Collection has links that are not physical entity IDs??");
      collision->RemoveExclusionId(physical_sibling_id);
    }
  }

  // Build the graph
  absl::flat_hash_map<uint32_t, absl::flat_hash_set<uint32_t>> exclusion_graph;
  for (auto coll_id : collision_ids) {
    INTR_ASSIGN_OR_RETURN(
        auto* collision,
        world.GetComponentByEntityId<CollisionComponent>(coll_id));
    absl::flat_hash_set<uint32_t> exclusion_set;
    for (const auto& v : collision->GetExclusions()) {
      exclusion_set.insert(v.value());
    }
    if (!exclusion_set.empty()) {
      exclusion_graph[coll_id.value()] = exclusion_set;
    }
  }

  absl::flat_hash_map<uint32_t, uint32_t> bitmasks;
  INTR_ASSIGN_OR_RETURN(bitmasks, GenerateCollisionBitmasks(exclusion_graph));
  absl::flat_hash_map<CollisionEntityId, uint32_t> result;
  for (const CollisionEntityId collision_id : collision_ids) {
    uint32_t cid = collision_id.value();
    if (bitmasks.contains(cid)) {
      result[collision_id] = bitmasks[cid];
    } else {
      result[collision_id] = kDefaultCollisionBitMask;
    }
  }

  return result;
}

std::string GenerateModelDataTag(CollectionsEntityId coll_id,
                                 const object_world::ObjectWorld* world) {
  stats::ScopedSpan generate_model_data_span(absl::StrCat(
      "WorldToSdfConverter/GenerateModelDataTag", coll_id.value()));

  std::string result;
  absl::StrAppend(&result, "<", kWorldModelConfigPlugin_ModelDataTag, " ",
                  kWorldModelConfigPlugin_WorldIdTag, "='", coll_id.value(),
                  "' ");

  ObjectWorldResourceId object_world_id =
      object_world::ObjectWorldResourceIdForObject(coll_id);
  absl::StrAppend(&result, kWorldModelConfigPlugin_WorldObjectResourceIdTag,
                  "='", object_world_id, "' ");

  absl::StatusOr<const object_world::WorldObject*> world_object =
      world->GetObject(object_world_id);
  if (world_object.ok()) {
    absl::StrAppend(&result, kWorldModelConfigPlugin_WorldObjectNameTag, "='",
                    (*world_object)->GetName(), "' ");
  } else {
    LOG(ERROR) << "Could not find world object for model with ID: "
               << object_world_id;
  }

  auto* entity = world->GetEntityWorld().GetEntityById(coll_id).value();
  const auto* ppr = entity->GetComponent<PPRComponent>().value_or(nullptr);
  if (ppr != nullptr) {
    std::optional<absl::string_view> resource_name = ppr->ResourceName();
    if (resource_name.has_value()) {
      absl::StrAppend(&result, kWorldModelConfigPlugin_ResourceNameTag, "='",
                      *resource_name, "' ");
    }
  }

  absl::StrAppend(&result, "/>");  // kModelDataTag
  return result;
}

// Get the /physics/max_step_size property in the sdformat world.
//
// Optionally, an override value can be provided. The `/physics/max_step_size`
// child element will be replaced with the given override value if provided.
//
// The world should have exactly one `physics` element.
absl::StatusOr<double> GetPhysicsMaxStepSizeWithOverride(
    ::sdf::World& sdf_world, std::optional<double> max_step_size_sec) {
  if (sdf_world.PhysicsCount() > 1) {
    return InvalidArgumentErrorBuilder()
           << "Sdf world should have at most 1 physics element, got "
           << sdf_world.PhysicsCount();
  }

  // The SDFormat spec guarantees that a `Physics` object is always present in
  // the world.
  ::sdf::Physics* physics = sdf_world.PhysicsByIndex(0);
  CHECK_NE(physics, nullptr);

  if (max_step_size_sec.has_value()) {
    LOG(INFO) << "Overriding sim step size in sdf world to "
              << *max_step_size_sec << "sec.";
    physics->SetMaxStepSize(*max_step_size_sec);

    // Update the xml element from the DOM.
    auto original_physics_element = physics->Element();
    auto updated_physics_element = physics->ToElement();
    if (original_physics_element != nullptr) {
      sdf_world.Element()->RemoveChild(original_physics_element);
    }
    sdf_world.Element()->InsertElement(updated_physics_element,
                                       /*_setParentToSelf=*/true);
  }

  return physics->MaxStepSize();
}

// Sets the `<kinematic>` child element of the `gz::sim::systems::Physics`
// plugin in the sdformat world:
//   <kinematic>
//     <enabled>true|false</enabled>
//     <contacts><enabled>true|false</enabled></contacts>  <!-- if set -->
//   </kinematic>
//
// The plugin is expected to be provided by the world template. Returns a
// NotFoundError if the world does not contain a Physics plugin. Plugin contents
// are free-form XML, so the element tree is edited directly (sdformat keeps
// unknown plugin children as string-valued elements).
absl::Status SetPhysicsSystemKinematicSettings(
    ::sdf::World& sdf_world, bool kinematic_enabled,
    std::optional<bool> kinematic_contacts) {
  constexpr absl::string_view kPhysicsSystemName = "gz::sim::systems::Physics";
  constexpr absl::string_view kKinematicElement = "kinematic";
  constexpr absl::string_view kEnabledElement = "enabled";
  constexpr absl::string_view kContactsElement = "contacts";

  ::sdf::ElementPtr world_elem = sdf_world.Element();
  CHECK_NE(world_elem, nullptr);

  ::sdf::ElementPtr physics_plugin_elem;
  if (world_elem->HasElement("plugin")) {
    for (::sdf::ElementPtr plugin_elem = world_elem->GetElement("plugin");
         plugin_elem != nullptr;
         plugin_elem = plugin_elem->GetNextElement("plugin")) {
      if (plugin_elem->Get<std::string>("name") == kPhysicsSystemName) {
        physics_plugin_elem = plugin_elem;
        break;
      }
    }
  }
  if (physics_plugin_elem == nullptr) {
    return NotFoundErrorBuilder()
           << "Unable to set kinematic mode: sdf world has no '"
           << kPhysicsSystemName
           << "' plugin. Make sure a world template with a Physics plugin is "
              "provided.";
  }

  const std::string enabled_str = kinematic_enabled ? "true" : "false";
  LOG(INFO) << "Overriding " << kKinematicElement << " of "
            << kPhysicsSystemName
            << " plugin in sdf world (enabled=" << enabled_str << ", contacts="
            << (kinematic_contacts.has_value()
                    ? (*kinematic_contacts ? "true" : "false")
                    : "default")
            << ").";

  if (::sdf::ElementPtr existing =
          physics_plugin_elem->FindElement(std::string(kKinematicElement));
      existing != nullptr) {
    physics_plugin_elem->RemoveChild(existing);
  }

  auto make_bool_elem = [](const ::sdf::ElementPtr& parent,
                           absl::string_view name, bool val) {
    auto elem = std::make_shared<::sdf::Element>();
    elem->SetParent(parent);
    elem->SetName(std::string(name));
    elem->AddValue("string", val ? "true" : "false", /*_required=*/true);
    parent->InsertElement(elem);
    return elem;
  };

  auto kinematic_elem = std::make_shared<::sdf::Element>();
  kinematic_elem->SetParent(physics_plugin_elem);
  kinematic_elem->SetName(std::string(kKinematicElement));
  physics_plugin_elem->InsertElement(kinematic_elem);

  make_bool_elem(kinematic_elem, kEnabledElement, kinematic_enabled);

  if (kinematic_contacts.has_value()) {
    auto contacts_elem = std::make_shared<::sdf::Element>();
    contacts_elem->SetParent(kinematic_elem);
    contacts_elem->SetName(std::string(kContactsElement));
    kinematic_elem->InsertElement(contacts_elem);
    make_bool_elem(contacts_elem, kEnabledElement, *kinematic_contacts);
  }
  return absl::OkStatus();
}

// Returns `Alias` if set.
// If alias is not set, this returns the `LocalName` prepended by the entity id
// to ensure uniqueness.
// If alias is not a valid SDF name, will attempt to
// correct it by prepending entity id. Returns an error if correction fails.
// If neither alias nor local name is set, will create a name of the form
// "world_model_<entity_id>".
absl::StatusOr<std::string> ModelNameForCollection(
    const World& world, const CollectionsEntityId entity_id) {
  INTR_ASSIGN_OR_RETURN(const auto* entity, world.GetEntityById(entity_id));
  std::string alias = entity->GetAlias();
  if (alias.empty()) {
    return GetEntityName(world, entity_id, /*type_string=*/"model",
                         /*prepend_entity_id=*/true);
  }

  if (sdf::IsReservedSDFName(alias)) {
    LOG(WARNING) << "Alias '" << alias
                 << "' is reserved in sdformat, prepending entity id "
                 << entity_id << " to create a valid name.";
    return FixSDFNameForReservedCharacters(
        absl::StrCat(entity_id.value(), kSdfNamePartsSeparator, alias));
  }

  return FixSDFNameForReservedCharacters(alias);
}

absl::Status CheckUniqueSensorTopicNames(
    const std::map<std::string, std::vector<GzTopicInfo>>& object_topics) {
  absl::flat_hash_map<std::string_view,
                      std::pair<std::string_view, std::string_view>>
      topic_to_object_and_sensor;
  for (const auto& [object_name, topic_infos] : object_topics) {
    for (const auto& topic_info : topic_infos) {
      if (topic_info.sensor_info.has_value() &&
          !topic_info.sensor_info->is_trigger_topic) {
        auto [it, inserted] = topic_to_object_and_sensor.try_emplace(
            topic_info.topic_name,
            std::make_pair(object_name, topic_info.sensor_info->name));
        if (!inserted) {
          return absl::FailedPreconditionError(absl::StrCat(
              "Cannot generate unique sensor topic names since multiple "
              "sensors "
              "within the same model have the same name/topic. Tried "
              "generating "
              "topic '",
              topic_info.topic_name, "' for sensors: ", it->second.first,
              "::", it->second.second, ", ", object_name,
              "::", topic_info.sensor_info->name));
        }
      }
    }
  }
  return absl::OkStatus();
}

class WorldToSdfConverter {
 public:
  explicit WorldToSdfConverter(const WorldSdfAdapter::Options& options)
      : options_(options) {}

  absl::StatusOr<std::string> Convert(const World& world_orig) {
    // Converting World entities into an SDF string. A World generated from
    // "SDF to World v2" is required for this to work.
    //
    // - A <model> named kWorldToSdfTopLevelModelName is added as the sole
    // model directly under <world>, it allows us to add cross-model fixed
    // joints as needed.
    // - Each collection, found by enumerating entities with
    // CollectionsComponent, is converted into a <model> directly under
    // kWorldToSdfTopLevelModelName model.
    // - Attachments between collections are represented by added cross-model
    // fixed joints.
    // - For attachments between collections and the root entity, we assume
    // robot bases are always fixed and "to world" fixed joints are added for
    // robots. For other collections directly under root we do not add fixed
    // joints. Whether their physics parameters specify IsStatic will determine
    // whether they are static under simulation.

    std::shared_ptr<opentelemetry::trace::Span> convert_span =
        stats::StartSampledRootSpan("WorldToSdfConverter/Convert");

    constexpr std::string_view kOutputSdfVersion = "1.6";

    // Clone a world so we can call non-const member functions.
    World world = world_orig.Clone();
    const std::string world_name = "default";
    entity_id_to_scoped_name_.left.insert(
        std::make_pair(kRootEntityId, "world"));

    GenNode root;
    root.prelude =
        absl::StrCat("<?xml version='1.0'?><sdf version='", kOutputSdfVersion,
                     "'><world name='", world_name, "'>");

    if (options_.world_template.has_value()) {
      if (options_.world_template->SdfVersion() != kOutputSdfVersion) {
        return InvalidArgumentErrorBuilder()
               << "World template should use sdf version " << kOutputSdfVersion
               << ", got " << options_.world_template->SdfVersion() << ".";
      }
      absl::StrAppend(&root.prelude, options_.world_template->WorldXml());
    }

    root.postlude = "</world></sdf>";

    // Make sure to set the collision masks first since we need them when
    // serializing links to SDF
    INTR_ASSIGN_OR_RETURN(collision_masks_, ProcessCollisionExclusions(world));

    absl::flat_hash_map<EntityId, absl::flat_hash_map<std::string, double>>
        robot_collection_id_to_dof_value_map;
    // Iterate over all robots, and all joints for each robot, and reset DoF
    // values to zero if requested.
    // We save the previous DoF values to set the initial position for simulated
    // ICON joints.
    for (auto robot_id : world.GetTypedEntityIds<RobotCollectionsEntityId>()) {
      absl::flat_hash_map<std::string, double> dof_values;
      INTR_ASSIGN_OR_RETURN(auto dof_ids, world.GetRobotDofs(robot_id));
      for (const auto& dof_id : dof_ids) {
        INTR_ASSIGN_OR_RETURN(
            auto* kinematics,
            world.GetComponentByEntityId<KinematicsComponent>(dof_id));
        double current_joint_pos = kinematics->GetRawValue();

        if (options_.reset_models_to_zero_dof_values) {
          // If we're resetting to zero before exporting, we want ICON joints to
          // have the current joint positions as their initial position.
          dof_values[world.GetLocalNameForEntityById(dof_id)] =
              current_joint_pos;
          INTR_RETURN_IF_ERROR(world.SetDofRawValue(dof_id, 0,
                                                    /*enforce_limits=*/false));
        } else {
          // If we *do not* reset to zero before exporting, then Gazebo will
          // treat the current position as the new zero position for this joint.
          // That means we must update the limits so that they, too, get
          // shifted.
          const auto& [system_lower, system_upper] =
              kinematics->GetSystemRawValueFixedLimits();
          const auto& [app_lower, app_upper] =
              kinematics->GetApplicationRawValueFixedLimits();

          // We skip enforcing limits for the application limits because the
          // current system limits might not allow this update, however we
          // enforce the limits when applying the system limits because that
          // will ensure that the two are consistent with each other.
          INTR_RETURN_IF_ERROR(kinematics->SetApplicationRawValueFixedLimits(
              app_lower - current_joint_pos, app_upper - current_joint_pos,
              /*enforce_limits=*/false));
          INTR_RETURN_IF_ERROR(kinematics->SetSystemRawValueFixedLimits(
              system_lower - current_joint_pos,
              system_upper - current_joint_pos, /*enforce_limits=*/true));

          // It also means we want ICON joints to have zero as their initial
          // position.
          dof_values[world.GetLocalNameForEntityById(dof_id)] = 0;
        }
      }
      robot_collection_id_to_dof_value_map[robot_id] = std::move(dof_values);
    }

    auto unassociated_ids =
        world.GetTypedEntityIds<AttachmentComponentType, GeometryComponentType,
                                PhysicsComponentType>();
    unassociated_ids.erase(
        std::remove_if(
            unassociated_ids.begin(), unassociated_ids.end(),
            [&world](auto entity_id) {
              ASSIGN_OR_DIE(auto* entity, world.GetEntityById(entity_id));
              return entity
                  ->template HasComponent<CollectionsMemberComponent>();
            }),
        unassociated_ids.end());
    std::sort(unassociated_ids.begin(), unassociated_ids.end());
    for (const auto& entity_id : unassociated_ids) {
      auto model_node = std::make_unique<GenNode>();
      const std::string model_name =
          absl::StrCat(entity_id.value(), kSdfNamePartsSeparator, "entity");
      model_node->start_tag = absl::StrCat("<model name='", model_name, "'>");
      model_node->end_tag = "</model>";
      // TODO(stoyang): Do we need to have <static>1</static> here too?
      // absl::StrAppend(&model_node->prelude, "<static>1</static>");
      INTR_ASSIGN_OR_RETURN(
          GeneratedLinkInfo link_info,
          WorldEntityToLink(convert_span, world, entity_id,
                            /*pose_reference_id=*/kRootEntityId, model_name,
                            options_, /*collision_mask=*/{}));
      if (!link_info.topic_infos.empty()) {
        object_topics_[model_name] = std::move(link_info.topic_infos);
      }
      absl::StrAppend(&model_node->prelude, link_info.sdf_string);
      const std::string scoped_link_name = CreateScopedName(
          model_name, link_info.name, options_.name_scope_separator);
      for (const auto& sensor_entity_id : link_info.sensor_ids) {
        INTR_ASSIGN_OR_RETURN(const auto* sensor_entity,
                              world.GetEntityById(sensor_entity_id));
        entity_id_to_scoped_name_.left.insert(std::make_pair(
            sensor_entity_id,
            absl::StrCat(scoped_link_name, options_.name_scope_separator,
                         sensor_entity->GetLocalName())));
      }

      // Add the model info for the unassociated ID. These kinds of objects are
      // legacy objects in the real world, since each object that has geometric
      // information should belong to a Collection to be object world
      // compatible.
      auto config_plugin_node = std::make_unique<GenNode>();
      config_plugin_node->start_tag =
          "<plugin "
          "filename=\"static://intrinsic::simulation::WorldModelConfigPlugin\" "
          "name=\"intrinsic::simulation::WorldModelConfigPlugin\">";
      config_plugin_node->prelude =
          absl::StrCat("<", kWorldModelConfigPlugin_LinkDataTag, " name='",
                       link_info.name, "' ", kWorldModelConfigPlugin_WorldIdTag,
                       "='", entity_id.value(), "'/>");
      config_plugin_node->end_tag = "</plugin>";
      model_node->AddChild(std::move(config_plugin_node));

      root.AddChild(std::move(model_node));

      entity_id_to_scoped_name_.left.insert(
          std::make_pair(entity_id, scoped_link_name));
    }

    // Convert each collection as a top-level <model> in the generated SDF.
    absl::flat_hash_map<CollectionsEntityId, ModelNodeInfo>
        coll_id_to_model_node_info;
    std::vector<CollectionsEntityId> coll_ids;
    absl::StatusOr<std::unique_ptr<const object_world::ObjectWorld>>
        object_world = object_world::ObjectWorld::CreateView(world);
    for (CollectionsEntityId coll_id :
         world.GetTypedEntityIds<CollectionsEntityId>()) {
      auto base_id_or = world.GetRootEntity(coll_id);
      if (!base_id_or.ok()) {
        if (!absl::IsNotFound(base_id_or.status())) {
          return base_id_or.status();
        }

        auto members = world.GetCollectionMembers(coll_id);
        if (!members.ok()) {
          return absl::InvalidArgumentError("Could not get collection members");
        }

        if (!members->empty()) {
          return absl::InvalidArgumentError(
              "Collection has no base entity but has members");
        } else {
          LOG(WARNING) << "Skipping empty collection " << coll_id;
        }

        continue;
      }

      // Skip collections without links since SDFormat disallows models without
      // links.
      INTR_ASSIGN_OR_RETURN(const auto link_ids,
                            world.ValidateCollectionMembers<LinkEntityId>(
                                coll_id, CollectionsComponent::kLinks));
      if (link_ids.empty()) {
        continue;
      }

      coll_id_to_base_id_[coll_id] = base_id_or.value();
      const object_world::ObjectWorld* object_world_ptr =
          object_world.ok() ? object_world->get() : nullptr;
      INTR_ASSIGN_OR_RETURN(
          auto model_node_info,
          CollectionToModel(convert_span, world, object_world_ptr, coll_id,
                            robot_collection_id_to_dof_value_map,
                            kRootEntityId));
      coll_id_to_model_node_info[coll_id] = std::move(model_node_info);
      coll_ids.push_back(coll_id);
    }

    // Create post-order traversal of the attachment graph, and then a hash map
    // of where each entity exists in the traversal. Use this to sort coll_ids
    // such that no parent's base link appears before a child's base link, that
    // way we can move the unique_ptrs out of coll_id_to_model_node_info when
    // traversing the tree.
    std::vector<AttachmentEntityId> postorder_attachment_ids;
    std::function<void(AttachmentEntityId)> traverse_postorder =
        [&](const AttachmentEntityId root) {
          // TODO(b/184373139) GetChildrenOf traverses entire entity list in
          // world, and is likely overkill for what we're doing here.
          std::vector<AttachmentEntityId> children = world.GetChildrenOf(root);
          std::sort(children.begin(), children.end());
          for (AttachmentEntityId child : children) {
            traverse_postorder(child);
          }
          postorder_attachment_ids.push_back(root);
        };
    traverse_postorder(kRootEntityId);
    absl::flat_hash_map<AttachmentEntityId, int> postorder_attachment_position;
    for (int i = 0; i < postorder_attachment_ids.size(); ++i) {
      postorder_attachment_position[postorder_attachment_ids[i]] = i;
    }

    std::sort(
        coll_ids.begin(), coll_ids.end(),
        [this, &postorder_attachment_position](
            const CollectionsEntityId& id_a, const CollectionsEntityId& id_b) {
          return postorder_attachment_position[coll_id_to_base_id_[id_a]] <
                 postorder_attachment_position[coll_id_to_base_id_[id_b]];
        });

    // Build the model tree and add cross-model fixed joints to realize
    // attachments.
    for (const auto& coll_id : coll_ids) {
      INTR_ASSIGN_OR_RETURN(auto* entity, world.GetEntityById(coll_id));
      INTR_ASSIGN_OR_RETURN(std::string model_name, GetBaseNameForId(coll_id));

      std::unique_ptr<GenNode> model_node =
          std::move(coll_id_to_model_node_info[coll_id].model_node);
      CHECK(coll_id_to_base_id_.contains(coll_id));
      AttachmentEntityId base_id = coll_id_to_base_id_[coll_id];
      INTR_ASSIGN_OR_RETURN(std::string base_name, GetBaseNameForId(base_id));
      INTR_ASSIGN_OR_RETURN(
          auto* attachment,
          world.GetComponentByEntityId<AttachmentComponent>(base_id));
      AttachmentEntityId parent_id = attachment->GetParentId();
      bool fixed_in_root = attachment->IsFixedInRoot();

      // Where do we put this model in the generated SDF? Its base link will
      // likely need to be attached via a fixed joint to some other link in
      // some other model. That parent model is where it should rest, but the
      // fixed joint may have been generated outside of any collections
      // component (such as when two collections are joined). For this reason,
      // if the parent_id belongs to a joint, instead bump it up to a link.
      while (parent_id != kRootEntityId &&
             !world
                  .ValidateCollectionParentAmongTypes(
                      parent_id, {CollectionsComponent::kLinks})
                  .ok()) {
        if (!world.ValidateEntity<CollectionsMemberComponentType>(parent_id)
                 .ok() &&
            world
                .ValidateEntity<GeometryComponentType, PhysicsComponentType>(
                    parent_id)
                .ok()) {
          LOG(DFATAL) << "Link with id " << base_id << " is child of "
                      << "unassociated entity: " << parent_id;
          break;
        }

        INTR_ASSIGN_OR_RETURN(
            attachment,
            world.GetComponentByEntityId<AttachmentComponent>(parent_id));
        parent_id = attachment->GetParentId();
      }

      if (parent_id == kRootEntityId) {
        // Add a fixed joint between entity base link and world if the entity is
        // a robot collection with controlled joints. Joints can be controlled
        // either by ICON devices or by OPC UA generic actions.
        bool is_robot = false;
        bool is_controlled_robot = false;
        if (entity->HasComponent<RobotComponent>()) {
          is_robot = true;
          INTR_ASSIGN_OR_RETURN(const auto* robot,
                                entity->GetComponent<RobotComponent>());
          INTR_ASSIGN_OR_RETURN(const auto generic_action_plugin_spec,
                                robot->GetGenericActionPluginSpec());
          is_controlled_robot =
              generic_action_plugin_spec.has_value() ||
              options_.hardware_module_objects.contains(model_name);
        }

        // TODO(b/231642607) Don't use fixed_in_root
        if (is_controlled_robot || fixed_in_root ||
            coll_id_to_model_node_info[coll_id].is_static) {
          // TODO(stoyang): Do we need <static>1</static> here?
          std::string joint_name = absl::StrCat(model_name, "_world_joint");
          absl::StrAppend(&model_node->prelude, "<joint name='", joint_name,
                          "' type='fixed'><parent>world</parent><child>",
                          base_name, "</child></joint>");
        } else if (!is_robot) {
          // Add ObjectInteractionModerator plugin for floating non-robot
          // object.
          if (options_.enable_object_interaction_moderator) {
            absl::StrAppend(
                &model_node->prelude,
                "<plugin "
                "filename=\"static://"
                "intrinsic::simulation::ObjectInteractionModerator\" "
                "name=\"intrinsic::simulation::ObjectInteractionModerator\" "
                "/>");
          }
        }
        // Not attached to any other <model>; add under root.
        root.AddChild(std::move(model_node));
      } else {
        if (!link_id_to_coll_id_.contains(parent_id)) {
          LOG(DFATAL) << "Link entity " << parent_id << " does not belong to a "
                      << "collection.";
          continue;
        }

        CollectionsEntityId parent_coll_id = link_id_to_coll_id_[parent_id];
        if (parent_coll_id == coll_id) {
          return intrinsic::InternalErrorBuilder().LogError()
                 << "Model '" << model_name << "' is parented to itself!";
        }

        INTR_ASSIGN_OR_RETURN(
            std::string parent_model_name, GetBaseNameForId(parent_coll_id),
            _ << "Cannot find corresponding <model> for entity "
              << parent_coll_id);

        // TODO(b/290236086) The semantics for having a static child model under
        // a moving parent are unclear in our World representation, so we ignore
        // them. If the parent is also marked as static, then we will add a
        // world joint there.
        if (coll_id_to_model_node_info[coll_id].is_static) {
          LOG(WARNING) << "Ignoring static flag for model [" << model_name
                       << "] nested under [" << parent_model_name;
        }

        if (!coll_id_to_model_node_info.contains(parent_coll_id)) {
          return intrinsic::InternalErrorBuilder().LogError()
                 << "Cannot find GenNode for converted model '"
                 << parent_model_name << "' while processing child model '"
                 << model_name << "'.";
        }
        std::unique_ptr<GenNode>& parent_node =
            coll_id_to_model_node_info[parent_coll_id].model_node;
        if (parent_node == nullptr) {
          // This should never happen, as we sort the coll_ids in a post-order.
          // traversal.
          return intrinsic::InternalErrorBuilder().LogError()
                 << "GenNode for model '" << parent_model_name
                 << "' was processed before GenNode for child model '"
                 << model_name << "'!";
        }

        INTR_ASSIGN_OR_RETURN(std::string parent_name,
                              GetBaseNameForId(parent_id));
        std::string child_link_name =
            absl::StrCat(model_name, kSdfNameSeparator, base_name);
        std::string joint_name =
            absl::StrCat(model_name, "_", parent_model_name, "_joint");

        parent_node->postlude =
            absl::StrCat("<joint name='", joint_name, "' type='fixed'><parent>",
                         parent_name, "</parent><child>", child_link_name,
                         "</child></joint>", parent_node->postlude);
        // Add under parent node.
        model_node->pose_elem = Pose3ToPoseString(
            world.GetTransform(coll_id_to_base_id_[parent_coll_id], base_id));
        parent_node->AddChild(std::move(model_node));
        INTR_RETURN_IF_ERROR(ReparentModelNames(coll_id, parent_coll_id));
      }
    }

    // Compile the sdf string.
    auto res = root.Flatten();

    // Round-trip the SDF string through the SDF library. This allows us to
    // check for any errors in the generated SDF, and it also formats the SDF
    // string nicely.
    auto parser_config = ::sdf::ParserConfig::GlobalConfig();
    parser_config.SetCalculateInertialConfiguration(
        ::sdf::ConfigureResolveAutoInertials::SKIP_CALCULATION_IN_LOAD);
    ::sdf::Root sdf_root;
    ::sdf::Errors errors = sdf_root.LoadSdfString(res, parser_config);
    if (!errors.empty()) {
      return absl::InternalError(absl::StrCat(
          "Encountered ", errors.size(),
          " errors parsing SDF string for pretty-printing:\n",
          absl::StrJoin(
              errors, "\n", [](std::string* os, const ::sdf::Error& error) {
                if (error.XmlPath().has_value()) {
                  absl::StrAppend(os, *error.XmlPath());
                }
                if (error.LineNumber().has_value()) {
                  absl::StrAppend(os, "(l. ", *error.LineNumber(), ")");
                }
                absl::StrAppend(os, ": ", error.Message());
              })));
    }

    // Override sim step size if specified in world_template_overrides.
    std::optional<double> sim_step_size_override;
    if (options_.world_template_overrides.physics_step_size.has_value()) {
      if (*options_.world_template_overrides.physics_step_size <=
          absl::ZeroDuration()) {
        return absl::InvalidArgumentError(absl::StrCat(
            "physics_step_size in world_template_overrides must be strictly "
            "positive, got ",
            absl::FormatDuration(
                *options_.world_template_overrides.physics_step_size)));
      }
      sim_step_size_override = absl::ToDoubleSeconds(
          *options_.world_template_overrides.physics_step_size);
    }

    INTR_ASSIGN_OR_RETURN(
        double sim_step_size_sec,
        GetPhysicsMaxStepSizeWithOverride(*sdf_root.WorldByIndex(0),
                                          sim_step_size_override));
    sim_step_size_ = absl::Seconds(sim_step_size_sec);

    // Configure the kinematic settings of the Physics system according to the
    // physics mode, if specified in world_template_overrides.
    if (options_.world_template_overrides.physics_mode.has_value()) {
      using PhysicsMode = WorldSdfAdapter::WorldTemplateOverrides::PhysicsMode;
      switch (*options_.world_template_overrides.physics_mode) {
        case PhysicsMode::kUnspecified:
        case PhysicsMode::kDynamic:
          INTR_RETURN_IF_ERROR(SetPhysicsSystemKinematicSettings(
              *sdf_root.WorldByIndex(0), /*kinematic_enabled=*/false,
              /*kinematic_contacts=*/std::nullopt));
          break;
        case PhysicsMode::kKinematic: {
          INTR_ASSIGN_OR_RETURN(const std::vector<std::string> ft_sensor_names,
                                GetForceTorqueSensorNames(world));
          if (!ft_sensor_names.empty()) {
            LOG(WARNING)
                << "Kinematic physics mode is enabled, but the world contains "
                   "force-torque sensor(s) ["
                << absl::StrJoin(ft_sensor_names, ", ")
                << "]. Because all links are kinematic and do not respond to "
                   "contact forces, force-torque sensor readings will not be "
                   "valid.";
          }
          // Contacts between kinematic links and static or other kinematic
          // links have no effect on the simulation and only cost collision
          // detection time, so they are disabled.
          INTR_RETURN_IF_ERROR(SetPhysicsSystemKinematicSettings(
              *sdf_root.WorldByIndex(0), /*kinematic_enabled=*/true,
              /*kinematic_contacts=*/false));
          break;
        }
      }
    }

    auto pretty_sdf_string = sdf_root.Element()->ToString("", false, false);
    convert_span->End();
    return pretty_sdf_string;
  }

  // Public member variables that are populated in `Convert` and visible to
  // `WorldSdfAdapter`.
  EntityIdToScopedNameBimap entity_id_to_scoped_name_;
  absl::Duration sim_step_size_;
  absl::flat_hash_map<std::string, std::vector<GzTopicInfo>> object_topics_;

 private:
  struct GeneratedJointInfo {
    std::string name;
    std::string sdf_string;
    std::vector<GzTopicInfo> topic_infos;
  };

  struct ModelNodeInfo {
    std::unique_ptr<GenNode> model_node;
    bool is_static;
  };

  absl::StatusOr<ModelNodeInfo> CollectionToModel(
      const std::shared_ptr<opentelemetry::trace::Span>& parent_span,
      const World& world, const object_world::ObjectWorld* object_world,
      CollectionsEntityId coll_id,
      const absl::flat_hash_map<EntityId,
                                absl::flat_hash_map<std::string, double>>&
          robot_collection_id_to_dof_value_map,
      AttachmentEntityId parent_id) {
    stats::ScopedSpan collection_to_model_span(
        absl::StrCat("WorldToSdfConverter/CollectionToModel/", coll_id.value()),
        parent_span);
    auto model_node = std::make_unique<GenNode>();
    INTR_ASSIGN_OR_RETURN(auto* entity, world.GetEntityById(coll_id));
    INTR_ASSIGN_OR_RETURN(std::string model_name,
                          ModelNameForCollection(world, coll_id));
    model_node->start_tag = absl::StrCat("<model name='", model_name, "'>");
    entity_id_to_scoped_name_.left.insert(std::make_pair(coll_id, model_name));
    model_node->end_tag = "</model>";

    CHECK(coll_id_to_base_id_.contains(coll_id));
    AttachmentEntityId base_id = coll_id_to_base_id_[coll_id];

    INTR_ASSIGN_OR_RETURN(auto link_ids,
                          world.ValidateCollectionMembers<LinkEntityId>(
                              coll_id, CollectionsComponent::kLinks));

    // Add pose from parent.
    model_node->pose_elem =
        Pose3ToPoseString(world.GetTransform(parent_id, base_id));

    std::vector<GzTopicInfo> model_topics_list;

    // Convert links.
    absl::flat_hash_map<AttachmentEntityId, AttachmentEntityId>
        link_parent_to_link;
    // Use std::map for stable iteration order.
    std::map<AttachmentEntityId, std::string> link_to_local_link_name;
    for (const auto& link_id : link_ids) {
      INTR_ASSIGN_OR_RETURN(
          GeneratedLinkInfo link_info,
          WorldEntityToLink(collection_to_model_span.span(), world, link_id,
                            /*pose_reference_id=*/base_id, model_name, options_,
                            collision_masks_[link_id]));
      model_topics_list.reserve(model_topics_list.size() +
                                link_info.topic_infos.size());
      std::ranges::move(link_info.topic_infos,
                        std::back_inserter(model_topics_list));

      link_to_local_link_name[link_id] = link_info.name;

      const std::string scoped_link_name = CreateScopedName(
          model_name, link_info.name, options_.name_scope_separator);
      entity_id_to_scoped_name_.left.insert(
          std::make_pair(link_id, scoped_link_name));

      for (const auto& sensor_entity_id : link_info.sensor_ids) {
        INTR_ASSIGN_OR_RETURN(const auto* sensor_entity,
                              world.GetEntityById(sensor_entity_id));
        entity_id_to_scoped_name_.left.insert(std::make_pair(
            sensor_entity_id,
            absl::StrCat(scoped_link_name, options_.name_scope_separator,
                         sensor_entity->GetLocalName())));
      }

      link_id_to_coll_id_[link_id] = coll_id;
      INTR_ASSIGN_OR_RETURN(const auto* link_entity,
                            world.GetEntityById(link_id));
      INTR_ASSIGN_OR_RETURN(const auto* attachment,
                            link_entity->GetComponent<AttachmentComponent>());
      link_parent_to_link[attachment->GetParentId()] = link_id;
      absl::StrAppend(&model_node->prelude, link_info.sdf_string);
    }
    // TODO(b/290236086) Use <static> again once we have alignment in
    // propagation of the flag to children between world and gazebo.
    bool model_is_static = false;
    if (entity->HasComponent<SimulationComponent>()) {
      INTR_ASSIGN_OR_RETURN(const SimulationComponent* simulation,
                            entity->GetComponent<SimulationComponent>());
      model_is_static = simulation->IsStatic();
    }

    // Get custom joint sdf data from user data component.
    // This a mapping of joint name to <joint> SDF strings for custom joints.
    // The map is used when composing the SDF for a joint entity of a custom
    // joint type.
    auto custom_joint_sdf = GetCustomJointSdfData(*entity);

    // Convert joints.
    INTR_ASSIGN_OR_RETURN(auto joint_ids,
                          world.ValidateCollectionMembers<JointEntityId>(
                              coll_id, CollectionsComponent::kJoints));
    for (const auto& joint_id : joint_ids) {
      INTR_ASSIGN_OR_RETURN(GeneratedJointInfo joint_info,
                            GetJointXml(world, link_parent_to_link, joint_id,
                                        custom_joint_sdf, model_name));

      const std::string scoped_joint_name = CreateScopedName(
          model_name, joint_info.name, options_.name_scope_separator);
      entity_id_to_scoped_name_.left.insert(
          std::make_pair(joint_id, scoped_joint_name));

      model_topics_list.reserve(model_topics_list.size() +
                                joint_info.topic_infos.size());
      std::ranges::move(joint_info.topic_infos,
                        std::back_inserter(model_topics_list));

      absl::StrAppend(&model_node->prelude, joint_info.sdf_string);
    }
    // Create fixed joints among links when there is no explicit joints.
    if (link_ids.size() > 1 && joint_ids.empty()) {
      for (const auto& link_id : link_ids) {
        INTR_ASSIGN_OR_RETURN(const auto* link_entity,
                              world.GetEntityById(link_id));
        INTR_ASSIGN_OR_RETURN(const auto* attachment,
                              link_entity->GetComponent<AttachmentComponent>());
        auto parent_id = attachment->GetParentId();
        if (link_to_local_link_name.contains(parent_id)) {
          INTR_ASSIGN_OR_RETURN(std::string parent_link_name,
                                GetBaseNameForId(parent_id));
          INTR_ASSIGN_OR_RETURN(std::string child_link_name,
                                GetBaseNameForId(link_id));
          std::string joint_name =
              absl::StrCat(child_link_name, "_", parent_link_name, "_joint");
          // This string replacement is needed to make sure that sdformat 11
          // would not try to infer nested model structures that do not exist.
          joint_name = absl::StrReplaceAll(
              joint_name, {{kSdfNameSeparator, kSdfNamePartsSeparator}});
          absl::StrAppend(&model_node->prelude, "<joint name='", joint_name,
                          "' type='fixed'><parent>", parent_link_name,
                          "</parent><child>", child_link_name,
                          "</child></joint>");
        }
      }
    }
    // Output support plugins.
    absl::flat_hash_map<std::string, double> dof_local_name_to_value;
    if (robot_collection_id_to_dof_value_map.contains(coll_id)) {
      dof_local_name_to_value =
          robot_collection_id_to_dof_value_map.at(coll_id);
    }

    INTR_ASSIGN_OR_RETURN(SimulatorPluginsResult sim_plugins_result,
                          GetSimulatorPlugins(entity, model_name, options_,
                                              world, dof_local_name_to_value));
    std::string sim_plugins = sim_plugins_result.plugins_sdf;
    model_topics_list.reserve(model_topics_list.size() +
                              sim_plugins_result.topic_infos.size());
    std::ranges::move(sim_plugins_result.topic_infos,
                      std::back_inserter(model_topics_list));

    // Add the model config data for this model.
    absl::StrAppend(
        &sim_plugins,
        "<plugin "
        "filename=\"static://intrinsic::simulation::WorldModelConfigPlugin\" "
        "name=\"intrinsic::simulation::WorldModelConfigPlugin\">");
    if (object_world != nullptr) {
      absl::StrAppend(&sim_plugins,
                      GenerateModelDataTag(coll_id, object_world));
    }
    for (const auto& [link_id, link_name] : link_to_local_link_name) {
      absl::StrAppend(&sim_plugins, "<", kWorldModelConfigPlugin_LinkDataTag,
                      " name='", link_name, "' ",
                      kWorldModelConfigPlugin_WorldIdTag, "='", link_id.value(),
                      "'/>");
    }
    absl::StrAppend(&sim_plugins, "</plugin>");

    if (!model_topics_list.empty()) {
      object_topics_[model_name] = std::move(model_topics_list);
    }

    absl::StrAppend(&model_node->prelude, sim_plugins);
    return ModelNodeInfo{.model_node = std::move(model_node),
                         .is_static = model_is_static};
  }

  // Parses the the custom joint sdf string, and overrides its name, parent and
  // child names with values from the World. This function is called for models
  // containing custom joint types, i.e. Has user data with the
  // kGazeboCustomJoint. For now, the only custom joint type supported is ball
  // joint.
  absl::StatusOr<std::pair<std::string, std::string>> OverrideJointSdf(
      std::string_view joint_sdf_string, JointParams override_params) {
    // Override joint sdf's joint name, parent and child names with the
    // values from the World.
    std::string versioned_sdf = "<sdf version='" + std::string(SDF_VERSION) +
                                "'>" + std::string(joint_sdf_string) + "</sdf>";
    ::sdf::ElementPtr joint_elem = std::make_shared<::sdf::Element>();
    // Initialize joint sdf element to use the joint.sdf schema
    // Note this has performance benefit over loading the entire root.sdf schema
    // or calling sdf::init
    ::sdf::initFile("joint.sdf", joint_elem);
    if (!::sdf::readString(versioned_sdf, joint_elem)) {
      return intrinsic::InternalErrorBuilder().LogError()
             << "Failed to load custom joint: " << override_params.joint_name
             << ". Joint sdf string: " << joint_sdf_string;
    }
    if (joint_elem->FindElement("sensor") != nullptr) {
      // TODO(b/498350265) Handle <sensor> SDF elements nested in the custom
      // <joint> sdf
      return intrinsic::InternalErrorBuilder().LogError()
             << "Unable to override joint SDF with a custom joint SDF string "
             << "that contains <sensor> elements. Joint: "
             << override_params.joint_name
             << ". Joint SDF string: " << joint_sdf_string;
    }
    joint_elem->GetAttribute("name")->Set(override_params.joint_name);
    joint_elem->GetElement("parent")->Set(override_params.parent_name);
    joint_elem->GetElement("child")->Set(override_params.child_name);
    return std::make_pair(override_params.joint_name, joint_elem->ToString(""));
  }

  absl::StatusOr<GeneratedJointInfo> GetJointXml(
      const World& world,
      const absl::flat_hash_map<AttachmentEntityId, AttachmentEntityId>&
          link_parent_to_link,
      JointEntityId joint_id,
      const absl::flat_hash_map<std::string, std::string>& custom_joint_sdf,
      std::string_view parent_model_name) {
    INTR_ASSIGN_OR_RETURN(const auto* joint_entity,
                          world.GetEntityById(joint_id));
    INTR_ASSIGN_OR_RETURN(const auto* attachment,
                          joint_entity->GetComponent<AttachmentComponent>());
    INTR_ASSIGN_OR_RETURN(
        std::string joint_name,
        GetEntityName(world, joint_id, /*type_string=*/"joint",
                      /*prepend_entity_id=*/false));
    INTR_ASSIGN_OR_RETURN(std::string parent_name,
                          GetBaseNameForId(attachment->GetParentId()),
                          _ << "Cannot find parent link for joint '"
                            << joint_entity->GetLocalName() << "'.");
    auto joint_child_id_itr = link_parent_to_link.find(joint_id);
    if (joint_child_id_itr == link_parent_to_link.end()) {
      return intrinsic::InternalErrorBuilder().LogError()
             << "Cannot find child link id for joint '"
             << joint_entity->GetLocalName() << "'.";
    }
    INTR_ASSIGN_OR_RETURN(std::string child_name,
                          GetBaseNameForId(joint_child_id_itr->second),
                          _ << "Cannot find child link for joint '"
                            << joint_entity->GetLocalName() << "'.");

    if (auto joint_data_iter = custom_joint_sdf.find(joint_name);
        joint_data_iter != custom_joint_sdf.end()) {
      LOG(INFO) << "Using custom joint user data for joint [" << joint_name
                << "]. Axis and pose properties from the world will be "
                << "ignored for this joint.";
      INTR_ASSIGN_OR_RETURN(
          auto override_joint_pair,
          OverrideJointSdf(/*joint_sdf_string=*/joint_data_iter->second,
                           {.joint_name = joint_name,
                            .parent_name = parent_name,
                            .child_name = child_name}));
      return GeneratedJointInfo{
          .name = std::move(override_joint_pair.first),
          .sdf_string = std::move(override_joint_pair.second)};
    }

    INTR_ASSIGN_OR_RETURN(const auto* kinematics,
                          joint_entity->GetComponent<KinematicsComponent>());

    JointProperties properties;
    properties.joint_name = joint_name;
    properties.parent_name = parent_name;
    properties.child_name = child_name;

    // Add an anchor pose from the child's frame to the joint
    INTR_ASSIGN_OR_RETURN(const AttachmentComponent* child_attachment,
                          world.GetComponentByEntityId<AttachmentComponent>(
                              joint_child_id_itr->second));
    auto joint_t_child = child_attachment->GetParentTThis();
    properties.child_t_joint = joint_t_child.inverse();

    std::vector<GzTopicInfo> topic_infos;
    auto sensors_or = GetSensorsInImmediateAttachmentChildren(
        world, joint_id, parent_model_name, options_);
    if (sensors_or.ok()) {
      topic_infos = std::move(sensors_or)->topic_infos;
      for (auto& topic_info : topic_infos) {
        if (!topic_info.joint_info.has_value()) {
          topic_info.joint_info = GzTopicInfo::JointInfo{.name = joint_name};
        }
      }
      for (const auto& sensor_entity_id : sensors_or->sensor_ids) {
        INTR_ASSIGN_OR_RETURN(const auto* sensor_entity,
                              world.GetEntityById(sensor_entity_id));
        entity_id_to_scoped_name_.left.insert(std::make_pair(
            sensor_entity_id, absl::StrJoin({parent_model_name, joint_name,
                                             sensor_entity->GetLocalName()},
                                            options_.name_scope_separator)));
      }
      properties.sensors_sdf = std::move(sensors_or)->sdf_string;
    } else {
      if (options_.skip_failed_sensors) {
        LOG(WARNING)
            << "Something went wrong when trying to find sensors under joint '"
            << joint_entity->GetLocalName() << "': " << sensors_or.status();
      } else {
        return sensors_or.status();
      }
    }

    intrinsic_proto::world::UserDataComponent user_data_proto;
    if (auto user_data = joint_entity->GetComponent<UserDataComponent>();
        user_data.ok()) {
      INTR_ASSIGN_OR_RETURN(user_data_proto, (*user_data)->ToProto());
    }

    INTR_ASSIGN_OR_RETURN(auto kinematics_proto, kinematics->ToProto());
    INTR_ASSIGN_OR_RETURN(auto joint_sdf,
                          JointSpecToString(kinematics_proto, properties,
                                            user_data_proto.user_data_map()));

    return GeneratedJointInfo{.name = std::move(joint_name),
                              .sdf_string = std::move(joint_sdf),
                              .topic_infos = std::move(topic_infos)};
  }

  absl::Status ReparentModelNames(EntityId child_id, EntityId parent_id) {
    // Find the common prefix of the parent and child scoped names.
    auto& id_to_name = entity_id_to_scoped_name_.left;
    const std::string child_scoped_name = id_to_name.at(child_id);
    const std::string parent_scoped_name = id_to_name.at(parent_id);

    const std::string common_prefix = [this](const std::string& x,
                                             const std::string& y) {
      // To distinguish between prefixes that are the same but do not contain a
      // name separator, we split each x and y by the separator and take the
      // first N that match.
      std::vector<std::string> xs =
          absl::StrSplit(x, options_.name_scope_separator);
      std::vector<std::string> ys =
          absl::StrSplit(y, options_.name_scope_separator);
      int i = -1;
      do {
        ++i;
      } while (xs[i] == ys[i]);
      return absl::StrJoin(xs.begin(), xs.begin() + i,
                           options_.name_scope_separator);
    }(child_scoped_name, parent_scoped_name);

    // Make sure that the scoped child name is a direct child of the prefix.
    INTR_ASSIGN_OR_RETURN(const std::string child_base_name,
                          GetBaseNameForId(child_id));

    CHECK_EQ(child_scoped_name.substr(common_prefix.size()),
             !common_prefix.empty()
                 ? absl::StrCat(options_.name_scope_separator, child_base_name)
                 : child_base_name);

    const std::string new_child_name = CreateScopedName(
        parent_scoped_name, child_base_name, options_.name_scope_separator);

    // Anything that starts with the "<child_scoped_name><separator>" will need
    // to be renamed to be the
    // "<parent_scoped_name><separator><child_base_name><separator>".
    const std::string old_child_name_prefix_with_sep =
        absl::StrCat(child_scoped_name, options_.name_scope_separator);
    for (auto itr = id_to_name.begin(); itr != id_to_name.end(); itr++) {
      const std::string& entity_name = itr->second;
      if (entity_name == child_scoped_name) {
        id_to_name.replace_data(itr, new_child_name);
        continue;
      }

      if (!absl::StartsWith(entity_name, old_child_name_prefix_with_sep)) {
        continue;
      }

      id_to_name.replace_data(
          itr, absl::StrCat(
                   new_child_name, options_.name_scope_separator,
                   entity_name.substr(old_child_name_prefix_with_sep.size())));
    }

    return absl::OkStatus();
  }

  absl::StatusOr<std::vector<std::string>> GetForceTorqueSensorNames(
      const World& world) const {
    std::vector<std::string> names;
    for (const SensorEntityId sensor_id :
         world.GetTypedEntityIds<SensorEntityId>()) {
      INTR_ASSIGN_OR_RETURN(const auto* sensor_entity,
                            world.GetEntityById(sensor_id));
      INTR_ASSIGN_OR_RETURN(const auto* sensor,
                            sensor_entity->GetComponent<SensorComponent>());
      if (sensor->GetType() != SensorType::kForceTorque) {
        continue;
      }
      if (auto it = entity_id_to_scoped_name_.left.find(sensor_id);
          it != entity_id_to_scoped_name_.left.end()) {
        names.push_back(it->second);
      } else {
        names.push_back(sensor_entity->GetLocalName());
      }
    }
    std::sort(names.begin(), names.end());
    return names;
  }

  absl::StatusOr<std::string> GetBaseNameForId(EntityId entity_id) {
    const auto& id_to_name = entity_id_to_scoped_name_.left;
    if (id_to_name.find(entity_id) == id_to_name.end()) {
      return intrinsic::NotFoundErrorBuilder()
             << "Could not find scoped name for entity with id: "
             << entity_id.value();
    }

    const std::string scoped_name = id_to_name.at(entity_id);
    auto separator_pos = scoped_name.rfind(options_.name_scope_separator);

    // Names may contain kSdfNameSeparator, so we will keep looking until the
    // parent name corresponds to an existing ID.
    while (separator_pos != std::string::npos) {
      const std::string base_name = scoped_name.substr(
          separator_pos + options_.name_scope_separator.size());
      const std::string parent_name = scoped_name.substr(0, separator_pos);

      const auto& name_to_id = entity_id_to_scoped_name_.right;
      if (name_to_id.find(parent_name) != name_to_id.end()) {
        return base_name;
      }

      separator_pos = parent_name.rfind(options_.name_scope_separator);
    }

    // Didn't find a parent? Name must be the fully scoped name...
    return scoped_name;
  }

  WorldSdfAdapter::Options options_;
  absl::flat_hash_map<AttachmentEntityId, CollectionsEntityId>
      link_id_to_coll_id_;
  absl::flat_hash_map<CollectionsEntityId, AttachmentEntityId>
      coll_id_to_base_id_;
  absl::flat_hash_map<CollisionEntityId, uint32_t> collision_masks_;
};

}  // namespace

absl::StatusOr<std::unique_ptr<WorldSdfAdapter>> WorldSdfAdapter::Create(
    const World& world, const WorldSdfAdapter::Options& options) {
  auto adapter = absl::WrapUnique(new WorldSdfAdapter);
  adapter->name_scope_separator_ = options.name_scope_separator;

  WorldToSdfConverter converter(options);
  INTR_ASSIGN_OR_RETURN(adapter->sdf_, converter.Convert(world));
  adapter->entity_id_to_scoped_name_ = converter.entity_id_to_scoped_name_;
  adapter->sim_step_size_ = converter.sim_step_size_;
  for (auto& [name, list] : converter.object_topics_) {
    if (!list.empty()) {
      adapter->object_topics_.emplace(name, std::move(list));
    }
  }

  if (options.ensure_unique_sensor_topic_names) {
    INTR_RETURN_IF_ERROR(CheckUniqueSensorTopicNames(adapter->object_topics_));
  }

  return adapter;
}

std::optional<std::string> WorldSdfAdapter::GetGazeboNameForEntity(
    EntityId entity_id, absl::string_view separator) const {
  const auto& id_to_name = entity_id_to_scoped_name_.left;
  if (id_to_name.find(entity_id) == id_to_name.end()) {
    return std::nullopt;
  }

  return absl::StrReplaceAll(id_to_name.at(entity_id),
                             {{name_scope_separator_, separator}});
}

std::optional<EntityId> WorldSdfAdapter::GetIntrinsicEntityForScopedName(
    absl::string_view name) const {
  const std::string name_str = std::string(name);
  const auto& name_to_id = entity_id_to_scoped_name_.right;
  if (name_to_id.find(name_str) == name_to_id.end()) {
    return std::nullopt;
  }

  return name_to_id.at(name_str);
}

std::optional<std::string> WorldSdfAdapter::GetSensorTopicNameForEntity(
    EntityId entity_id) const {
  std::optional<std::string> scoped_name =
      GetGazeboNameForEntity(entity_id, name_scope_separator_);
  if (!scoped_name.has_value()) {
    return std::nullopt;
  }

  std::vector<std::string> parts =
      absl::StrSplit(*scoped_name, name_scope_separator_);
  if (parts.empty()) {
    return std::nullopt;
  }

  const std::string& sensor_unscoped_name = parts.back();

  // Move right to left to find the parent object name in object_topics_
  for (int i = static_cast<int>(parts.size()) - 2; i >= 0; --i) {
    std::string object_name = absl::StrJoin(
        parts.begin(), parts.begin() + i + 1, name_scope_separator_);
    auto it = object_topics_.find(object_name);
    if (it != object_topics_.end()) {
      for (const auto& topic_info : it->second) {
        if (topic_info.sensor_info.has_value() &&
            topic_info.sensor_info->name == sensor_unscoped_name &&
            !topic_info.sensor_info->is_trigger_topic) {
          return topic_info.topic_name;
        }
      }
    }
  }

  return std::nullopt;
}

}  // namespace simulation
}  // namespace intrinsic
