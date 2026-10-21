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

#include "intrinsic/simulation/gazebo/server_main_impl.h"

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "grpcpp/security/server_credentials.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "gz/common/Console.hh"
#include "gz/math/Rand.hh"
#include "gz/sim/ServerConfig.hh"
#include "gz/sim/Util.hh"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/assets/interface_utils.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"
#include "intrinsic/hardware/gripper/eoat/eoat_service.pb.h"
#include "intrinsic/hardware/gripper/gripper_equipment.pb.h"
#include "intrinsic/hardware/gripper/service_asset/pinch_gripper_opcua_service_config.pb.h"
#include "intrinsic/hardware/gripper/service_asset/pinch_gripper_realtime_control_service_config.pb.h"
#include "intrinsic/hardware/gripper/service_asset/suction_gripper_opcua_service_config.pb.h"
#include "intrinsic/hardware/gripper/service_asset/suction_gripper_realtime_control_service_config.pb.h"
#include "intrinsic/icon/hal/proto/hardware_module_config.pb.h"
#include "intrinsic/icon/release/file_helpers.h"
#include "intrinsic/icon/release/portable/init_intrinsic.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/perception/proto/v1/camera_config.pb.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/resources/proto/runtime_context.pb.h"
#include "intrinsic/scene/sdf/separators.h"
#include "intrinsic/simulation/gazebo/asset/gazebo_simulator_config.pb.h"
#include "intrinsic/simulation/gazebo/asset_instances_client.h"
#include "intrinsic/simulation/gazebo/gazebo_performance_logger.h"
#include "intrinsic/simulation/gazebo/gazebo_runner.h"
#include "intrinsic/simulation/gazebo/gazebo_service.h"
#include "intrinsic/simulation/gazebo/outfeed/outfeed_manager.h"
#include "intrinsic/simulation/gazebo/plugins/cameras/camera_services.h"
#include "intrinsic/simulation/gazebo/plugins/gpio/gpio_service.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/pinch_gripper_server_impl.h"
#include "intrinsic/simulation/gazebo/plugins/sim_inputs/sim_inputs_service_singleton.h"
#include "intrinsic/simulation/gazebo/simulator_control_service.h"
#include "intrinsic/simulation/gazebo/spawner/spawner_manager.h"
#include "intrinsic/simulation/gazebo/world_sync_system.h"
#include "intrinsic/simulation/gazebo/world_templates/world_template.h"
#include "intrinsic/simulation/gazebo/world_util.h"
#include "intrinsic/simulation/service/proto/v1/simulator_world_sync.grpc.pb.h"
#include "intrinsic/simulation/service/proto/v1/simulator_world_sync.pb.h"
#include "intrinsic/simulation/world/world_object_plugin.h"
#include "intrinsic/simulation/world/world_to_sdf.h"
#include "intrinsic/stats/opencensus.h"
#include "intrinsic/util/grpc/channel.h"
#include "intrinsic/util/grpc/connection_params.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "sdf/ParserConfig.hh"
#include "tools/cpp/runfiles/runfiles.h"

ABSL_FLAG(std::string, sim_control_address, "0.0.0.0:50053",
          "Simulator control server address.");

ABSL_FLAG(std::vector<std::string>, forward_transport_to_dds, {},
          "Comma-separated list of topics to forward from Gazebo's transport "
          "layer to our internal DDS");

ABSL_FLAG(
    std::optional<unsigned int>, seed, std::nullopt,
    "Optional seed for random number generator used in simulated sensors to "
    "add noise.");

ABSL_FLAG(
    bool, start_paused, false,
    "If true, gazebo will be started in a paused state. To start stepping the "
    "simulation, call Unpause().");

ABSL_FLAG(
    bool, ignore_server_create_error_at_startup, false,
    "If true, when creating a SimulatorControlService, if the factory "
    "function for GazeboRunner returns an error, the error is logged and "
    "the service is started in a Stopped state. If set to false (default), the "
    "SimulatorControlService instance will not be created.");

ABSL_FLAG(bool, measure_performance, true,
          "If true, a custom gz::common::Profiler will be installed to measure "
          "server performance at runtime. Note that the server must be built "
          "with --@gz-common//profiler:config=custom for this "
          "functionality (enabled by default in insrc/.bazelrc).");

ABSL_FLAG(bool, use_dart, false,
          "If true, Gazebo will use the Dart physics engine. "
          "By default, it will use Bullet Featherstone physics engine.");

ABSL_FLAG(std::string, data_logger_grpc_service_address, "",
          "(optional) Address of the gRPC service to send logs to, in the form "
          "'host:port'.");

