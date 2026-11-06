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

#include "intrinsic/icon/server/create_icon_main_loop.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/channel.h"
#include "intrinsic/assets/dependencies/utils.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/control/parts/hal/v1/hal_part_config.pb.h"
#include "intrinsic/icon/control/services/assembly_from_world_service.h"
#include "intrinsic/icon/control/services/dynamics_from_kinematics_service.h"
#include "intrinsic/icon/control/services/geometry_from_geometry_library.h"
#include "intrinsic/icon/control/services/kinematics_from_world_service.h"
#include "intrinsic/icon/control/services/service.h"
#include "intrinsic/icon/control/services/service_collection.h"
#include "intrinsic/icon/control/services/world_from_grpc_service.h"
#include "intrinsic/icon/control/services/world_service.h"
#include "intrinsic/icon/hal/hardware_module_manager.h"
#include "intrinsic/icon/hal/hardware_module_proxy.h"
#include "intrinsic/icon/hal/interfaces/hardware_module_state.fbs.h"
#include "intrinsic/icon/hal/interfaces/hardware_module_state_utils.h"
#include "intrinsic/icon/hal/realtime_clock.h"
#include "intrinsic/icon/server/config/icon_main_config.pb.h"
#include "intrinsic/icon/server/config/services_config.pb.h"
#include "intrinsic/icon/server/main_loop.h"
#include "intrinsic/icon/server/runtime_options.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/util/proto/repeated_field_util.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread_options.h"
#include "intrinsic/util/thread/util.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"

namespace intrinsic::icon {

namespace {

constexpr absl::string_view kGeometryServiceInterface =
    "grpc://intrinsic_proto.geometry.GeometryService";
constexpr absl::string_view kObjectWorldServiceInterface =
    "grpc://intrinsic_proto.world.ObjectWorldService";
constexpr absl::Duration kNonRealtimeReadWriteTimeout = absl::Seconds(20);
constexpr absl::Duration kServiceInitTimeout = absl::Seconds(180);
constexpr absl::Duration kServiceRetryInterval = absl::Seconds(10);

// Connects to `ifaces` of the Intrinsic runtime. If
// `use_runtime_asset_fallback` is set, interfaces missing from
// `intrinsic_runtime` fall back to the runtime via the ingress.
template <size_t N>
absl::StatusOr<std::array<std::shared_ptr<grpc::Channel>, N>> ConnectToRuntime(
    const intrinsic_proto::assets::v1::ResolvedDependency& intrinsic_runtime,
    const absl::string_view (&ifaces)[N], bool use_runtime_asset_fallback) {
  if (use_runtime_asset_fallback) {
    return intrinsic::assets::dependencies::
        ConnectManyWithRuntimeAssetFallbackForAssetMigrationOnly(
            intrinsic_runtime, ifaces,
            connect::UnlimitedMessageSizeGrpcChannelArgs());
  }
  return intrinsic::assets::dependencies::ConnectMany(
      intrinsic_runtime, ifaces,
      connect::UnlimitedMessageSizeGrpcChannelArgs());
}

}  // namespace

absl::StatusOr<intrinsic_proto::icon::IconMainConfig>
ApplyDeactivatedHardwareConfiguration(
    const intrinsic_proto::icon::IconMainConfig& initial_config) {
  intrinsic_proto::icon::IconMainConfig config = initial_config;
  if (initial_config.has_deactivated_hardware_configuration()) {
    for (const auto& deactivated_hw_module_name :
         initial_config.deactivated_hardware_configuration()
             .hardware_module_names()) {
      int removed_hw_modules = RemoveIf(
          config.mutable_hardware_module_names(),
          [&deactivated_hw_module_name](const std::string* hw_module_name) {
            return *hw_module_name == deactivated_hw_module_name;
          });
      if (removed_hw_modules == 0) {
        return absl::InvalidArgumentError(
            absl::StrCat("Hardware module '", deactivated_hw_module_name,
                         "' was requested to be deactivated, but was not found "
                         "in the hardware module names."));
      }
    }

    // If the clock driver is deactivated, clear the field. ICON will tick any
    // remaining hardware modules.
    if (!config.hardware_module_that_drives_clock().empty() &&
        absl::c_linear_search(
            initial_config.deactivated_hardware_configuration()
                .hardware_module_names(),
            config.hardware_module_that_drives_clock())) {
      config.clear_hardware_module_that_drives_clock();
    }
    // If the safety interface hardware module is deactivated, clear the field.
    if (config.realtime_control_config().has_safety_hardware_interface() &&
        absl::c_linear_search(
            initial_config.deactivated_hardware_configuration()
                .hardware_module_names(),
            config.realtime_control_config()
                .safety_hardware_interface()
                .module_name())) {
      config.mutable_realtime_control_config()
          ->clear_safety_hardware_interface();
    }
    for (const auto& part_name :
         initial_config.deactivated_hardware_configuration().part_names()) {
      if (!config.mutable_realtime_control_config()
               ->mutable_parts_by_name()
               ->erase(part_name)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Part '", part_name,
                         "' was requested to be deactivated, but was not found "
                         "in the realtime control config."));
      };
    }
  }
  return config;
}

