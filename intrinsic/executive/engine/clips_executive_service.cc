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

#include "intrinsic/executive/engine/clips_executive_service.h"

#include <cstdint>
#include <filesystem>  // NOLINT
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/longrunning/operations.pb.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "google/protobuf/timestamp.pb.h"
#include "google/protobuf/util/field_mask_util.h"
#include "grpcpp/channel.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/assets/id_utils.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/assets/proto/installed_assets.grpc.pb.h"
#include "intrinsic/assets/proto/v1alpha1/asset_info_internal.grpc.pb.h"
#include "intrinsic/conductor/proto/conductor.grpc.pb.h"
#include "intrinsic/config/proto/process.pb.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/engine/blackboard.h"
#include "intrinsic/executive/engine/clips_executor.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/engine/span_file_exporter.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/executive/proto/blackboard_service.pb.h"
#include "intrinsic/executive/proto/code_execution_service.grpc.pb.h"
#include "intrinsic/executive/proto/executive_config.pb.h"
#include "intrinsic/executive/proto/executive_debug_service.pb.h"
#include "intrinsic/executive/proto/executive_execution_mode.pb.h"
#include "intrinsic/executive/proto/executive_flags.pb.h"
#include "intrinsic/executive/proto/executive_service.pb.h"
#include "intrinsic/executive/proto/run_metadata.pb.h"
#include "intrinsic/executive/proto/run_response.pb.h"
#include "intrinsic/frontend/solution_service/proto/solution_service.grpc.pb.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/errors/proto/error_report.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/logging/proto/logger_service.pb.h"
#include "intrinsic/resources/client/resource_registry_client.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/simulation/service/proto/first_party/simulation_service.grpc.pb.h"
#include "intrinsic/skills/internal/skill_registry_client.h"
#include "intrinsic/skills/internal/skill_registry_client_interface.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/storage/hot_shared_state/proto/v1/resource_set_service.grpc.pb.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/pagination/page_token_manager.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/unique_id.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/updater/world_updater.grpc.pb.h"
#include "intrinsic/world/service/world_compatibility_service.grpc.pb.h"
#include "intrinsic/world/service/world_service.grpc.pb.h"
#include "opentelemetry/proto/trace/v1/trace.pb.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_metadata.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
#include "riegeli/bytes/cfile_reader.h"
#include "riegeli/records/record_reader.h"

ABSL_FLAG(std::string, skill_registry_address, "",
          "Address of the skill registry service in the form 'host:port'.");
ABSL_FLAG(std::string, resource_registry_address, "",
          "Address of the resource registry service in the form 'host:port'.");
ABSL_FLAG(std::string, installed_assets_service_address, "",
          "Address of the installed assets service in the form 'host:port'.");
ABSL_FLAG(
    std::string, asset_info_internal_service_address, "",
    "Address of the asset info internal service in the form 'host:port'.");
ABSL_FLAG(std::string, code_execution_service_address, "",
          "Address of the code execution service in the form 'host:port'.");
ABSL_FLAG(std::string, data_logger_grpc_service_address, "",
          "(optional) Address of the gRPC service to send logs to, in the form "
          "'host:port'.");
ABSL_FLAG(std::string, solution_service_address, "",
          "(optional) Address of the gRPC installer service in the form "
          "'host:port'.");
ABSL_FLAG(std::string, world_service_address, "",
          "Address of the world service in the form 'host:port'.");
ABSL_FLAG(std::string, world_updater_address, "",
          "Address of the world updater in the form 'host:port'.");
ABSL_FLAG(std::string, simulation_service_address, "",
          "Address of the simulation service in the form 'host:port'.");
ABSL_FLAG(std::string, conductor_service_address, "",
          "Address of the conductor service in the form 'host:port'.");
ABSL_RETIRED_FLAG(std::string, geometry_service_address, "", "Deprecated.");
ABSL_FLAG(absl::Duration, loop_time, absl::Milliseconds(25),
          "Desired minimum time per loop of the executive.");
ABSL_FLAG(absl::Duration, report_active_actions_timeout, absl::Seconds(5),
          "Time interval in which the executive regularly prints out status "
          "information, such as the currently active actions.");
ABSL_FLAG(absl::Duration, grpc_client_connection_timeout,
          intrinsic::connect::kGrpcClientConnectDefaultTimeout,
          "Time to wait for the grpc server to become available.");

ABSL_FLAG(std::vector<std::string>, clips_init_files, {},
          "CLIPS files to load during initialization in addition to the "
          "default executive initialization file (files will be looked for "
          "in directories passed by --clips_dirs).");
ABSL_FLAG(std::vector<std::string>, clips_dirs, {"intrinsic/executive/clips"},
          "Directories to search when loading CLIPS files as a runfiles path "
          "(e.g. intrinsic/executive/clips). First path must contain CLIPS "
          "base files.");
ABSL_RETIRED_FLAG(std::string, clips_action_library_file, "", "Deprecated.");

ABSL_FLAG(std::string, flags_config, "",
          "Filename of pbtxt file with flags config (ExecutiveFlags)");
ABSL_FLAG(std::string, deployment_id, "",
          "Deployment ID used for logging context.");

ABSL_FLAG(std::string, world_id, "", "ID of world in the World Service.");

ABSL_FLAG(std::string, google_cloud_project, "",
          "Google Cloud project ID, used for example to create tracing links.");
ABSL_FLAG(bool, tracing_add_info_to_state, false,
          "Add tracing info to the ExecutiveState.");
// TODO (b/525330816): implement span linking
ABSL_FLAG(bool, tracing_link_opencensus, false,
          "Link tracing spans by adding a child link via opencensus.");
ABSL_FLAG(bool, tracing_show_internal_calls, false,
          "Spans are added that show internal work of the executive, e.g., "
          "gRPC calls.");
ABSL_FLAG(bool, tracing_show_internal_clips_runs_leaking_memory, false,
          "If tracing_show_internal_calls is true, also add spans for each "
          "internal clips run. This feature potentially leaks memory. Never "
          "enable this in staging or prod.");
ABSL_FLAG(
    bool, enable_executive_skill_conflict_default_wait, false,
    "If set to true, default skill conflict handling mode for UNSPECIFIED is "
    "WAIT instead of FAIL.");
ABSL_FLAG(absl::Duration, conductor_client_operation_poll_interval,
          absl::Milliseconds(100),
          "Time interval in which the executive polls the conductor service "
          "for the status of a pending client operation.");

// The number of rules that may fire in a single loop before assuming an error.
constexpr int kDefaultClipsRunLimit = 10000;
constexpr int kWaitMaxDurationMinutes = 60;
constexpr int kWaitIntervalSeconds = 2;