ABSL_FLAG(std::string, simulator_world_id_testonly, "",
          "Sets the simulator world id directly, bypassing the simulation "
          "service. This can be used in tests which don't start the simulation "
          "service but do start the world and geometry services. See "
          "--simulation_service_address for description of the world and "
          "geometry services requirements.");

ABSL_FLAG(std::string, sdf_path_testonly, "",
          "Path to the SDF file containing the world to run. This path will be "
          "ignored if either --simulation_service_address or "
          "--simulator_world_id_testonly is set. If the path is empty, gazebo "
          "will run with an empty world.");

ABSL_FLAG(int, gz_verbosity_level, 3,
          "Gazebo verbosity level, default is 3 (info level). Set to 4 to "
          "enable debug logging.");

ABSL_FLAG(
    std::string, simulation_service_address, "",
    "Address of the simulation service. The simulation server will connect "
    "to this service to initialize a session and obtain the simulator world "
    "id. The world must be available in the World Service before the server is "
    "run, and it must out-live the server. If --runtime_context_file is not "
    "present, the WORLD_SERVICE_ADDRESS and GEOMETRY_SERVICE_ADDRESS env "
    "variables must be set so that Gazebo can fetch the world and associated "
    "geometry data from the services.");

ABSL_FLAG(std::string, sdf_world_template_path,
          "intrinsic/simulation/gazebo/world_templates/default.sdf.tpl",
          "Path to the sdformat world template file. This file will be used to "
          "construct the world sdf string for Gazebo, and is only used if a "
          "simulator world id is resolved (either via "
          "--simulation_service_address or --simulator_world_id_testonly).");

ABSL_FLAG(std::string, runtime_context_file, "/etc/intrinsic/runtime_config.pb",
          "The path to the runtime context file containing "
          "intrinsic_proto.config.RuntimeContext binary proto. Will be set "
          "when `gzserver` is run as an asset service.");

namespace intrinsic {
namespace simulation {

namespace {
const absl::Duration kGeometryServiceConnectTimeout = absl::Seconds(300);
constexpr absl::Duration kGetSimulatorWorldInfoTimeout = absl::Seconds(10);

using ::intrinsic_proto::world::ObjectWorldService;
using simulation::GazeboRunner;

// Use different retries to get world from world service for server init and for
// reset.
constexpr int kNumRetriesToGetWorldAtInit = 5;
constexpr int kNumRetriesToGetWorldAtReset = 0;

absl::StatusOr<simulation::WorldSdfAdapter::WorldTemplateOverrides>
WorldTemplateOverridesFromConfig(
    const intrinsic_proto::simulation::gazebo::v1::GazeboConfig&
        gazebo_config) {
  simulation::WorldSdfAdapter::WorldTemplateOverrides world_template_overrides;
  if (gazebo_config.has_simulation_options() &&
      gazebo_config.simulation_options().has_physics_update_rate_hz()) {
    const int64_t rate_hz =
        gazebo_config.simulation_options().physics_update_rate_hz();
    if (rate_hz <= 0) {
      return absl::InvalidArgumentError(absl::StrCat(
          "physics_update_rate_hz must be positive, got ", rate_hz));
    }
    world_template_overrides.physics_step_size =
        absl::Seconds(1.0 / static_cast<double>(rate_hz));
  }
  return world_template_overrides;
}

simulation::WorldSdfAdapter::Options WorldAdapterOptions(
    simulation::WorldSdfAdapter::WorldTemplateOverrides
        world_template_overrides) {
  simulation::WorldSdfAdapter::Options opts{
      .mesh_savepath = absl::GetFlag(FLAGS_mesh_savepath),
      .name_scope_separator = std::string(intrinsic::kSdfNameSeparator),
      .ensure_unique_sensor_topic_names = true,
      .skip_failed_sensors = true,
      .skip_unsupported_collision_geos = true,
      .world_template_overrides = std::move(world_template_overrides),
  };
  // Try to load the sdf world template file or die, since we cannot guarantee
  // the simulation server will be functional if the spec from the world
  // template file cannot be added in the generated sdf file for Gazebo.
  std::string resolved_tpl_path = PathResolver::ResolveRunfilesPath(
      absl::GetFlag(FLAGS_sdf_world_template_path));
  ASSIGN_OR_DIE(opts.world_template,
                simulation::WorldTemplate::LoadFromFile(resolved_tpl_path));
  return opts;
}

gz::sim::ServerConfig DefaultServerConfig() {
  gz::sim::ServerConfig server_config;
  server_config.SetHeadlessRendering(true);

  if (absl::GetFlag(FLAGS_use_dart)) {
    server_config.SetPhysicsEngine("static://gz::physics::dartsim::Plugin");
  } else {
    // Use Bullet featherstone by default.
    server_config.SetPhysicsEngine(
        "static://gz::physics::bullet_featherstone::Plugin");
  }
  return server_config;
}

void SetGzRenderingResourcePathEnv() {
  std::string error;
  using ::bazel::tools::cpp::runfiles::Runfiles;
  std::unique_ptr<Runfiles> runfiles(
      Runfiles::Create(program_invocation_name, &error));
  if (runfiles == nullptr) {
    LOG(FATAL) << "Runfiles is null, error: " << error;
  }
  std::string gz_rendering_resource_path =
      runfiles->Rlocation("gz-rendering/", BAZEL_CURRENT_REPOSITORY);
  // This path can be null if the rendering engine is not statically linked
  // into the binary.
  if (!gz_rendering_resource_path.empty()) {
    setenv("GZ_RENDERING_RESOURCE_PATH", gz_rendering_resource_path.c_str(), 1);
  }
  std::string ogre_next_resource_path =
      runfiles->Rlocation("ogre-next/", BAZEL_CURRENT_REPOSITORY);
  // This path can be null if the Ogre-Next RenderSystem plugin is not linked
  // into the binary.
  if (!ogre_next_resource_path.empty()) {
    setenv("OGRE2_RESOURCE_PATH", ogre_next_resource_path.c_str(), 1);
  }
}

void SetupTransportForwarding(GazeboRunner* gazebo) {
  for (const auto& topic : absl::GetFlag(FLAGS_forward_transport_to_dds)) {
    CHECK_OK(gazebo->AddForwardingTopic(topic));
  }
}

absl::StatusOr<std::unique_ptr<GazeboRunner>> InitGazeboWithSdfPath(
    absl::string_view sdf_path, PubSub& pubsub,
    const simulation::SimulatorControlService::
        GazeboRunnerInitParams& /*unused*/) {
  gz::sim::ServerConfig server_config = DefaultServerConfig();

  if (sdf_path.empty()) {
    LOG(ERROR) << "You did not pass a path to a SDF file. Gazebo will run "
               << "default config.";
  } else {
    LOG(INFO) << "Starting Gazebo Simulation Server from SDF file: "
              << sdf_path;
    if (!server_config.SetSdfFile(std::string(sdf_path))) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to set SDF file [", sdf_path, "]"));
    }
  }