absl::StatusOr<std::unique_ptr<MainLoop>> CreateIconMainLoopFromProto(
    const intrinsic_proto::icon::IconMainConfig& initial_config,
    const ServerRuntimeOptions& options,
    absl::string_view shared_memory_namespace,
    bool use_runtime_asset_fallback) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::icon::IconMainConfig config,
                        ApplyDeactivatedHardwareConfiguration(initial_config));
  if (config.control_frequency_hz() <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat(" control_frequency_hz is ", config.control_frequency_hz(),
                     ", but should be >0"));
  }

  if (config.hardware_module_read_write_timeout_seconds() <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("hardware_module_read_write_timeout_seconds is ",
                     config.hardware_module_read_write_timeout_seconds(),
                     ", but should be >0"));
  }

  Service::InitParameters init_parameters{
      .server_name = options.server_name,
      .control_frequency_hz = config.control_frequency_hz(),
      .resource_id = options.resource_id};

  bool hard_deadline = config.hard_deadline();

  ThreadOptions thread_options;
  // Runtime mode kNormal is used for real-time ICON instances.
  // Runtime mode kNonRealtime is used for non-real-time ICON instances, e.g.
  // simulation and testing.
  if (options.mode == icon::ServerRuntimeMode::kNormal) {
    // ICON is running in high priority real-time mode.
    thread_options.SetRealtimeHighPriorityAndScheduler();

    // Only set CPU affinity if configured for realtime priority.
    // The optional configuration parameter `realtime_core_override` takes
    // precedence over `rcu_nocbs`.
    // Returns an error if no valid affinity (>0) is found.

    if (config.has_realtime_core_override()) {
      if (config.realtime_core_override() <= 0) {
        return absl::InvalidArgumentError(absl::StrCat(
            "realtime_core_override is ", config.realtime_core_override(),
            ", but should be >0"));
      }
      thread_options.SetAffinity({config.realtime_core_override()});
      LOG(INFO) << "Using realtime_core_override: "
                << config.realtime_core_override();
    } else if (const absl::StatusOr<absl::flat_hash_set<int>> cpu_affinity_set =
                   intrinsic::ReadCpuAffinitySetFromCommandLine();
               cpu_affinity_set.ok()) {
      thread_options.SetAffinity(
          {cpu_affinity_set->begin(), cpu_affinity_set->end()});
      LOG(INFO) << "Using cpu_affinity_set {"
                << absl::StrJoin(cpu_affinity_set->begin(),
                                 cpu_affinity_set->end(), ", ")
                << "} for realtime scheduling.";
    } else {
      return absl::InvalidArgumentError(
          "Realtime Priority was configured in the `IconMainConfig` but no "
          "thread CPU affinity was found.");
    }
  } else {
    thread_options.SetNormalPriorityAndScheduler();
    // Keeping the common scheduling log message for debugging.
    std::string_view normal_priority_message =
        "Configured with normal priority and scheduler.";

    if (hard_deadline) {
      LOG(WARNING) << normal_priority_message << " Ignoring `hard_deadline`.";
      hard_deadline = false;
    } else {
      LOG(INFO) << normal_priority_message;
    }
  }

  absl::Duration read_write_timeout = kNonRealtimeReadWriteTimeout;
  if (options.mode == icon::ServerRuntimeMode::kNormal) {
    read_write_timeout =
        absl::Seconds(config.hardware_module_read_write_timeout_seconds());
  }

  // Create a ServiceCollection with all Services from the config.
  absl::StatusOr<ServiceCollection> service_collection;
  const absl::Time service_init_start_time = absl::Now();
  while ((absl::Now() - service_init_start_time) < kServiceInitTimeout) {
    service_collection = CreateServiceCollectionFromProto(
        config.services(), init_parameters, config.intrinsic_runtime(),
        use_runtime_asset_fallback);
    if (service_collection.ok()) {
      LOG(INFO) << "Successfully initialized the ServiceCollection.";
      break;
    }
    LOG(INFO) << "Failed to initialize the ServiceCollection after "
              << absl::ToDoubleSeconds(absl::Now() - service_init_start_time)
              << " seconds with status: " << service_collection.status();
    absl::SleepFor(kServiceRetryInterval);
  }
  if (!service_collection.ok()) {
    return service_collection.status();
  }

  // Try to attach to each hardware module.
  auto hardware_module_manager = std::make_unique<HardwareModuleManager>();

  absl::flat_hash_map<std::string, intrinsic_proto::icon::HardwareConfig>
      hardware_module_name_to_config;
  for (const auto& hw_module_name : config.hardware_module_names()) {
    // Set default HardwareConfig.
    if (!hardware_module_name_to_config
             .emplace(hw_module_name, intrinsic_proto::icon::HardwareConfig())
             .second) {
      return absl::InvalidArgumentError(absl::StrCat(
          "hardware_module_names contains duplicate entry: ", hw_module_name));
    }
  }
  for (const auto& [hw_module_name, config] : config.hardware_config()) {
    auto it = hardware_module_name_to_config.find(hw_module_name);
    if (it == hardware_module_name_to_config.end()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "hardware_config ", hw_module_name,
          " does not match any hardware_module_names in IconMainConfig."));
    }
    it->second = config;
  }
  auto control_frequency_hz = config.control_frequency_hz();
  if (control_frequency_hz <= 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        " Cycle frequency is ", control_frequency_hz, ", should be >0"));
  }

  // TODO(b/310639164): Reduce timeout back to 30 once we've made sim
  // startup faster and more reliable.
  INTR_RETURN_IF_ERROR(ConnectToHardwareModules(
      hardware_module_name_to_config, hardware_module_manager,
      shared_memory_namespace, config.hardware_module_that_drives_clock(),
      /*module_wait_timeout=*/absl::Seconds(120),
      /*expected_control_period=*/intrinsic::fromHz(control_frequency_hz)));

  MainLoop::ClockMode clock_mode = MainLoop::ClockMode::kInternalClock;
  if (!config.hardware_module_that_drives_clock().empty()) {
    clock_mode = MainLoop::ClockMode::kHardwareModuleDrivenClock;
  }
  auto service_collection_ptr = std::make_unique<ServiceCollection>(
      std::move(service_collection.value()));
  Context context(service_collection_ptr.get(), hardware_module_manager.get(),
                  options);

  // Defaults to kThrottled if logging mode is not set in the config.
  LoggingMode default_logging_mode = FromProto(config.default_logging_mode());

  return MainLoop::Create(
      config.control_frequency_hz(), hard_deadline, read_write_timeout, context,
      config.realtime_control_config(), std::move(service_collection_ptr),
      std::move(hardware_module_manager), default_logging_mode, clock_mode,
      shared_memory_namespace, config.hardware_module_that_drives_clock(),
      std::move(thread_options), !config.disable_malloc_guard());
}