namespace intrinsic {
namespace executive {

namespace {

absl::Status ValidateExecutiveConfig(
    const intrinsic_proto::executive::ExecutiveConfig& config) {
  if (config.skill_registry_address().empty()) {
    return absl::FailedPreconditionError(
        "No skill registry address in executive config");
  }
  if (config.resource_registry_address().empty()) {
    return absl::FailedPreconditionError(
        "No resource registry address in executive config");
  }
  if (config.world_id().empty()) {
    return absl::FailedPreconditionError("No world_id in executive config");
  }
  if (config.world_service_address().empty()) {
    return absl::FailedPreconditionError(
        "No world service address in executive config");
  }
  if (config.clips_dirs().empty()) {
    return absl::InvalidArgumentError(
        "--clips_dirs not set. Must contain at least one directory.");
  }

  return absl::OkStatus();
}

absl::StatusOr<intrinsic_proto::executive::ExecutiveConfig>
BuildExecutiveConfig() {
  intrinsic_proto::executive::ExecutiveConfig config;
  config.set_loop_time_ms(
      absl::ToInt64Milliseconds(absl::GetFlag(FLAGS_loop_time)));
  if (config.clips_run_limit() == 0) {
    config.set_clips_run_limit(kDefaultClipsRunLimit);
  }
  INTR_RETURN_IF_ERROR(
      FromAbslDuration(absl::GetFlag(FLAGS_report_active_actions_timeout),
                       config.mutable_report_active_actions_timeout()));
  absl::Duration grpc_connect_timeout =
      absl::GetFlag(FLAGS_grpc_client_connection_timeout);
  INTR_RETURN_IF_ERROR(FromAbslDuration(
      grpc_connect_timeout, config.mutable_grpc_client_connection_timeout()));
  config.set_skill_registry_address(
      absl::GetFlag(FLAGS_skill_registry_address));
  config.set_resource_registry_address(
      absl::GetFlag(FLAGS_resource_registry_address));
  config.set_installed_assets_service_address(
      absl::GetFlag(FLAGS_installed_assets_service_address));
  config.set_asset_info_internal_service_address(
      absl::GetFlag(FLAGS_asset_info_internal_service_address));
  config.set_code_execution_service_address(
      absl::GetFlag(FLAGS_code_execution_service_address));
  config.set_world_service_address(absl::GetFlag(FLAGS_world_service_address));
  config.set_world_updater_address(absl::GetFlag(FLAGS_world_updater_address));
  config.set_simulation_service_address(
      absl::GetFlag(FLAGS_simulation_service_address));
  config.set_solution_service_address(
      absl::GetFlag(FLAGS_solution_service_address));
  config.set_conductor_service_address(
      absl::GetFlag(FLAGS_conductor_service_address));
  config.set_google_cloud_project(absl::GetFlag(FLAGS_google_cloud_project));
  config.set_tracing_add_info_to_state(
      absl::GetFlag(FLAGS_tracing_add_info_to_state));
  config.set_tracing_link_opencensus(
      absl::GetFlag(FLAGS_tracing_link_opencensus));
  config.set_tracing_show_internal_calls(
      absl::GetFlag(FLAGS_tracing_show_internal_calls));
  config.set_tracing_show_internal_clips_runs_leaking_memory(
      absl::GetFlag(FLAGS_tracing_show_internal_clips_runs_leaking_memory));
  INTR_RETURN_IF_ERROR(FromAbslDuration(
      absl::GetFlag(FLAGS_conductor_client_operation_poll_interval),
      config.mutable_conductor_client_operation_poll_interval()));

  absl::c_copy(absl::GetFlag(FLAGS_clips_init_files),
               google::protobuf::RepeatedPtrFieldBackInserter(
                   config.mutable_clips_init_files()));
  absl::c_copy(absl::GetFlag(FLAGS_clips_dirs),
               google::protobuf::RepeatedPtrFieldBackInserter(
                   config.mutable_clips_dirs()));

  config.set_world_id(absl::GetFlag(FLAGS_world_id));

  if (!absl::GetFlag(FLAGS_flags_config).empty()) {
    intrinsic_proto::executive::ExecutiveFlags flags;
    INTR_RETURN_IF_ERROR(file::GetTextProto(absl::GetFlag(FLAGS_flags_config),
                                            &flags, file::Defaults()));
    absl::c_copy(flags.flags(), google::protobuf::RepeatedPtrFieldBackInserter(
                                    config.mutable_flags()));
  }

  if (const std::string& deployment_id = absl::GetFlag(FLAGS_deployment_id);
      !deployment_id.empty()) {
    bool found = false;
    for (intrinsic_proto::executive::ExecutiveConfig::Flag& flag :
         *config.mutable_flags()) {
      if (flag.name() == "deployment_id") {
        flag.set_string_value(deployment_id);
        found = true;
        break;
      }
    }
    if (!found) {
      intrinsic_proto::executive::ExecutiveConfig::Flag* flag =
          config.mutable_flags()->Add();
      flag->set_name("deployment_id");
      flag->set_string_value(deployment_id);
    }
  }

  if (absl::GetFlag(FLAGS_enable_executive_skill_conflict_default_wait)) {
    intrinsic_proto::executive::ExecutiveConfig::Flag* flag =
        config.mutable_flags()->Add();
    flag->set_name("enable_executive_skill_conflict_default_wait");
    flag->set_bool_value(true);
  }

  INTR_RETURN_IF_ERROR(ValidateExecutiveConfig(config));

  return config;
}

}  // namespace

absl::StatusOr<std::unique_ptr<ClipsExecutiveService>>
ClipsExecutiveService::CreateService() {
  std::unique_ptr<ClipsExecutiveService> service =
      absl::WrapUnique(new ClipsExecutiveService());
  INTR_RETURN_IF_ERROR(service->InitExecutor());
  return std::move(service);
}

absl::StatusOr<std::unique_ptr<ClipsExecutiveService>>
ClipsExecutiveService::CreateService(
    std::unique_ptr<intrinsic::executive::ClipsExecutor> executor) {
  std::unique_ptr<ClipsExecutiveService> service =
      absl::WrapUnique(new ClipsExecutiveService(std::move(executor)));
  service->InitLogging(absl::GetFlag(FLAGS_grpc_client_connection_timeout));
  return std::move(service);
}

ClipsExecutiveService::ClipsExecutiveService(
    std::unique_ptr<intrinsic::executive::ClipsExecutor> executor)
    : executor_(std::move(executor)) {}

void ClipsExecutiveService::Shutdown() {
  LOG(INFO) << "Shutdown requested";
  absl::MutexLock lock_operations(operations_mutex_);
  absl::MutexLock lock_executor(executor_mutex_);

  quit_ = true;

  if (!operations_.empty()) {
    LOG(INFO) << "Canceling running operations";

    for (const auto& [name, data] : operations_) {
      LOG(INFO) << "  - CANCEL " << name;

      if (data.operation.done()) {
        LOG(INFO) << "    ALREADY DONE " << name;
        continue;
      }

      absl::Status status = executor_->RequestToCancel(name);
      if (status.ok()) {
        LOG(INFO) << "    CANCELLED OK";
      } else {
        LOG(INFO) << "    ERROR: " << status.message();
      }
    }
  }
}

// Important: This must continue to be called even if the logger is not used
// directly in this file. Downstream components (e.g. skill service client)
// depend on the logger being initialized by the executive.
void ClipsExecutiveService::InitLogging(absl::Duration grpc_connect_timeout) {
  if (logging_initialized_) {
    return;  // StartUpIntrinsicLoggerViaGrpc should only be called once at
             // startup.
  }
  if (std::string data_logger_address =
          absl::GetFlag(FLAGS_data_logger_grpc_service_address);
      !data_logger_address.empty()) {
    auto s = intrinsic::data_logger::StartUpIntrinsicLoggerViaGrpc(
        data_logger_address, grpc_connect_timeout);
    if (!s.ok()) {
      LOG(ERROR) << "Failed to connect to data logger: " << s;
    }
  }
  logging_initialized_ = true;
}

absl::Status ClipsExecutiveService::InitExecutor() {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::executive::ExecutiveConfig config,
                        BuildExecutiveConfig());
  absl::Duration grpc_connect_timeout =
      ToAbslDurationNoValidation(config.grpc_client_connection_timeout());
  InitLogging(grpc_connect_timeout);

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic::skills::SkillRegistryClientInterface>
          skill_registry_client,
      intrinsic::skills::CreateSkillRegistryClient(
          config.skill_registry_address(), grpc_connect_timeout));

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic::resources::ResourceRegistryClientInterface>
          resource_registry_client,
      intrinsic::resources::CreateResourceRegistryClient(
          config.resource_registry_address(), absl::Seconds(60),
          grpc_connect_timeout));

  std::unique_ptr<
      intrinsic_proto::assets::v1::InstalledAssetsReader::StubInterface>
      installed_assets_stub;
  if (!config.installed_assets_service_address().empty()) {
    // TODO(b/356228011): Pagination might be a better approach for this.
    grpc::ChannelArguments channel_args =
        connect::UnlimitedMessageSizeGrpcChannelArgs();
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> installed_assets_channel,
        connect::CreateClientChannel(config.installed_assets_service_address(),
                                     absl::Now() + grpc_connect_timeout,
                                     channel_args));
    installed_assets_stub =
        intrinsic_proto::assets::v1::InstalledAssetsReader::NewStub(
            installed_assets_channel);
  }

  if (config.asset_info_internal_service_address().empty()) {
    return absl::InvalidArgumentError(
        "asset_info_internal_service_address must be set in config.");
  }
  std::shared_ptr<
      intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface>
      asset_info_internal_stub;
  {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> asset_info_internal_channel,
        connect::CreateClientChannel(
            config.asset_info_internal_service_address(),
            absl::Now() + grpc_connect_timeout));
    asset_info_internal_stub =
        intrinsic_proto::assets::v1alpha1::AssetInfoInternal::NewStub(
            asset_info_internal_channel);
  }

  std::unique_ptr<intrinsic_proto::executive::CodeExecutionService::Stub>
      code_execution_service_stub;
  if (!config.code_execution_service_address().empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> code_execution_channel,
        connect::CreateClientChannel(config.code_execution_service_address(),
                                     absl::Now() + grpc_connect_timeout));
    code_execution_service_stub =
        intrinsic_proto::executive::CodeExecutionService::NewStub(
            code_execution_channel);
  }

  std::unique_ptr<intrinsic_proto::world::internal::WorldService::Stub>
      world_service_stub;
  std::unique_ptr<intrinsic_proto::world::ObjectWorldService::Stub>
      object_world_service_stub;
  std::unique_ptr<intrinsic_proto::world::WorldCompatibilityService::Stub>
      world_compatibility_service_stub;

  if (!config.world_service_address().empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> world_channel,
        connect::CreateClientChannel(config.world_service_address(),
                                     absl::Now() + grpc_connect_timeout));
    world_service_stub =
        intrinsic_proto::world::internal::WorldService::NewStub(world_channel);
    object_world_service_stub =
        intrinsic_proto::world::ObjectWorldService::NewStub(world_channel);
    world_compatibility_service_stub =
        intrinsic_proto::world::WorldCompatibilityService::NewStub(
            world_channel);
  }

  std::unique_ptr<intrinsic_proto::world::WorldUpdater::Stub>
      world_updater_stub;

  if (!config.world_updater_address().empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> world_updater_channel,
        connect::CreateClientChannel(config.world_updater_address(),
                                     absl::Now() + grpc_connect_timeout));
    world_updater_stub =
        intrinsic_proto::world::WorldUpdater::NewStub(world_updater_channel);
  }

  std::unique_ptr<
      intrinsic_proto::simulation::first_party::SimulationService::Stub>
      simulation_service_stub;
  if (!config.simulation_service_address().empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> simulation_channel,
        connect::CreateClientChannel(config.simulation_service_address(),
                                     absl::Now() + grpc_connect_timeout));
    simulation_service_stub =
        intrinsic_proto::simulation::first_party::SimulationService::NewStub(
            simulation_channel);
  } else {
    LOG(WARNING) << "No simulation service address.";
  }

  std::unique_ptr<intrinsic_proto::solution::v1::SolutionService::StubInterface>
      solution_service_stub;
  if (!config.solution_service_address().empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> solution_service_data_channel,
        connect::CreateClientChannel(config.solution_service_address(),
                                     absl::Now() + grpc_connect_timeout));
    solution_service_stub =
        intrinsic_proto::solution::v1::SolutionService::NewStub(
            solution_service_data_channel);
  }

  std::unique_ptr<intrinsic_proto::conductor::ConductorService::Stub>
      conductor_service_stub;
  if (!config.conductor_service_address().empty()) {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<grpc::Channel> conductor_channel,
        connect::CreateClientChannel(config.conductor_service_address(),
                                     absl::Now() + grpc_connect_timeout));
    conductor_service_stub =
        intrinsic_proto::conductor::ConductorService::NewStub(
            conductor_channel);
  } else {
    LOG(WARNING) << "No conductor service address.";
  }

  {
    // Ensure that no other method is currently using the executor.
    absl::WriterMutexLock lock_executor(executor_mutex_);
    INTR_ASSIGN_OR_RETURN(
        executor_,
        intrinsic::executive::ClipsExecutor::Create(
            {.config = std::move(config),
             .skill_registry_client = std::move(skill_registry_client),
             .resource_registry_client = std::move(resource_registry_client),
             .installed_assets_stub = std::move(installed_assets_stub),
             .asset_info_internal_stub = std::move(asset_info_internal_stub),
             .code_execution_service_stub =
                 std::move(code_execution_service_stub),
             .world_service_stub = std::move(world_service_stub),
             .object_world_service_stub = std::move(object_world_service_stub),
             .world_updater_stub = std::move(world_updater_stub),
             .world_compatibility_service_stub =
                 std::move(world_compatibility_service_stub),
             .simulation_service_stub = std::move(simulation_service_stub),
             .solution_service_stub = std::move(solution_service_stub),
             .conductor_service_stub = std::move(conductor_service_stub)}));

    INTR_RETURN_IF_ERROR(executor_->Init());
  }

  return absl::OkStatus();
}