  const std::string resolved_sdf_path =
      PathResolver::ResolveRunfilesPath(sdf_path);
  const std::string runfiles_dir =
      resolved_sdf_path.substr(0, resolved_sdf_path.rfind(sdf_path));
  gz::sim::addResourcePaths({runfiles_dir});

  INTR_ASSIGN_OR_RETURN(std::unique_ptr<GazeboRunner> gazebo,
                        GazeboRunner::Create(server_config, &pubsub));
  SetupTransportForwarding(gazebo.get());
  return std::move(gazebo);
}

absl::StatusOr<WorldObjectPlugin::AllPluginSpecs>
GetPluginSpecOverridesFromAssetInstances(
    const World& world, std::string_view asset_instances_service_address) {
  WorldObjectPlugin::AllPluginSpecs empty_result;
  if (asset_instances_service_address.empty()) {
    return empty_result;
  }

  INTR_ASSIGN_OR_RETURN(
      auto client,
      AssetInstancesClient::Create(asset_instances_service_address,
                                   /*connection_timeout=*/absl::Seconds(60)));
  INTR_ASSIGN_OR_RETURN(auto asset_instances, client->ListHardwareDevices());

  simulation::WorldObjectPlugin world_object_plugin(world);
  std::vector<std::string> error_messages;
  for (const auto& instance : asset_instances) {
    const std::string& object_name = instance.name();
    const auto& config =
        instance.config().hardware_device().service().service_config();

    absl::Status status = absl::OkStatus();
    if (config.Is<intrinsic_proto::eoat::GripperConfig>()) {
      intrinsic_proto::eoat::GripperConfig gripper_config;
      if (config.UnpackTo(&gripper_config)) {
        status = world_object_plugin.ConfigureEoatGripper(gripper_config,
                                                          object_name);
      } else {
        status = absl::InternalError(
            absl::StrCat("Failed to unpack GripperConfig for ", object_name));
      }
    } else if (config.Is<intrinsic_proto::gripper::PinchGripperPart>()) {
      intrinsic_proto::gripper::PinchGripperPart pinch_config;
      if (config.UnpackTo(&pinch_config)) {
        status = world_object_plugin.ConfigurePinchGripper(pinch_config,
                                                           object_name);
      } else {
        status = absl::InternalError(absl::StrCat(
            "Failed to unpack PinchGripperPart for ", object_name));
      }
    } else if (config.Is<intrinsic_proto::gripper_service::
                             SuctionGripperRealtimeControlServiceConfig>()) {
      intrinsic_proto::gripper_service::
          SuctionGripperRealtimeControlServiceConfig gripper_config;
      if (config.UnpackTo(&gripper_config)) {
        status = world_object_plugin.ConfigureSuctionGripperRealtimeControl(
            gripper_config, object_name);
      } else {
        status = absl::InternalError(absl::StrCat(
            "Failed to unpack SuctionGripperRealtimeControlServiceConfig for ",
            object_name));
      }
    } else if (config.Is<intrinsic_proto::gripper_service::
                             SuctionGripperOpcuaServiceConfig>()) {
      intrinsic_proto::gripper_service::SuctionGripperOpcuaServiceConfig
          gripper_config;
      if (config.UnpackTo(&gripper_config)) {
        status = world_object_plugin.ConfigureSuctionGripperOpcua(
            gripper_config, object_name);
      } else {
        status = absl::InternalError(absl::StrCat(
            "Failed to unpack SuctionGripperOpcuaServiceConfig for ",
            object_name));
      }
    } else if (config.Is<intrinsic_proto::gripper_service::
                             PinchGripperRealtimeControlServiceConfig>()) {
      intrinsic_proto::gripper_service::PinchGripperRealtimeControlServiceConfig
          gripper_config;
      if (config.UnpackTo(&gripper_config)) {
        status = world_object_plugin.ConfigurePinchGripperRealtimeControl(
            gripper_config, object_name);
      } else {
        status = absl::InternalError(absl::StrCat(
            "Failed to unpack PinchGripperRealtimeControlServiceConfig for ",
            object_name));
      }
    } else if (config.Is<intrinsic_proto::gripper_service::
                             PinchGripperOpcuaServiceConfig>()) {
      intrinsic_proto::gripper_service::PinchGripperOpcuaServiceConfig
          gripper_config;
      if (config.UnpackTo(&gripper_config)) {
        status = world_object_plugin.ConfigurePinchGripperOpcua(gripper_config,
                                                                object_name);
      } else {
        status = absl::InternalError(
            absl::StrCat("Failed to unpack PinchGripperOpcuaServiceConfig for ",
                         object_name));
      }
    } else if (config.Is<intrinsic_proto::perception::v1::CameraConfig>()) {
      intrinsic_proto::perception::v1::CameraConfig camera_config;
      if (config.UnpackTo(&camera_config)) {
        status =
            world_object_plugin.ConfigureCamera(camera_config, object_name);
      } else {
        status = absl::InternalError(
            absl::StrCat("Failed to unpack v1 CameraConfig for ", object_name));
      }
    } else if (config.Is<intrinsic_proto::icon::HardwareModuleConfig>()) {
      intrinsic_proto::icon::HardwareModuleConfig hwm_config;
      if (config.UnpackTo(&hwm_config)) {
        status = world_object_plugin.ConfigureHardwareModule(hwm_config,
                                                             object_name);
      } else {
        status = absl::InternalError(absl::StrCat(
            "Failed to unpack HardwareModuleConfig for ", object_name));
      }
    } else {
      continue;
    }

    if (absl::IsNotFound(status)) {
      LOG(WARNING) << "Failed to configure plugin for " << object_name
                   << " (skipping because it was not found in the world): "
                   << status;
      continue;
    }
    if (!status.ok()) {
      error_messages.push_back(absl::StrCat("Failed to configure plugin for ",
                                            object_name, ": ",
                                            status.ToString()));
    }
  }

  if (!error_messages.empty()) {
    return absl::AbortedError(absl::StrCat(
        "Errors configuring plugins:\n", absl::StrJoin(error_messages, "\n")));
  }

  INTR_ASSIGN_OR_RETURN(
      WorldObjectPlugin::AllPluginSpecs specs,
      world_object_plugin.GetAllPluginSpecs(/*add_gz_plugins=*/true));

  auto log_overrides = [](std::string_view type, const auto& specs_map) {
    if (!specs_map.empty()) {
      std::vector<std::string_view> overridden_names;
      overridden_names.reserve(specs_map.size());
      for (const auto& [name, _] : specs_map) {
        overridden_names.push_back(name);
      }
      LOG(INFO) << "Overriding " << type << " specs for resource(s): "
                << absl::StrJoin(overridden_names, ", ");
    }
  };
  log_overrides("gripper", specs.gripper_specs);
  log_overrides("Gazebo joint plugins", specs.gz_plugins_specs);
  log_overrides("camera", specs.camera_specs);
  if (!specs.hardware_module_objects.empty()) {
    LOG(INFO) << "Configuring following resource(s) as hardware modules: "
              << absl::StrJoin(specs.hardware_module_objects, ", ");
  }

  return specs;
}

absl::StatusOr<std::unique_ptr<GazeboRunner>> InitGazeboWithWorldService(
    std::shared_ptr<ObjectWorldService::StubInterface>
        object_world_service_stub,
    std::unique_ptr<GeometryLibrary> geom_service_library,
    std::string_view simulation_service_address,
    std::string_view simulator_name,
    simulation::WorldSdfAdapter::WorldTemplateOverrides
        world_template_overrides,
    GazeboService& gazebo_service, PubSub& pubsub,
    const simulation::SimulatorControlService::GazeboRunnerInitParams&
        init_params) {
  const int num_retries = init_params.is_reset ? kNumRetriesToGetWorldAtReset
                                               : kNumRetriesToGetWorldAtInit;
  INTR_ASSIGN_OR_RETURN(
      World world,
      simulation::details::DownloadWorldFromObjectWorldService(
          init_params.simulator_world_id, object_world_service_stub.get(),
          geom_service_library->Deserializer(),
          /*num_retries_if_unavailable=*/num_retries));

  auto adapter_options =
      WorldAdapterOptions(std::move(world_template_overrides));

  INTR_ASSIGN_OR_RETURN(
      auto plugin_spec_overrides,
      GetPluginSpecOverridesFromAssetInstances(
          world, init_params.asset_instances_service_address_flag));
  adapter_options.collection_entity_local_name_to_gripper_spec =
      std::move(plugin_spec_overrides.gripper_specs);
  adapter_options.collection_entity_local_name_to_gz_plugins_spec =
      std::move(plugin_spec_overrides.gz_plugins_specs);
  adapter_options.collection_entity_local_name_to_camera_spec =
      std::move(plugin_spec_overrides.camera_specs);
  adapter_options.hardware_module_objects =
      std::move(plugin_spec_overrides.hardware_module_objects);

  INTR_ASSIGN_OR_RETURN(
      auto adapter,
      simulation::details::GetWorldAdapterAndSaveSdf(world, adapter_options));

  INTR_RETURN_IF_ERROR(gazebo_service.SetTopics(adapter->GetObjectTopics()));

  gz::sim::addResourcePaths({absl::GetFlag(FLAGS_mesh_savepath)});
  gz::sim::ServerConfig server_config = DefaultServerConfig();

  LOG(INFO) << "Starting Gazebo Simulation Server from World Service with "
               "simulator world id ["
            << init_params.simulator_world_id << "]";
  if (!server_config.SetSdfString(adapter->GetSDF())) {
    return absl::InternalError("Failed to set SDF string.");
  }

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<GazeboRunner> gazebo,
      GazeboRunner::Create(server_config, &pubsub, adapter->GetSimStepSize()));