absl::Status ConnectToHardwareModules(
    const absl::flat_hash_map<std::string,
                              intrinsic_proto::icon::HardwareConfig>&
        hardware_module_name_to_config,
    std::unique_ptr<HardwareModuleManager>& hardware_module_manager,
    std::string_view shared_memory_namespace,
    std::string_view hardware_module_that_drives_clock,
    absl::Duration module_wait_timeout,
    intrinsic::Duration expected_control_period) {
  if (!hardware_module_that_drives_clock.empty() &&
      !hardware_module_name_to_config.contains(
          hardware_module_that_drives_clock)) {
    return absl::InvalidArgumentError(
        absl::StrCat(hardware_module_that_drives_clock,
                     " was set to drive the clock but was not found in the set "
                     "of hardware modules."));
  }
  for (const auto& [hw_module_name, hardware_config] :
       hardware_module_name_to_config) {
    INTR_ASSIGN_OR_RETURN(
        auto hw_module,
        WaitForHardwareModule(shared_memory_namespace, hw_module_name,
                              module_wait_timeout, expected_control_period));
    LOG(INFO) << "Successfully connected to hardware module '"
              << hw_module.Name() << "'.";

    auto state = hw_module.GetHardwareModuleState();
    if (state != nullptr &&
        state->code() == intrinsic_fbs::StateCode::kInitFailed) {
      return absl::UnavailableError(absl::StrCat(
          "Initialization of ", hw_module_name,
          " failed with message: ", intrinsic_fbs::GetMessage(state),
          ". Please resolve the issue, then try to clear faults."));
    }

    absl::Status get_clock_status =
        hw_module
            .GetReadOnlyMemorySegment<RealtimeClockUpdate>(
                kRealtimeClockUpdateInterfaceName)
            .status();

    if (hw_module_name == hardware_module_that_drives_clock &&
        !get_clock_status.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Hardware module '", hw_module_name,
          "' is configured as clock driver, but does not provide "
          "a clock interface. Check your configuration to make sure that both "
          "the realtime control service and hardware module config agree on "
          "which hardware module drives the clock"));
    }
    if (hw_module_name != hardware_module_that_drives_clock &&
        get_clock_status.ok()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Hardware module '", hw_module_name,
          "' is *not* configured as clock driver, but provides a clock "
          "interface. Check your configuration to make sure that both the "
          "realtime control service and hardware module config agree on which "
          "hardware module drives the clock"));
    }
    INTR_RETURN_IF_ERROR(hardware_module_manager->Add(
        std::move(hw_module),
        hw_module_name == hardware_module_that_drives_clock, hardware_config));
  }
  return absl::OkStatus();
}