grpc::Status ClipsExecutiveService::CreateOperation(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::CreateOperationRequest* request,
    google::longrunning::Operation* response) {
  LOG(INFO) << "CreateOperation called: "
            << "has_behavior_tree: " << request->has_behavior_tree()
            << ", has_process_id: " << request->has_process_id()
            << ", process_id: " << request->process_id();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  if (!operations_.empty()) {
    return ToGrpcStatus(absl::AlreadyExistsError(
        "An operation has already been created. Currently the executive "
        "supports only a single operation at a time. Call DeleteOperation to "
        "remove the existing operation."));
  }

  absl::MutexLock lock_executor(executor_mutex_);

  std::string operation_name = UniqueId();

  if (request->runnable_type_case() ==
      intrinsic_proto::executive::CreateOperationRequest::
          RUNNABLE_TYPE_NOT_SET) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "No runnable to load specified in CreateOperationRequest"));
  }

  intrinsic_proto::status::ExtendedStatus diagnostics;
  switch (request->runnable_type_case()) {
    case intrinsic_proto::executive::CreateOperationRequest::kBehaviorTree:
      LOG(INFO) << "Loading behavior tree";
      INTR_RETURN_IF_ERROR_GRPC(executor_->CreateOperation(
          operation_name, request->behavior_tree(), &diagnostics));
      break;

    case intrinsic_proto::executive::CreateOperationRequest::kProcessId: {
      if (request->process_id().package().empty() ||
          request->process_id().name().empty()) {
        return ToGrpcStatus(
            absl::InvalidArgumentError("process_id must not have empty package "
                                       "or name when creating an operation"));
      }
      INTR_ASSIGN_OR_RETURN_GRPC(
          std::string process_id_str,
          assets::IdFromProto(request->process_id()),
          _ << "The requested process_id must be a valid asset id.");
      LOG(INFO) << "Loading behavior tree from: " << process_id_str;
      INTR_RETURN_IF_ERROR_GRPC(executor_->CreateOperationFromProcessId(
          operation_name, request->process_id(), &diagnostics));
    } break;

    default:
      return ToGrpcStatus(absl::InvalidArgumentError("Unknown runnable type."));
  }

  OperationData operation_data;
  operation_data.operation.set_name(operation_name);
  operation_data.metadata.set_world_id(absl::GetFlag(FLAGS_world_id));
  if (diagnostics.has_status_code()) {  // only set if there are diagnostics
    *operation_data.metadata.mutable_diagnostics() = std::move(diagnostics);
  }
  UpdateOperationData(&operation_data);

  *response = operation_data.operation;
  LOG(INFO) << "Created operation " << operation_name;
  operations_.insert({std::move(operation_name), std::move(operation_data)});
  LOG(INFO) << "Behavior tree loaded successfully.";
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::ListOperations(
    grpc::ServerContext* context,
    const google::longrunning::ListOperationsRequest* request,
    google::longrunning::ListOperationsResponse* response) {
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  absl::MutexLock lock_executor(executor_mutex_);
  for (auto& [name, operation_data] : operations_) {
    UpdateOperationData(&operation_data);
    *response->add_operations() = operation_data.operation;
  }
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::DeleteOperation(
    grpc::ServerContext* context,
    const google::longrunning::DeleteOperationRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "DeleteOperation called: name: " << request->name();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  absl::MutexLock lock_executor(executor_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(executor_->DeleteOperation(request->name()));
  operations_.erase(request->name());
  LOG(INFO) << "Delete done.";
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::GetOperation(
    grpc::ServerContext* context,
    const google::longrunning::GetOperationRequest* request,
    google::longrunning::Operation* response) {
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(OperationData * operation,
                             FindOperationData(request->name()),
                             _.LogWarning());
  absl::MutexLock lock_executor(executor_mutex_);
  UpdateOperationData(operation);
  *response = operation->operation;
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::GetOperationView(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetOperationViewRequest* request,
    google::longrunning::Operation* response) {
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(OperationData * operation,
                             FindOperationData(request->name()));
  absl::MutexLock lock_executor(executor_mutex_);
  UpdateOperationData(operation);
  *response = operation->operation;
  if (request->has_metadata_fieldmask()) {
    if (request->metadata_fieldmask().paths().empty()) {
      response->clear_metadata();
    } else {
      intrinsic_proto::executive::RunMetadata metadata = operation->metadata;
      google::protobuf::util::FieldMaskUtil::TrimMessage(
          request->metadata_fieldmask(), &metadata);
      response->mutable_metadata()->PackFrom(std::move(metadata));
    }
  } else if (request->has_view()) {
    if (request->view() ==
        intrinsic_proto::executive::GetOperationViewRequest::VIEW_STATE_ONLY) {
      intrinsic_proto::executive::RunMetadata metadata = operation->metadata;
      metadata.clear_behavior_tree();
      response->mutable_metadata()->PackFrom(std::move(metadata));
    }
  }
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::GetOperationMetadata(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetOperationMetadataRequest* request,
    intrinsic_proto::executive::RunMetadata* response) {
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(OperationData * operation,
                             FindOperationData(request->name()),
                             _.LogWarning());
  absl::MutexLock lock_executor(executor_mutex_);
  UpdateOperationData(operation);
  *response = operation->metadata;
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::WaitOperation(
    grpc::ServerContext* context,
    const google::longrunning::WaitOperationRequest* request,
    google::longrunning::Operation* response) {
  LOG(INFO) << "WaitOperation called: name: " << request->name()
            << ", timeout: " << request->timeout();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  INTR_ASSIGN_OR_RETURN_GRPC(absl::Duration timeout,
                             ToAbslDuration(request->timeout()));

  if (timeout == absl::ZeroDuration()) {
    timeout = absl::Minutes(kWaitMaxDurationMinutes);
  }
  absl::Time deadline = absl::Now() + timeout;
  bool done = false;
  do {
    {
      absl::MutexLock lock_operations(operations_mutex_);
      INTR_ASSIGN_OR_RETURN_GRPC(OperationData * operation,
                                 FindOperationData(request->name()));
      absl::MutexLock lock_executor(executor_mutex_);
      UpdateOperationData(operation);
      done = operation->operation.done();
    }
    absl::SleepFor(absl::Seconds(kWaitIntervalSeconds));
  } while (!done && !quit_ && !context->IsCancelled() &&
           absl::Now() < deadline);

  if (context->IsCancelled()) {
    return ToGrpcStatus(absl::CancelledError("Service call was cancelled"));
  }
  if (quit_) {
    return ToGrpcStatus(absl::CancelledError("Service is being shut down"));
  }

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(OperationData * operation,
                             FindOperationData(request->name()));
  absl::MutexLock lock_executor(executor_mutex_);
  UpdateOperationData(operation);
  *response = operation->operation;
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::ResetOperation(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::ResetOperationRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "ResetOperation called: name: " << request->name()
            << ", keep_blackboard: " << request->keep_blackboard();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(OperationData * operation_data,
                             FindOperationData(request->name()));
  LOG(INFO) << "Resetting operation " << request->name() << " "
            << (request->keep_blackboard() ? "keeping" : "flushing")
            << " blackboard";
  absl::MutexLock lock_executor(executor_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(
      executor_->ResetOperation(request->name(), request->keep_blackboard()));

  operation_data->operation.set_done(false);
  operation_data->operation.clear_error();
  operation_data->operation.clear_response();

  LOG(INFO) << "Reset done.";
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::ForceAbandonOperation(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::ForceAbandonOperationRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "ForceAbandonOperation called: name: " << request->name();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  LOG(INFO) << "Abandoning operation " << request->name();
  absl::MutexLock lock_executor(executor_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(executor_->RequestToAbandon(request->name()));

  LOG(INFO) << "Abandon requested.";
  return grpc::Status::OK;
}

absl::Status ClipsExecutiveService::StartOperation(
    OperationData* operation_data,
    const std::optional<google::protobuf::Any>& parameters,
    const absl::flat_hash_map<std::string, std::string>& resources,
    const std::optional<std::string_view>& scene_id,
    std::vector<intrinsic_proto::executive::BehaviorTree::NodeIdentifier>
        recovery_nodes)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_, executor_mutex_) {
  LOG(INFO) << "Starting operation...";
  INTR_ASSIGN_OR_RETURN(clips::TraceSpanReferenceId execution_span_id,
                        executor_->StartExecutionSpan());

  std::unique_ptr<intrinsic::stats::ScopedSpan> start_span;
  if (absl::GetFlag(FLAGS_tracing_show_internal_calls)) {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<opentelemetry::trace::Span> execution_span,
        executor_->GetSpanManager()->GetSpanForParenting(execution_span_id));
    start_span = std::make_unique<intrinsic::stats::ScopedSpan>(
        "Executive Start", execution_span);
  }
  clips::TraceSpanManager::ScopedSpan execution_span{
      execution_span_id, executor_->GetSpanManager(),
      opentelemetry::trace::StatusCode::kError, "Failed to start execution"};

  INTR_RETURN_IF_ERROR(executor_->RequestToStart(
      operation_data->operation.name(),
      {.execution_span_id = execution_span_id,
       .parameters = parameters,
       .resources = resources,
       .scene_id = scene_id.value_or(ClipsExecutor::kDefaultSceneId),
       .recovery_nodes = std::move(recovery_nodes)}));
  // release execution span here as the executor has started without errors
  // and is now responsible to end the span.
  execution_span.Release();

  {
    absl::MutexLock lock(*executor_->GetClipsMutex());
    executor_->WaitForLoop();  // Wait until executor processes request.
    absl::Status update_status = executor_->UpdateInitialMetadata(
        operation_data->operation.name(), &operation_data->metadata);
    if (!update_status.ok()) {
      LOG(WARNING) << "Failed to update operation "
                   << operation_data->operation.name()
                   << " metadata: " << update_status.message();
    }
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutiveService::SetStartOperationModes(
    const intrinsic_proto::executive::StartOperationRequest* request)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_, executor_mutex_) {
  if (request->has_start_tree_id() && request->has_start_node_id()) {
    INTR_RETURN_IF_ERROR(executor_->SetStartNode(
        request->name(), request->start_tree_id(), request->start_node_id()));
  }
  if (!request->has_start_tree_id()) {
    INTR_RETURN_IF_ERROR(executor_->SetStartNode(request->name(), "", 0));
  }

  if (request->execution_mode() !=
      intrinsic_proto::executive::EXECUTION_MODE_UNSPECIFIED) {
    INTR_RETURN_IF_ERROR(executor_->SetExecutionMode(
        request->name(), request->execution_mode()));
  } else {
    INTR_RETURN_IF_ERROR(executor_->SetExecutionMode(
        request->name(), intrinsic_proto::executive::EXECUTION_MODE_NORMAL));
  }

  // If request simulation mode is unspecified, use the same mode as the
  // previous execution.
  if (request->simulation_mode() !=
      intrinsic_proto::executive::SIMULATION_MODE_UNSPECIFIED) {
    INTR_RETURN_IF_ERROR(executor_->SetSimulationMode(
        request->name(), request->simulation_mode()));
  }

  if (request->skill_trace_handling() !=
      intrinsic_proto::executive::RunMetadata::TracingInfo::
          SKILL_TRACES_UNSPECIFIED) {
    if (request->skill_trace_handling() ==
        intrinsic_proto::executive::RunMetadata::TracingInfo::
            SKILL_TRACES_LINK) {
      LOG_EVERY_N_SEC(WARNING, 60)
          << "Skill trace linking (SKILL_TRACES_LINK) is currently unsupported "
             "with OpenTelemetry; span links will be omitted (b/525330816).";
    }
    INTR_RETURN_IF_ERROR(executor_->SetSkillTraceHandling(
        request->name(), request->skill_trace_handling()));
  } else {
    LOG_EVERY_N_SEC(WARNING, 60)
        << "Skill trace linking (SKILL_TRACES_LINK) is currently unsupported "
           "with OpenTelemetry; span links will be omitted (b/525330816).";
    INTR_RETURN_IF_ERROR(executor_->SetSkillTraceHandling(
        request->name(), intrinsic_proto::executive::RunMetadata::TracingInfo::
                             SKILL_TRACES_LINK));
  }

  return absl::OkStatus();
}

grpc::Status ClipsExecutiveService::StartOperation(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::StartOperationRequest* request,
    google::longrunning::Operation* response) {
  LOG(INFO) << "StartOperation called: name: " << request->name()
            << ", execution_mode: "
            << intrinsic_proto::executive::ExecutionMode_Name(
                   request->execution_mode())
            << ", simulation_mode: "
            << intrinsic_proto::executive::SimulationMode_Name(
                   request->simulation_mode())
            << ", skill_trace_handling: "
            << intrinsic_proto::executive::RunMetadata::TracingInfo::
                   SkillTraceHandling_Name(request->skill_trace_handling())
            << ", start_tree_id: " << request->start_tree_id()
            << ", start_node_id: " << request->start_node_id()
            << ", has_parameters: " << request->has_parameters()
            << ", resources_size: " << request->resources_size()
            << ", has_scene_id: " << request->has_scene_id()
            << ", scene_id: " << request->scene_id()
            << ", recovery_nodes_size: " << request->recovery_nodes_size();

  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(OperationData * operation_data,
                             FindOperationData(request->name()));

  if ((request->has_start_tree_id() && !request->has_start_node_id()) ||
      (!request->has_start_tree_id() && request->has_start_node_id())) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "StartOperation must have either both start_tree_id and start_node_id "
        "set or neither."));
  }

  if ((request->has_start_tree_id() || request->has_start_node_id()) &&
      request->recovery_nodes_size() > 0) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "StartOperation must not have start_tree_id/start_node_id and recovery "
        "nodes set. Starting a specific node and recovering at the same time "
        "is not supported."));
  }

  // If the process tree is a PBT (more specifically if it specifies
  // parameters), then the request must pass in parameters.
  if (!operation_data->metadata.behavior_tree()
           .description()
           .parameter_description()
           .parameter_message_full_name()
           .empty() &&
      !request->has_parameters()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The process tree is a parameterizable tree and requires the "
        "'parameters' field to be set when starting."));
  }
  if (!operation_data->metadata.behavior_tree()
           .description()
           .resource_selectors()
           .empty() &&
      request->resources().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The process tree is a parameterizable tree and requires the "
        "'resources' field to be set when starting."));
  }

  absl::MutexLock lock_executor(executor_mutex_);

  INTR_RETURN_IF_ERROR_GRPC(SetStartOperationModes(request));

  std::optional<google::protobuf::Any> parameters;
  if (request->has_parameters()) {
    parameters = request->parameters();
  }
  absl::flat_hash_map<std::string, std::string> resources;
  if (!request->resources().empty()) {
    for (const auto& [resource_key, resource_handle] : request->resources()) {
      resources[resource_key] = resource_handle;
    }
  }

  std::optional<std::string_view> scene_id = std::nullopt;
  if (request->has_scene_id()) {
    scene_id = request->scene_id();
  }

  std::vector<intrinsic_proto::executive::BehaviorTree::NodeIdentifier>
      recovery_nodes(request->recovery_nodes().begin(),
                     request->recovery_nodes().end());

  INTR_RETURN_IF_ERROR_GRPC(StartOperation(operation_data, parameters,
                                           resources, scene_id,
                                           std::move(recovery_nodes)));

  UpdateOperationData(operation_data);
  *response = operation_data->operation;
  LOG(INFO) << "Started operation";
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::SuspendOperation(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::SuspendOperationRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "SuspendOperation called: name: " << request->name();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  absl::MutexLock lock_executor(executor_mutex_);
  LOG(INFO) << "Suspending process tree execution...";
  INTR_RETURN_IF_ERROR_GRPC(executor_->RequestToSuspend(request->name()));
  {
    absl::MutexLock lock(*executor_->GetClipsMutex());
    executor_->WaitForLoop();  // Wait until executor processes request.
  }
  LOG(INFO) << "Suspension of process tree triggered";
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::ResumeOperation(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::ResumeOperationRequest* request,
    google::longrunning::Operation* response) {
  LOG(INFO)
      << "ResumeOperation called: name: " << request->name() << ", mode: "
      << intrinsic_proto::executive::ResumeOperationRequest::ResumeMode_Name(
             request->mode());
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(OperationData * operation,
                             FindOperationData(request->name()));
  intrinsic_proto::executive::ResumeOperationRequest::ResumeMode resume_mode =
      request->mode();
  if (resume_mode == intrinsic_proto::executive::ResumeOperationRequest::
                         RESUME_MODE_UNSPECIFIED) {
    resume_mode = intrinsic_proto::executive::ResumeOperationRequest::CONTINUE;
  }

  absl::MutexLock lock_executor(executor_mutex_);
  if (resume_mode ==
      intrinsic_proto::executive::ResumeOperationRequest::CONTINUE) {
    INTR_RETURN_IF_ERROR_GRPC(executor_->SetExecutionMode(
        request->name(), intrinsic_proto::executive::EXECUTION_MODE_NORMAL));
  }

  LOG(INFO) << "Resuming process tree execution...";
  INTR_RETURN_IF_ERROR_GRPC(
      executor_->RequestToResume(request->name(), resume_mode));
  {
    absl::MutexLock lock(*executor_->GetClipsMutex());
    executor_->WaitForLoop();  // Wait until executor processes request.
  }

  LOG(INFO) << "Resume triggered";
  UpdateOperationData(operation);
  *response = operation->operation;
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::CancelOperation(
    grpc::ServerContext* context,
    const google::longrunning::CancelOperationRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "CancelOperation called: name: " << request->name();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));
  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  absl::MutexLock lock_executor(executor_mutex_);
  LOG(INFO) << "Cancelling process tree execution...";
  INTR_RETURN_IF_ERROR_GRPC(executor_->RequestToCancel(request->name()));
  {
    absl::MutexLock lock(*executor_->GetClipsMutex());
    executor_->WaitForLoop();  // Wait until executor processes request.
  }

  LOG(INFO) << "Process tree cancellation triggered.";
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::CreateBreakpoint(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::CreateBreakpointRequest* request,
    intrinsic_proto::executive::BehaviorTree::Breakpoint* response) {
  LOG(INFO) << "CreateBreakpoint called: name: " << request->name()
            << ", breakpoint: " << request->breakpoint().ShortDebugString();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  absl::MutexLock lock_executor(executor_mutex_);
  if (!request->has_breakpoint()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "CreateBreakpointRequest is missing a breakpoint specification"));
  }

  INTR_RETURN_IF_ERROR_GRPC(executor_->CreateBreakpoint(
      request->name(), request->breakpoint().tree_id(),
      request->breakpoint().node_id(), request->breakpoint().type()));
  *response = request->breakpoint();
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::DeleteBreakpoint(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::DeleteBreakpointRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "DeleteBreakpoint called: name: " << request->name()
            << ", breakpoint: " << request->breakpoint().ShortDebugString();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));
  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  absl::MutexLock lock_executor(executor_mutex_);
  if (!request->has_breakpoint()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "DeleteBreakpointRequest is missing a breakpoint specification"));
  }
  return ToGrpcStatus(executor_->DeleteBreakpoint(
      request->name(), request->breakpoint().tree_id(),
      request->breakpoint().node_id()));
}