  // Add world-scope System plugins that sync the Gazebo scene to the Intrinsic
  // World.
  if (!simulation_service_address.empty()) {
    INTR_ASSIGN_OR_RETURN(auto world_sync_channel,
                          Channel::MakeFromAddress(ConnectionParams::NoIngress(
                              simulation_service_address)));
    auto world_sync_stub =
        intrinsic_proto::simulation::v1::SimulatorWorldSync::NewStub(
            world_sync_channel->GetChannel());
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<simulation::WorldSyncSystem> world_sync_system,
        simulation::WorldSyncSystem::Create(
            simulator_name, init_params.simulator_world_id,
            std::move(world_sync_stub), object_world_service_stub, &pubsub));
    INTR_RET_CHECK_OK(gazebo->AddSystem(world_sync_system))
        << "Failed to add WorldSyncSystem to Gazebo.";
  } else {
    LOG(WARNING) << "Simulation service address was not set. WorldSyncSystem "
                    "will not be added. Updates will not be published.";
  }

  INTR_ASSIGN_OR_RETURN(
      auto spawner_manager,
      simulation::SpawnerManager::Create(object_world_service_stub,
                                         std::move(geom_service_library),
                                         init_params.simulator_world_id));
  INTR_RET_CHECK_OK(gazebo->AddSystem(std::move(spawner_manager)))
      << "Failed to add SpawnerManager System to Gazebo.";