absl::StatusOr<ServiceCollection> CreateServiceCollectionFromProto(
    const intrinsic_proto::icon::ServicesConfig& config,
    const Service::InitParameters& init_parameters,
    const intrinsic_proto::assets::v1::ResolvedDependency& intrinsic_runtime,
    bool use_runtime_asset_fallback) {
  // Connect to ObjectWorldService and GeometryService when runtime asset
  // fallback is enabled (when ICON is run as an Asset) or when explicitly
  // configured, allowing standalone binaries to run without them.
  const bool connect_world =
      config.has_world_service_from_grpc() &&
      (use_runtime_asset_fallback ||
       intrinsic_runtime.interfaces().contains(kObjectWorldServiceInterface));
  const bool connect_geometry =
      use_runtime_asset_fallback ||
      intrinsic_runtime.interfaces().contains(kGeometryServiceInterface);

  std::shared_ptr<grpc::Channel> world_channel;
  std::shared_ptr<grpc::Channel> geometry_channel;
  if (connect_world && connect_geometry) {
    INTR_ASSIGN_OR_RETURN((auto [g_channel, w_channel]),
                          ConnectToRuntime(intrinsic_runtime,
                                           {kGeometryServiceInterface,
                                            kObjectWorldServiceInterface},
                                           use_runtime_asset_fallback));
    geometry_channel = std::move(g_channel);
    world_channel = std::move(w_channel);
  } else if (connect_world) {
    INTR_ASSIGN_OR_RETURN(
        (auto [w_channel]),
        ConnectToRuntime(intrinsic_runtime, {kObjectWorldServiceInterface},
                         use_runtime_asset_fallback));
    world_channel = std::move(w_channel);
  } else if (connect_geometry) {
    INTR_ASSIGN_OR_RETURN(
        (auto [g_channel]),
        ConnectToRuntime(intrinsic_runtime, {kGeometryServiceInterface},
                         use_runtime_asset_fallback));
    geometry_channel = std::move(g_channel);
  }

  WorldService* maybe_world_service_ptr = nullptr;
  std::unique_ptr<WorldFromGrpcService> maybe_world_from_grpc_service;
  if (world_channel != nullptr) {
    INTR_RETURN_IF_ERROR(intrinsic::connect::WaitForChannelReady(
        world_channel, connect::kGrpcClientConnectDefaultTimeout))
        << "While connecting to " << kObjectWorldServiceInterface;
    auto world_service_stub =
        intrinsic_proto::world::ObjectWorldService::NewStub(
            std::move(world_channel));
    INTR_ASSIGN_OR_RETURN(
        maybe_world_from_grpc_service,
        WorldFromGrpcService::Create(
            init_parameters, config.world_service_from_grpc().world_id(),
            std::move(world_service_stub)));
    maybe_world_service_ptr = maybe_world_from_grpc_service.get();
  }
  std::unique_ptr<GeometryFromGeometryLibrary> geometry_from_geometry_library =
      nullptr;
  if (geometry_channel != nullptr) {
    INTR_RETURN_IF_ERROR(intrinsic::connect::WaitForChannelReady(
        geometry_channel, connect::kGrpcClientConnectDefaultTimeout))
        << "While connecting to " << kGeometryServiceInterface;
    auto geometry_service_stub =
        intrinsic_proto::geometry::GeometryService::NewStub(
            std::move(geometry_channel));
    INTR_ASSIGN_OR_RETURN(
        geometry_from_geometry_library,
        GeometryFromGeometryLibrary::Create(std::move(geometry_service_stub)));
  }

  std::unique_ptr<KinematicsFromWorldService> maybe_kinematics_service;
  if (config.has_kinematics_from_world_service() &&
      config.kinematics_from_world_service()) {
    if (maybe_world_service_ptr == nullptr) {
      return absl::FailedPreconditionError(
          "Kinematics Service was configured but no World Service was "
          "found.");
    }
    INTR_ASSIGN_OR_RETURN(maybe_kinematics_service,
                          KinematicsFromWorldService::Create(
                              init_parameters, maybe_world_service_ptr,
                              config.allow_non_real_time_inverse_kinematics()));
  }
  std::unique_ptr<AssemblyFromWorldService> maybe_assembly_service;
  if (config.has_assembly_from_world_service() &&
      config.assembly_from_world_service()) {
    if (maybe_world_service_ptr == nullptr) {
      return FailedPreconditionError(
          "Assembly Service was configured but no World Service was found.");
    }
    INTR_ASSIGN_OR_RETURN(maybe_assembly_service,
                          AssemblyFromWorldService::Create(
                              init_parameters, maybe_world_service_ptr));
  }
  std::unique_ptr<DynamicsFromKinematicsService> maybe_dynamics_service;
  if (config.has_dynamics_from_kinematics_service_config()) {
    if (maybe_kinematics_service == nullptr) {
      return FailedPreconditionError(
          "Dynamics Service was configured but no Kinematics Service was "
          "found.");
    }
    INTR_ASSIGN_OR_RETURN(
        maybe_dynamics_service,
        DynamicsFromKinematicsService::Create(
            init_parameters, maybe_kinematics_service.get(),
            config.dynamics_from_kinematics_service_config()));
  }

  std::vector<std::unique_ptr<Service>> services;
  if (maybe_world_from_grpc_service != nullptr) {
    services.emplace_back(std::move(maybe_world_from_grpc_service));
  }
  if (maybe_kinematics_service != nullptr) {
    services.emplace_back(std::move(maybe_kinematics_service));
  }
  if (maybe_assembly_service != nullptr) {
    services.emplace_back(std::move(maybe_assembly_service));
  }
  if (maybe_dynamics_service != nullptr) {
    services.emplace_back(std::move(maybe_dynamics_service));
  }
  if (geometry_from_geometry_library != nullptr) {
    services.emplace_back(std::move(geometry_from_geometry_library));
  }

  INTR_ASSIGN_OR_RETURN(ServiceCollection service_collection,
                        ServiceCollection::CreateFromServiceListNoInitialize(
                            std::move(services)));

  return std::move(service_collection);
}

}  // namespace intrinsic::icon