grpc::Status ClipsExecutiveService::DeleteAllBreakpoints(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::DeleteAllBreakpointsRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "DeleteAllBreakpoints called: name: " << request->name();
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  absl::MutexLock lock_executor(executor_mutex_);
  return ToGrpcStatus(executor_->DeleteAllBreakpoints(request->name()));
}

grpc::Status ClipsExecutiveService::ListBreakpoints(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::ListBreakpointsRequest* request,
    intrinsic_proto::executive::ListBreakpointsResponse* response) {
  if (quit_)
    return ToGrpcStatus(absl::UnavailableError("Server shutting down"));

  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  absl::MutexLock lock_executor(executor_mutex_);

  INTR_ASSIGN_OR_RETURN_GRPC(
      std::vector<intrinsic_proto::executive::BehaviorTree::Breakpoint>
          breakpoints,
      executor_->ListBreakpoints(request->name()));
  for (const intrinsic_proto::executive::BehaviorTree::Breakpoint& breakpoint :
       breakpoints) {
    *response->add_breakpoints() = breakpoint;
  }
  return grpc::Status::OK;
}

grpc::Status ClipsExecutiveService::SetNodeExecutionSettings(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::SetNodeExecutionSettingsRequest* request,
    intrinsic_proto::executive::SetNodeExecutionSettingsResponse* response) {
  LOG(INFO) << "SetNodeExecutionSettings called: name: " << request->name()
            << ", tree_id: " << request->tree_id()
            << ", node_id: " << request->node_id() << ", execution_settings: "
            << request->execution_settings().ShortDebugString();
  absl::MutexLock lock_operations(operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(FindOperationData(request->name()).status());
  absl::MutexLock lock_executor(executor_mutex_);
  if (request->tree_id().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "SetNodeExecutionSettingsRequest is missing a tree id"));
  }
  if (!request->has_execution_settings()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "SetNodeExecutionSettingsRequest is missing ExecutionSettings"));
  }
  INTR_RETURN_IF_ERROR_GRPC(executor_->SetNodeExecutionSettings(
      request->name(), request->tree_id(), request->node_id(),
      request->execution_settings()));
  return grpc::Status::OK;
}