  INTR_ASSIGN_OR_RETURN(
      auto outfeed_manager,
      simulation::OutfeedManager::Create(std::move(object_world_service_stub),
                                         init_params.simulator_world_id));
  INTR_RET_CHECK_OK(gazebo->AddSystem(std::move(outfeed_manager)))
      << "Failed to add OutfeedManager System to Gazebo.";

  SetupTransportForwarding(gazebo.get());
  return std::move(gazebo);
}

absl::StatusOr<std::unique_ptr<GazeboRunner>> InitGazeboWithWorldService(
    std::string_view world_service_address,
    std::string_view geometry_service_address,
    std::string_view simulation_service_address,
    std::string_view simulator_name,
    simulation::WorldSdfAdapter::WorldTemplateOverrides
        world_template_overrides,
    GazeboService& gazebo_service, PubSub& pubsub,
    const simulation::SimulatorControlService::GazeboRunnerInitParams&
        init_params) {
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<ObjectWorldService::Stub> object_world_service_stub,
      simulation::details::GetObjectWorldServiceStub(world_service_address));
  auto geom_service_library = GetGeometryServiceGeometryLibrary(
      geometry_service_address, kGeometryServiceConnectTimeout);

  return InitGazeboWithWorldService(
      std::move(object_world_service_stub), std::move(geom_service_library),
      simulation_service_address, simulator_name,
      std::move(world_template_overrides), gazebo_service, pubsub, init_params);
}