absl::StatusOr<ClipsExecutiveService::OperationData*>
ClipsExecutiveService::FindOperationData(std::string_view name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_) {
  auto find_it = operations_.find(name);
  if (find_it == operations_.end()) {
    return absl::NotFoundError(
        absl::StrFormat("Operation '%s' is unknown.", name));
  }
  return &find_it->second;
}

void ClipsExecutiveService::UpdateOperationData(
    ClipsExecutiveService::OperationData* operation_data)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(operations_mutex_)
        ABSL_SHARED_LOCKS_REQUIRED(executor_mutex_) {
  // TODO(b/493547558): Replace the logic below by reusing the operation's
  // Operation and RunMetadata protos in the ProtobufManager, which CLIPS
  // already keeps up to date (see state_proto_update.clp).
  operation_data->metadata.set_behavior_tree_state(
      intrinsic_proto::executive::BehaviorTree::UNSPECIFIED);
  operation_data->metadata.set_operation_state(
      intrinsic_proto::executive::RunMetadata::UNSPECIFIED);
  absl::StatusOr<intrinsic_proto::executive::BehaviorTree> bt =
      executor_->GetProcessTree(operation_data->operation.name());
  if (bt.ok()) {
    *operation_data->metadata.mutable_behavior_tree() = std::move(*bt);
  } else {
    operation_data->metadata.clear_behavior_tree();
  }
  absl::StatusOr<intrinsic_proto::executive::RunMetadata::State>
      operation_state =
          executor_->GetOperationState(operation_data->operation.name());
  if (operation_state.ok()) {
    operation_data->metadata.set_operation_state(*operation_state);

    intrinsic_proto::executive::BehaviorTree::State tree_state;
    switch (*operation_state) {
      case intrinsic_proto::executive::RunMetadata::UNSPECIFIED:
        tree_state = intrinsic_proto::executive::BehaviorTree::UNSPECIFIED;
        break;
      case intrinsic_proto::executive::RunMetadata::ACCEPTED:
        tree_state = intrinsic_proto::executive::BehaviorTree::ACCEPTED;
        break;
      case intrinsic_proto::executive::RunMetadata::PREPARING:
        tree_state = intrinsic_proto::executive::BehaviorTree::RUNNING;
        break;
      case intrinsic_proto::executive::RunMetadata::RUNNING:
        tree_state = intrinsic_proto::executive::BehaviorTree::RUNNING;
        break;
      case intrinsic_proto::executive::RunMetadata::SUSPENDING:
        tree_state = intrinsic_proto::executive::BehaviorTree::SUSPENDING;
        break;
      case intrinsic_proto::executive::RunMetadata::SUSPENDED:
        tree_state = intrinsic_proto::executive::BehaviorTree::SUSPENDED;
        break;
      case intrinsic_proto::executive::RunMetadata::CANCELING:
        tree_state = intrinsic_proto::executive::BehaviorTree::CANCELING;
        break;
      case intrinsic_proto::executive::RunMetadata::SUCCEEDED:
        tree_state = intrinsic_proto::executive::BehaviorTree::SUCCEEDED;
        break;
      case intrinsic_proto::executive::RunMetadata::FAILED:
        tree_state = intrinsic_proto::executive::BehaviorTree::FAILED;
        break;
      case intrinsic_proto::executive::RunMetadata::CANCELED:
        tree_state = intrinsic_proto::executive::BehaviorTree::CANCELED;
        break;
      default:
        tree_state = intrinsic_proto::executive::BehaviorTree::UNSPECIFIED;
        LOG(ERROR) << "Unknown operation state: " << *operation_state;
        break;
    }
    operation_data->metadata.set_behavior_tree_state(tree_state);
  }
  absl::StatusOr<absl::Time> start_time =
      executor_->GetStartTime(operation_data->operation.name());
  if (start_time.ok()) {
    if (!FromAbslTime(*start_time,
                      operation_data->metadata.mutable_start_time())
             .ok()) {
      operation_data->metadata.clear_start_time();
      LOG(ERROR) << "Could not encode start time " << start_time
                 << " into timestamp proto";
    }
  } else {
    operation_data->metadata.clear_start_time();
  }
  absl::StatusOr<absl::Duration> execution_duration =
      executor_->GetExecutionDuration(operation_data->operation.name());
  if (execution_duration.ok()) {
    if (!FromAbslDuration(*execution_duration,
                          operation_data->metadata.mutable_execution_time())
             .ok()) {
      operation_data->metadata.clear_execution_time();
      LOG(ERROR) << "Could not encode execution time " << execution_duration
                 << " into duration proto";
    }
  } else {
    operation_data->metadata.clear_execution_time();
  }

  absl::StatusOr<std::string> scene_id =
      executor_->GetSceneId(operation_data->operation.name());
  if (scene_id.ok()) {
    operation_data->metadata.set_scene_id(*scene_id);
  } else {
    operation_data->metadata.clear_scene_id();
  }

  *operation_data->metadata.mutable_log_context() =
      executor_->GetStateLogContext(operation_data->operation.name());
  operation_data->operation.mutable_metadata()->PackFrom(
      operation_data->metadata);

  // No more metadata changes beyond this point (because it has already been
  // packed).

  if (!operation_data->operation.done()) {
    if (operation_data->metadata.operation_state() ==
        intrinsic_proto::executive::RunMetadata::SUCCEEDED) {
      operation_data->operation.set_done(true);
      operation_data->operation.mutable_response();
      absl::StatusOr<google::protobuf::Any> operation_return_value =
          executor_->GetOperationReturnValue(operation_data->operation.name());
      if (operation_return_value.ok()) {
        intrinsic_proto::executive::RunResponse response;
        *response.mutable_result() = *operation_return_value;
        operation_data->operation.mutable_response()->PackFrom(response);
      }
    } else if (operation_data->metadata.operation_state() ==
               intrinsic_proto::executive::RunMetadata::FAILED) {
      operation_data->operation.set_done(true);

      absl::StatusOr<intrinsic_proto::status::ExtendedStatus> extended_status =
          executor_->GetOperationExtendedStatusWithLegacyErrors(
              operation_data->operation.name());

      // Check if the operation extended status contains a title or user report
      // message and use that as the operation error message, otherwise fall
      // back to a generic message.
      if (extended_status.ok() && !extended_status->title().empty()) {
        *operation_data->operation.mutable_error() =
            SaveStatusAsRpcStatus(absl::AbortedError(extended_status->title()));
      } else if (extended_status.ok() &&
                 !extended_status->user_report().message().empty()) {
        *operation_data->operation.mutable_error() = SaveStatusAsRpcStatus(
            absl::AbortedError(extended_status->user_report().message()));
      } else if (extended_status.ok()) {
        *operation_data->operation.mutable_error() = SaveStatusAsRpcStatus(
            absl::AbortedError("Operation failed, please check details."));
      } else {
        *operation_data->operation.mutable_error() = SaveStatusAsRpcStatus(
            absl::InternalError("Operation failed with unknown error."));
      }

      if (extended_status.ok()) {
        google::protobuf::Any es_detail;
        es_detail.PackFrom(*extended_status);
        *operation_data->operation.mutable_error()->add_details() =
            std::move(es_detail);
      }
    } else if (operation_data->metadata.operation_state() ==
               intrinsic_proto::executive::RunMetadata::CANCELED) {
      operation_data->operation.set_done(true);
      *operation_data->operation.mutable_error() = SaveStatusAsRpcStatus(
          absl::CancelledError("User cancellation of behavior tree finished."));
    }
  }
}

absl::StatusOr<std::unique_ptr<ExecutiveDebugService>>
ExecutiveDebugService::CreateService(
    ClipsExecutiveService* clips_executive_service) {
  if (clips_executive_service == nullptr) {
    return absl::InvalidArgumentError(
        "clips_executive_service must not be nullptr.");
  }

  return absl::WrapUnique(new ExecutiveDebugService(clips_executive_service));
}

grpc::Status ExecutiveDebugService::SetClipsTracing(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::SetClipsTracingRequest* request,
    intrinsic_proto::executive::SetClipsTracingResponse* response) {
  LOG(INFO)
      << "SetClipsTracing called: clips_tracing: "
      << intrinsic_proto::executive::SetClipsTracingRequest::ClipsTracing_Name(
             request->clips_tracing());
  if (request->clips_tracing() ==
      intrinsic_proto::executive::SetClipsTracingRequest::UNSPECIFIED) {
    return ToGrpcStatus(
        absl::InvalidArgumentError("SetClipsTracing called with UNSPECIFIED"));
  }

  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);

  return ToGrpcStatus(clips_executive_service_->executor_->SetClipsTracing(
      request->clips_tracing()));
}

grpc::Status ExecutiveDebugService::GetClipsTracing(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetClipsTracingRequest* request,
    intrinsic_proto::executive::GetClipsTracingResponse* response) {
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);

  absl::StatusOr<uint64_t> tracing_position =
      clips_executive_service_->executor_->GetClipsTracingPosition();
  if (!tracing_position.ok()) {
    response->set_clips_tracing(
        intrinsic_proto::executive::GetClipsTracingResponse::DISABLED);
  } else {
    response->set_clips_tracing(
        intrinsic_proto::executive::GetClipsTracingResponse::ENABLED);
    response->set_trace_log_offset(*tracing_position);
  }
  return grpc::Status::OK;
}