std::string GeometryServiceInterfaceUri() {
  return absl::StrCat(
      assets::kGrpcUriPrefix,
      intrinsic_proto::geometry::GeometryService::service_full_name());
}

absl::StatusOr<std::unique_ptr<GazeboRunner>> InitGazeboWithWorldService(
    const intrinsic_proto::assets::v1::ResolvedDependency&
        intrinsic_runtime_dep,
    std::string_view simulation_service_address,
    std::string_view simulator_name,
    simulation::WorldSdfAdapter::WorldTemplateOverrides
        world_template_overrides,
    GazeboService& gazebo_service, PubSub& pubsub,
    const simulation::SimulatorControlService::GazeboRunnerInitParams&
        init_params) {
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<ObjectWorldService::Stub> object_world_service_stub,
      simulation::details::GetObjectWorldServiceStub(intrinsic_runtime_dep));
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<grpc::Channel> geometry_service_channel,
      assets::dependencies::
          ConnectWithRuntimeAssetFallbackForAssetMigrationOnly(
              intrinsic_runtime_dep, GeometryServiceInterfaceUri(),
              connect::UnlimitedMessageSizeGrpcChannelArgs()));
  INTR_RETURN_IF_ERROR(connect::WaitForChannelReady(
      geometry_service_channel, kGeometryServiceConnectTimeout));
  auto geom_service_library = GetGeometryServiceGeometryLibrary(
      intrinsic_proto::geometry::GeometryService::NewStub(
          std::move(geometry_service_channel)));

  return InitGazeboWithWorldService(
      std::move(object_world_service_stub), std::move(geom_service_library),
      simulation_service_address, simulator_name,
      std::move(world_template_overrides), gazebo_service, pubsub, init_params);
}

void BlockOnSigset(const sigset_t* sig_set) {
  int signum = 0;
  sigwait(sig_set, &signum);
  LOG(INFO) << "Received signal " << signum << ", exiting.";
  return;
}

void SetGzIpEnvVar() {
  if (const char* gz_ip = getenv("GZ_IP"); gz_ip != nullptr) {
    LOG(INFO) << "GZ_IP has been set to " << gz_ip;
    return;
  }

  // TODO(b/517228136): Rename env var, once POD_IP is directly set in
  // https://github.com/intrinsic-ai/intrinsic-core/blob/8fa1de0dee0080a17730773b749ff750a23abc00/google3/intrinsic/assets/deploy/renderutil.go#L133
  if (const char* allowed_pubsub_ip = getenv("ALLOWED_PUBSUB_IPv4");
      allowed_pubsub_ip != nullptr) {
    setenv("GZ_IP", allowed_pubsub_ip, /*replace=*/1);
    LOG(INFO) << "Set GZ_IP to " << allowed_pubsub_ip;
    return;
  }

  LOG(WARNING) << "GZ_IP has not been set, pubsub over gz transport might not "
                  "work across processes.";
  return;
}

absl::StatusOr<std::string> GetSimulatorWorldIdFromSimulationService(
    std::string_view simulation_service_address) {
  INTR_ASSIGN_OR_RETURN(auto channel,
                        Channel::MakeFromAddress(ConnectionParams::NoIngress(
                            simulation_service_address)));
  auto stub = intrinsic_proto::simulation::v1::SimulatorWorldSync::NewStub(
      channel->GetChannel());
  grpc::ClientContext context;
  context.set_deadline(
      absl::ToChronoTime(absl::Now() + kGetSimulatorWorldInfoTimeout));
  intrinsic_proto::simulation::v1::GetSimulatorWorldInfoRequest request;
  intrinsic_proto::simulation::v1::SimulatorWorldInfo response;
  grpc::Status status =
      stub->GetSimulatorWorldInfo(&context, request, &response);
  INTR_RETURN_IF_ERROR(ToAbslStatus(status))
      << "Failed to query simulator world info";
  return response.simulator_world_id();
}