grpc::Status ExecutiveDebugService::SetSpanFileWriting(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::SetSpanFileWritingRequest* request,
    intrinsic_proto::executive::SetSpanFileWritingResponse* response) {
  LOG(INFO) << "SetSpanFileWriting called: enabled: " << request->enabled();
  if (request->enabled()) {
    SpanFileExporter::Enable();
  } else {
    SpanFileExporter::Disable();
  }

  return grpc::Status::OK;
}

grpc::Status ExecutiveDebugService::GetSpanFileWriting(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetSpanFileWritingRequest* request,
    intrinsic_proto::executive::GetSpanFileWritingResponse* response) {
  response->set_enabled(SpanFileExporter::IsEnabled());
  return grpc::Status::OK;
}

grpc::Status ExecutiveDebugService::GetSpanTrace(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetSpanTraceRequest* request,
    grpc::ServerWriter<intrinsic_proto::executive::GetSpanTraceResponse>*
        writer) {
  SpanFileExporter::ForceFlush();

  std::string trace_id_hex = request->trace_id();
  if (trace_id_hex.empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError("trace_id cannot be empty"));
  }

  int32_t start_batch_index = request->start_batch_index();
  if (start_batch_index <= 0) {
    start_batch_index = 1;
  }

  std::string output_dir = SpanFileExporter::GetOutputDirectory();
  std::error_code ec;
  if (!std::filesystem::exists(output_dir, ec) ||
      !std::filesystem::is_directory(output_dir, ec)) {
    // Output directory doesn't exist, so no traces have been written yet.
    // We just return OK with an empty stream.
    return grpc::Status::OK;
  }

  // 1. Snapshot Max Index: Find the highest completed file index for this
  // trace_id.
  int max_available_index = 0;
  int current_index = start_batch_index;

  // Scan sequentially from start_batch_index to find the max available.
  // We stop at the first missing file.
  while (true) {
    std::string filename =
        absl::StrFormat("trace_%s_%06d.riegeli", trace_id_hex, current_index);
    std::filesystem::path filepath =
        std::filesystem::path(output_dir) / filename;
    if (std::filesystem::exists(filepath, ec)) {
      max_available_index = current_index;
      current_index++;
    } else {
      break;
    }
  }

  if (max_available_index < start_batch_index) {
    // Nothing new to stream.
    return grpc::Status::OK;
  }

  // 2. Stream files in the range [start_batch_index, max_available_index].
  for (int file_index = start_batch_index; file_index <= max_available_index;
       ++file_index) {
    if (context->IsCancelled()) {
      return grpc::Status::CANCELLED;
    }

    std::string filename =
        absl::StrFormat("trace_%s_%06d.riegeli", trace_id_hex, file_index);
    std::filesystem::path filepath =
        std::filesystem::path(output_dir) / filename;

    riegeli::RecordReader reader(riegeli::CFileReader(filepath.string()));
    if (!reader.ok()) {
      LOG(ERROR) << "Failed to open Riegeli file for streaming: "
                 << filepath.string() << " status: " << reader.status();
      return ToGrpcStatus(reader.status());
    }

    opentelemetry::proto::trace::v1::Span proto_span;
    while (reader.ReadRecord(proto_span)) {
      intrinsic_proto::executive::GetSpanTraceResponse response;
      *response.mutable_span() = proto_span;
      response.set_batch_index(file_index);

      if (!writer->Write(response)) {
        // Stream broken (e.g. client disconnected).
        return grpc::Status::CANCELLED;
      }
    }

    if (!reader.Close()) {
      LOG(ERROR) << "Failed to close Riegeli reader: " << reader.status();
    }
  }

  return grpc::Status::OK;
}

grpc::Status ExecutiveDebugService::GetClipsTrace(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetClipsTraceRequest* request,
    intrinsic_proto::executive::GetClipsTraceResponse* response) {
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);

  INTR_ASSIGN_OR_RETURN_GRPC(
      std::string trace_file_contents,
      clips_executive_service_->executor_->GetClipsTraceFileContents(
          request->trace_log_start_offset()));
  response->set_trace_log(trace_file_contents);
  LOG(INFO) << "Retrieved CLIPS trace file of size: "
            << trace_file_contents.size();
  return grpc::Status::OK;
}

grpc::Status ExecutiveDebugService::GetClipsData(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetClipsDataRequest* request,
    intrinsic_proto::executive::GetClipsDataResponse* response) {
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);

  INTR_ASSIGN_OR_RETURN_GRPC(
      intrinsic_proto::executive::GetClipsDataResponse internal_data,
      clips_executive_service_->executor_->GetClipsData());
  *response = std::move(internal_data);
  return grpc::Status::OK;
}

absl::StatusOr<std::unique_ptr<ExecutiveBlackboardService>>
ExecutiveBlackboardService::CreateService(
    ClipsExecutiveService* clips_executive_service) {
  if (clips_executive_service == nullptr) {
    return absl::InvalidArgumentError(
        "clips_executive_service must not be nullptr.");
  }

  return absl::WrapUnique(
      new ExecutiveBlackboardService(clips_executive_service));
}

grpc::Status ExecutiveBlackboardService::GetBlackboardValue(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::GetBlackboardValueRequest* request,
    intrinsic_proto::executive::BlackboardValue* response) {
  if (request->operation_name().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The operation name for a list request cannot be empty"));
  }
  absl::MutexLock lock_operations(clips_executive_service_->operations_mutex_);
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);
  *response->mutable_key() = request->key();

  std::string scope;
  if (request->has_scope() && !request->scope().empty()) {
    scope = request->scope();
  } else {
    INTR_ASSIGN_OR_RETURN_GRPC(
        scope, clips_executive_service_->executor_->GetProcessTreeScope(
                   request->operation_name()));
  }

  response->set_key(request->key());
  response->set_scope(scope);
  response->set_operation_name(request->operation_name());
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response->mutable_value(),
      clips_executive_service_->executor_->GetBlackboardValue(
          request->key(), scope, request->operation_name()));
  return grpc::Status::OK;
}

grpc::Status ExecutiveBlackboardService::UpdateBlackboardValue(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::UpdateBlackboardValueRequest* request,
    intrinsic_proto::executive::BlackboardValue* response) {
  LOG(INFO) << "UpdateBlackboardValue called: operation_name: "
            << request->value().operation_name()
            << ", key: " << request->value().key()
            << ", scope: " << request->value().scope();
  if (request->value().operation_name().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The operation name for a value to update cannot be empty"));
  }

  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);

  std::string scope;
  if (!request->value().scope().empty()) {
    scope = request->value().scope();
  } else {
    INTR_ASSIGN_OR_RETURN_GRPC(
        scope, clips_executive_service_->executor_->GetProcessTreeScope(
                   request->value().operation_name()));
  }

  INTR_RETURN_IF_ERROR_GRPC(
      clips_executive_service_->executor_->UpdateBlackboardValue(
          request->value().key(), scope, request->value().operation_name(),
          request->value().value()));

  *response = request->value();
  return grpc::Status::OK;
}

grpc::Status ExecutiveBlackboardService::ListBlackboardValues(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::ListBlackboardValuesRequest* request,
    intrinsic_proto::executive::ListBlackboardValuesResponse* response) {
  if (request->operation_name().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The operation name for a list request cannot be empty"));
  }
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(
      std::vector<intrinsic_proto::executive::BlackboardValue> values,
      clips_executive_service_->executor_->ListBlackboardValues(
          request->scope(), request->operation_name(), request->view()));
  response->mutable_values()->Assign(std::make_move_iterator(values.begin()),
                                     std::make_move_iterator(values.end()));
  return grpc::Status::OK;
}

grpc::Status ExecutiveBlackboardService::DeleteBlackboardValue(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::DeleteBlackboardValueRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "DeleteBlackboardValue called: operation_name: "
            << request->operation_name() << ", key: " << request->key()
            << ", scope: " << request->scope();
  if (request->operation_name().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The operation name for a list request cannot be empty"));
  }
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);

  std::string scope;
  if (!request->scope().empty()) {
    scope = request->scope();
  } else {
    INTR_ASSIGN_OR_RETURN_GRPC(
        scope, clips_executive_service_->executor_->GetProcessTreeScope(
                   request->operation_name()));
  }

  INTR_RETURN_IF_ERROR_GRPC(
      clips_executive_service_->executor_->DeleteBlackboardValue(
          request->key(), scope, request->operation_name()));
  return grpc::Status::OK;
}

grpc::Status ExecutiveBlackboardService::CreateBlackboardSnapshot(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::CreateBlackboardSnapshotRequest* request,
    intrinsic_proto::executive::CreateBlackboardSnapshotResponse* response) {
  LOG(INFO)
      << "CreateBlackboardSnapshot called: operation_name: "
      << request->operation_name()
      << ", display_name: " << request->display_name() << ", snapshot_source: "
      << intrinsic_proto::executive::BlackboardSnapshot::SnapshotSource_Name(
             request->snapshot_source());
  if (request->operation_name().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The operation name for a save request cannot be empty"));
  }
  absl::MutexLock lock_operations(clips_executive_service_->operations_mutex_);
  // Verify the referred operation does actually exist.
  INTR_RETURN_IF_ERROR_GRPC(
      clips_executive_service_->FindOperationData(request->operation_name())
          .status());
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response->mutable_snapshot(),
      clips_executive_service_->executor_->CreateBlackboardSnapshot(
          request->operation_name(), request->display_name(),
          request->snapshot_source()));
  return grpc::Status::OK;
}

grpc::Status ExecutiveBlackboardService::LoadBlackboardSnapshot(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::LoadBlackboardSnapshotRequest* request,
    intrinsic_proto::executive::LoadBlackboardSnapshotResponse* response) {
  LOG(INFO) << "LoadBlackboardSnapshot called: operation_name: "
            << request->operation_name() << ", handle: " << request->handle();
  if (request->operation_name().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "The operation name for a load request cannot be empty"));
  }
  if (request->handle().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "No snapshot handle specified in LoadBlackboardSnapshotRequest"));
  }
  Blackboard::SnapshotHandle handle =
      Blackboard::SnapshotHandle(request->handle());
  absl::MutexLock lock_operations(clips_executive_service_->operations_mutex_);
  INTR_RETURN_IF_ERROR_GRPC(
      clips_executive_service_->FindOperationData(request->operation_name())
          .status());
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);
  INTR_ASSIGN_OR_RETURN_GRPC(
      *response->mutable_diagnostics(),
      clips_executive_service_->executor_->LoadBlackboardSnapshot(
          handle, request->operation_name()));
  return grpc::Status::OK;
}

grpc::Status ExecutiveBlackboardService::ListBlackboardSnapshots(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::ListBlackboardSnapshotsRequest* request,
    intrinsic_proto::executive::ListBlackboardSnapshotsResponse* response) {
  INTR_ASSIGN_OR_RETURN_GRPC(
      int page_size,
      list_snapshots_page_size_validator_.Run(request->page_size()));

  int page_offset = 0;
  if (!request->page_token().empty()) {
    INTR_ASSIGN_OR_RETURN_GRPC(auto page_token_proto,
                               page_token::Deopacify(request->page_token()));
    INTR_ASSIGN_OR_RETURN_GRPC(
        intrinsic_proto::executive::ListBlackboardSnapshotsRequest
            token_request,
        page_token::RecoverRequest<
            intrinsic_proto::executive::ListBlackboardSnapshotsRequest>(
            page_token_proto));
    if (request->page_size() != token_request.page_size()) {
      return ToGrpcStatus(absl::InvalidArgumentError(absl::StrFormat(
          "Request with page token must have identical parameters to the "
          "original request. Page size differs: %d (current) <-> %d (token)",
          request->page_size(), token_request.page_size())));
    }
    if (!absl::SimpleAtoi(page_token_proto.start_after_id(), &page_offset)) {
      return ToGrpcStatus(absl::InvalidArgumentError(
          absl::StrFormat("Token had an invalid start after id: %s",
                          page_token_proto.start_after_id())));
    }
  }

  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);

  std::pair<std::vector<intrinsic_proto::executive::BlackboardSnapshot>, int>
      snapshot_list =
          clips_executive_service_->executor_->ListBlackboardSnapshots(
              page_size, page_offset);
  response->mutable_snapshots()->Assign(snapshot_list.first.begin(),
                                        snapshot_list.first.end());
  if (snapshot_list.second > 0) {
    INTR_ASSIGN_OR_RETURN_GRPC(
        std::string next_page_token,
        page_token::Opacify(*request, absl::StrCat(snapshot_list.second)));
    response->set_next_page_token(next_page_token);
  }
  return grpc::Status::OK;
}

grpc::Status ExecutiveBlackboardService::DeleteBlackboardSnapshot(
    grpc::ServerContext* context,
    const intrinsic_proto::executive::DeleteBlackboardSnapshotRequest* request,
    google::protobuf::Empty* response) {
  LOG(INFO) << "DeleteBlackboardSnapshot called: handle: " << request->handle();
  if (request->handle().empty()) {
    return ToGrpcStatus(absl::InvalidArgumentError(
        "No snapshot handle specified in DeleteBlackboardSnapshotRequest"));
  }
  absl::MutexLock lock_executor(clips_executive_service_->executor_mutex_);
  Blackboard::SnapshotHandle handle =
      Blackboard::SnapshotHandle(request->handle());
  INTR_RETURN_IF_ERROR_GRPC(
      clips_executive_service_->executor_->DeleteBlackboardSnapshot(handle));
  return grpc::Status::OK;
}

}  // namespace executive
}  // namespace intrinsic