std::unique_ptr<GazeboService> CreateGazeboServiceFromEnv() {
  const char* gz_partition_env = getenv("GZ_PARTITION");
  std::string gz_transport_partition =
      gz_partition_env != nullptr ? gz_partition_env : "";
  return std::make_unique<GazeboService>(gz_transport_partition);
}

}  // namespace

int RunGazeboSimulationServerWithServices(int argc, char* argv[]) {
  InitIntrinsic(argv[0], argc, argv);

  const absl::StatusOr<intrinsic_proto::config::RuntimeContext>
      runtime_context = GetBinaryProto<intrinsic_proto::config::RuntimeContext>(
          absl::GetFlag(FLAGS_runtime_context_file));

  // Block SIGINT and SIGTERM in this thread and all created threads. This
  // ensures that only the sigwait below will catch the signal and cleanly
  // terminate the program.
  // `pthread_sigmask` must be called before any child threads are started
  // below, to block the signals on all threads.
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGTERM);

  if (pthread_sigmask(SIG_BLOCK, &sigset, /*oldset=*/nullptr) != 0) {
    LOG(WARNING)
        << "Failed to block signals. Program may exit without cleaning up.";
  }

  intrinsic::OpenCensusPlugin opencensus(/*service_name=*/"simulation-server",
                                         /*use_otel_metrics=*/true);

  // Always start the gripper, camera, GPIO and SetSimulatedInputs singletons so
  // that their services are available independent of whether or not grippers or
  // cameras are currently present (they might be added later).
  //
  // If we didn't do this, gzserver container deployment would hang/fail waiting
  // for these services to come up if there aren't any such devices, or if SDF
  // parsing fails.
  simulation::SimulatedPinchGripperServerImpl::
      StartSimulatedPinchGripperServiceSingleton();
  perception::GazeboCameraGrpcServices::StartCameraServicesSingleton();
  simulation::GPIOService::StartSingleton();
  (void)simulation::SimInputsServiceSingleton::Get();

  // Must be called before any Gz Transport node is initialized.
  SetGzIpEnvVar();

  if (std::optional<unsigned int> seed = absl::GetFlag(FLAGS_seed);
      seed.has_value()) {
    gz::math::Rand::Seed(seed.value());
  }

  if (absl::GetFlag(FLAGS_enable_auto_inertial)) {
    // Update the global SDF parser config to enable a policy that treats
    // inertial caculation failures as a warning instead of an error,
    // and returns default inertial values when failure occurs.
    sdf::ParserConfig& sdf_parser_config = sdf::ParserConfig::GlobalConfig();
    sdf_parser_config.SetCalculateInertialFailurePolicy(
        sdf::CalculateInertialFailurePolicyType::WARN_AND_USE_DEFAULT_INERTIAL);
  }

  // Configure Gazebo verbosity
  gz::common::Console::SetVerbosity(absl::GetFlag(FLAGS_gz_verbosity_level));

  // Configure Gazebo Rendering resource path from runfiles.
  SetGzRenderingResourcePathEnv();

  PubSub pubsub;
  simulation::SimulatorControlService::GazeboRunnerFactoryFunction factory_fn;
  std::unique_ptr<GazeboService> gazebo_service = CreateGazeboServiceFromEnv();

  std::string simulator_world_id;
  const std::string simulation_service_address =
      absl::GetFlag(FLAGS_simulation_service_address);
  if (!simulation_service_address.empty()) {
    LOG(INFO) << "Querying simulator world info from simulation service at "
              << simulation_service_address;
    ASSIGN_OR_DIE(simulator_world_id, GetSimulatorWorldIdFromSimulationService(
                                          simulation_service_address));
  } else {
    simulator_world_id = absl::GetFlag(FLAGS_simulator_world_id_testonly);
  }

  if (!simulator_world_id.empty()) {
    std::string simulator_name = "gazebo";
    simulation::WorldSdfAdapter::WorldTemplateOverrides
        world_template_overrides;
    if (runtime_context.ok()) {
      if (!runtime_context->name().empty()) {
        simulator_name = runtime_context->name();
      }
      intrinsic_proto::simulation::gazebo::v1::GazeboConfig gazebo_config;
      if (runtime_context->has_config()) {
        QCHECK(runtime_context->config().UnpackTo(&gazebo_config))
            << "Failed to unpack RuntimeContext config of type "
            << runtime_context->config().type_url() << " to GazeboConfig.";
        ASSIGN_OR_DIE(world_template_overrides,
                      WorldTemplateOverridesFromConfig(gazebo_config));
      }
      factory_fn =
          [intrinsic_runtime = gazebo_config.intrinsic_runtime(),
           simulation_service_address, simulator_name, world_template_overrides,
           &gazebo_service, &pubsub](
              const simulation::SimulatorControlService::GazeboRunnerInitParams&
                  init_params) {
            return InitGazeboWithWorldService(
                intrinsic_runtime, simulation_service_address, simulator_name,
                world_template_overrides, *gazebo_service, pubsub, init_params);
          };
    } else {
      const char* world_service_address_env = getenv("WORLD_SERVICE_ADDRESS");
      CHECK_NE(world_service_address_env, nullptr)
          << "Unable to find world service address from environment variable "
             "'WORLD_SERVICE_ADDRESS'";

      const char* geometry_service_address_env =
          getenv("GEOMETRY_SERVICE_ADDRESS");
      CHECK_NE(geometry_service_address_env, nullptr)
          << "Unable to find geometry service address from environment "
             "variable 'GEOMETRY_SERVICE_ADDRESS'";

      factory_fn =
          [world_service_address = std::string(world_service_address_env),
           geometry_service_address = std::string(geometry_service_address_env),
           simulation_service_address, simulator_name, world_template_overrides,
           &gazebo_service, &pubsub](
              const simulation::SimulatorControlService::GazeboRunnerInitParams&
                  init_params) {
            return InitGazeboWithWorldService(
                world_service_address, geometry_service_address,
                simulation_service_address, simulator_name,
                world_template_overrides, *gazebo_service, pubsub, init_params);
          };
    }
  } else {
    // Initialize with path from cmd-line flag.
    factory_fn = absl::bind_front(&InitGazeboWithSdfPath,
                                  absl::GetFlag(FLAGS_sdf_path_testonly),
                                  std::ref(pubsub));
  }
  simulation::SimulatorControlService::Config simulator_control_service_config{
      .start_paused = absl::GetFlag(FLAGS_start_paused),
      .ignore_server_create_error_at_startup =
          absl::GetFlag(FLAGS_ignore_server_create_error_at_startup),
      .measure_performance = absl::GetFlag(FLAGS_measure_performance),
      .simulator_world_id = simulator_world_id};
  ASSIGN_OR_DIE(std::unique_ptr<simulation::SimulatorControlService> service,
                simulation::SimulatorControlService::Create(
                    factory_fn, simulator_control_service_config));

  std::unique_ptr<simulation::GazeboPerformanceLogger> performance_logger;
  if (const std::string data_logger_address =
          absl::GetFlag(FLAGS_data_logger_grpc_service_address);
      !data_logger_address.empty()) {
    if (absl::Status start_logger_status =
            intrinsic::data_logger::StartUpIntrinsicLoggerViaGrpc(
                data_logger_address);
        start_logger_status.ok()) {
      absl::StatusOr<std::unique_ptr<simulation::GazeboPerformanceLogger>>
          logger_or = simulation::GazeboPerformanceLogger::CreateAndStart(
              &pubsub, /*config=*/{});
      if (logger_or.ok()) {
        performance_logger = std::move(*logger_or);
      } else {
        LOG(ERROR) << "Failed to start performance logger: "
                   << logger_or.status();
      }
    } else {
      LOG(ERROR) << "Failed to connect to data logger: " << start_logger_status;
    }
  }

  LOG(INFO) << "Starting simulator control service.";
  std::string sim_control_service_address =
      absl::GetFlag(FLAGS_sim_control_address);
  if (runtime_context.ok()) {
    sim_control_service_address =
        absl::StrFormat("0.0.0.0:%d", runtime_context->port());
    LOG(INFO)
        << "Using simulator control service address from runtime context file: "
        << sim_control_service_address;
  }

  grpc::ServerBuilder builder;
  builder.AddListeningPort(
      sim_control_service_address,
      grpc::InsecureServerCredentials());  // NOLINT (insecure)
  builder.RegisterService(service.get());

  LOG(INFO) << "Registering Gazebo service";
  builder.RegisterService(gazebo_service.get());
  std::unique_ptr<grpc::Server> sim_control_server(builder.BuildAndStart());

  // Wait for SIGINT or SIGTERM signals to exit.
  BlockOnSigset(&sigset);
  return 0;
}

}  // namespace simulation
}  // namespace intrinsic
