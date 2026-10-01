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

#include "intrinsic/executive/engine/clips_executor.h"

#include <malloc.h>
#include <stddef.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/cleanup/cleanup.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "grpcpp/client_context.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/config/proto/process.pb.h"
#include "intrinsic/executive/clips/cc/cel.h"
#include "intrinsic/executive/clips/cc/operation_error.h"
#include "intrinsic/executive/clips/cc/recovery.h"
#include "intrinsic/executive/clips/cc/time.h"
#include "intrinsic/executive/clips/clips_init.h"
#include "intrinsic/executive/clips_cpp/environment.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/slot_value.h"
#include "intrinsic/executive/clips_cpp/trace.h"
#include "intrinsic/executive/clips_cpp/trace_span_manager.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/engine/clips_conductor_client.h"
#include "intrinsic/executive/engine/clips_importer.h"
#include "intrinsic/executive/engine/clips_logger.h"
#include "intrinsic/executive/engine/clips_metrics.h"
#include "intrinsic/executive/engine/clips_skill_dispatcher.h"
#include "intrinsic/executive/engine/clips_world.h"
#include "intrinsic/executive/engine/code_execution_dispatcher.h"
#include "intrinsic/executive/engine/extended_status_codes.h"
#include "intrinsic/executive/engine/pubsub.h"
#include "intrinsic/executive/engine/skill_client_generator.h"
#include "intrinsic/executive/proto/behavior_tree.pb.h"
#include "intrinsic/executive/proto/blackboard_service.pb.h"
#include "intrinsic/executive/proto/executive_config.pb.h"
#include "intrinsic/executive/proto/executive_debug_service.pb.h"
#include "intrinsic/executive/proto/executive_execution_mode.pb.h"
#include "intrinsic/executive/proto/executive_service.pb.h"
#include "intrinsic/executive/proto/log_items.pb.h"
#include "intrinsic/executive/proto/process_tree.pb.h"
#include "intrinsic/executive/proto/run_metadata.pb.h"
#include "intrinsic/executive/proto/run_response.pb.h"
#include "intrinsic/frontend/solution_service/proto/solution_service.grpc.pb.h"
#include "intrinsic/frontend/solution_service/proto/solution_service.pb.h"
#include "intrinsic/frontend/solution_service/proto/status.pb.h"
#include "intrinsic/logging/data_logger_client.h"
#include "intrinsic/logging/log_item_builder.h"
#include "intrinsic/logging/proto/context.pb.h"
#include "intrinsic/stats/scoped_span.h"
#include "intrinsic/stats/tracing_utils.h"
#include "intrinsic/util/grpc/grpc.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/extended_status.pb.h"
#include "intrinsic/util/status/log_if_error.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_specs.h"
#include "intrinsic/util/thread/stop_token.h"
#include "intrinsic/util/thread/thread.h"
#include "opentelemetry/context/context.h"
#include "opentelemetry/context/runtime_context.h"
#include "opentelemetry/trace/scope.h"
#include "opentelemetry/trace/span.h"
#include "opentelemetry/trace/span_context.h"
#include "opentelemetry/trace/span_metadata.h"
#include "opentelemetry/trace/span_startoptions.h"
#include "opentelemetry/trace/tracer.h"
#include "ortools/base/path.h"
#include "re2/re2.h"

ABSL_FLAG(std::string, clips_trace_logfile, "",
          "If not empty, write log file of CLIPS traces.");

ABSL_FLAG(bool, enable_internal_logging, false,
          "Enable output of privileged internal logging.");

ABSL_FLAG(bool, enable_detailed_extended_status_debug, false,
          "When issuing an ExtendedStatus attach detailed internal debug "
          "information");

namespace intrinsic {
namespace executive {
namespace {

using ::intrinsic_proto::executive::BehaviorTree;

// RunWithStatus() will sleep for this duration to enable other threads to
// interject and by acquiring the CLIPS mutex.
constexpr absl::Duration kRunThreadSleepTime = absl::Microseconds(50);
constexpr char kWorldIdFlag[] = "world-id";
constexpr char kInternalLogger[] = "clips-executor-internal";
constexpr char kIntrinsicInternalProject[] = "giza-workcells";

constexpr char kSymbolRegex[] = R"re([[:alpha:]][a-zA-Z0-9_-]+)re";

constexpr char kExecutiveClipsInitFile[] = "executive-init.clp";

constexpr char kClipsDebugTraceLogDefault[] =
    "/clips-logs/clips_debug_trace_%timestamp%.txt";

// Conjunctions of execute states used for testing compatibility of requests.

// States in which the executor is not processing a request.
const std::vector<intrinsic_proto::executive::RunMetadata::State>&
GetWaitingStates() {
  static absl::NoDestructor<
      std::vector<intrinsic_proto::executive::RunMetadata::State>>
      states({intrinsic_proto::executive::RunMetadata::ACCEPTED,
              intrinsic_proto::executive::RunMetadata::SUSPENDED,
              intrinsic_proto::executive::RunMetadata::SUCCEEDED,
              intrinsic_proto::executive::RunMetadata::FAILED,
              intrinsic_proto::executive::RunMetadata::CANCELED});
  return *states;
}

absl::Status SetSearchDirs(
    clips::Environment* env,
    const google::protobuf::RepeatedPtrField<std::string>& clips_dirs)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  for (const auto& d : clips_dirs) {
    const std::string statement =
        absl::StrFormat("(file-load-add-search-dir \"%s\")",
                        file::AddSlash(PathResolver::ResolveRunfilesPath(d)));
    INTR_RETURN_IF_ERROR(env->Evaluate(statement).status());
  }
  return absl::OkStatus();
}

absl::Status LoadInitFiles(
    clips::Environment* env,
    const google::protobuf::RepeatedPtrField<std::string>& clips_init_filenames,
    const google::protobuf::RepeatedPtrField<std::string>& clips_dirs)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  clips::Values init_files(clips_init_filenames.begin(),
                           clips_init_filenames.end());
  INTR_RETURN_IF_ERROR(
      env->AssertFact("flag", {{"name", clips::Symbol("init-files")},
                               {"type", clips::Symbol("STRING")},
                               {"values", init_files}})
          .status());

  INTR_ASSIGN_OR_RETURN(clips::Value loaded,
                        env->EvaluateExpectSingleReturn(absl::StrFormat(
                            R"((file-load "%s"))", kExecutiveClipsInitFile)));
  if (loaded.GetValueType() == clips::Value::Type::kSymbol &&
      loaded.GetSymbol().value() == clips::Symbol::False()) {
    return absl::NotFoundError(absl::StrFormat(
        R"(File "%s" could not be found in any of [%s])",
        kExecutiveClipsInitFile, absl::StrJoin(clips_dirs, ", ")));
  }
  return absl::OkStatus();
}

absl::Status AssertNoFatalClipsErrors(clips::Environment* env)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  std::vector<clips::Fact> fatal_error_facts =
      env->QueryFacts("error", {{"type", clips::Symbol("FATAL")}});
  if (!fatal_error_facts.empty()) {
    return absl::InternalError(absl::StrFormat(
        "Init failed: %s",
        absl::StrJoin(GetClipsLegacyErrorMessages(env, /*message_only=*/false,
                                                  /*fatal_only=*/true),
                      ", ")));
  }
  return absl::OkStatus();
}

absl::Status AssertWorldDownloadSuccess(clips::Environment* env)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(env->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Value world_proto_id_val,
      env->EvaluateExpectSingleReturn("(world-get-object-world-proto)"));
  INTR_ASSIGN_OR_RETURN(int64_t world_proto_id,
                        world_proto_id_val.GetInteger());
  if (world_proto_id == clips::ProtobufManager::kInvalidId.value()) {
    return absl::FailedPreconditionError(
        "Failed to download world from world service, misconfigured world "
        "ID?");
  }
  return absl::OkStatus();
}

absl::Status ValidateConfig(
    const intrinsic_proto::executive::ExecutiveConfig& config) {
  if (config.loop_time_ms() == 0) {
    return absl::InvalidArgumentError(
        "Loop time cannot be zero duration "
        "(forgot to use nanoseconds?)");
  }
  if (config.clips_run_limit() == 0) {
    return absl::InvalidArgumentError(
        "CLIPS run limit must be greater than 0.");
  }
  if (!config.has_grpc_client_connection_timeout()) {
    return absl::InvalidArgumentError(
        "grpc_client_connection_timeout has to be set.");
  }
  if (config.world_id().empty()) {
    return absl::InvalidArgumentError("world_id must be set.");
  }

  RE2 symbol_re(kSymbolRegex);
  for (const intrinsic_proto::executive::ExecutiveConfig::Flag& flag :
       config.flags()) {
    if (flag.name().empty()) {
      return absl::InvalidArgumentError("Flag name must not be empty");
    }
    if (!RE2::FullMatch(flag.name(), symbol_re)) {
      return absl::InvalidArgumentError("Flag name must be valid CLIPS symbol");
    }
    if (flag.value_case() ==
        intrinsic_proto::executive::ExecutiveConfig::Flag::VALUE_NOT_SET) {
      return absl::InvalidArgumentError("Flag must have a value field set");
    }
  }
  return absl::OkStatus();
}

absl::Status VerifyCompatibleState(
    const absl::StatusOr<intrinsic_proto::executive::RunMetadata::State>&
        operation_state,
    std::vector<intrinsic_proto::executive::RunMetadata::State>
        compatible_states) {
  if (!operation_state.ok()) {
    return operation_state.status();
  }
  intrinsic_proto::executive::RunMetadata::State state = *operation_state;

  // Check preconditions.
  if (std::find(compatible_states.begin(), compatible_states.end(), state) ==
      compatible_states.end()) {
    return absl::FailedPreconditionError(absl::StrFormat(
        "Incompatible state (%s not in {%s})",
        intrinsic_proto::executive::RunMetadata::State_Name(state),
        absl::StrJoin(
            compatible_states, ", ",
            [](std::string* out,
               const intrinsic_proto::executive::RunMetadata::State& vstate) {
              absl::StrAppend(
                  out,
                  intrinsic_proto::executive::RunMetadata::State_Name(vstate));
            })));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::unique_ptr<ClipsExecutor>> ClipsExecutor::Create(
    ClipsExecutorCreateOptions create_options) {
  INTR_RETURN_IF_ERROR(ValidateConfig(create_options.config));
  if (create_options.asset_info_internal_stub == nullptr) {
    return absl::InvalidArgumentError(
        "asset_info_internal_stub must not be null");
  }
  return absl::WrapUnique(new ClipsExecutor(std::move(create_options)));
}

ClipsExecutor::ClipsExecutor(ClipsExecutorCreateOptions create_options)
    : clips_(std::move(create_options.clips_env)),
      proto_mgr_(create_options.proto_mgr != nullptr
                     ? std::move(create_options.proto_mgr)
                     : clips::ProtobufManager::Create(clips_.get()).value()),
      cel_mgr_(
          clips::CelManager::Create(clips_.get(), proto_mgr_.get()).value()),
      span_mgr_(clips::TraceSpanManager::Create(clips_.get()).value()),
      blackboard_(*clips_, *proto_mgr_),
      skill_registry_client_(std::move(create_options.skill_registry_client)),
      resource_registry_client_(
          std::move(create_options.resource_registry_client)),
      installed_assets_stub_(std::move(create_options.installed_assets_stub)),
      code_execution_service_stub_(
          std::move(create_options.code_execution_service_stub)),
      world_service_stub_(std::move(create_options.world_service_stub)),
      object_world_service_stub_(
          std::move(create_options.object_world_service_stub)),
      world_updater_stub_(std::move(create_options.world_updater_stub)),
      world_compatibility_service_stub_(
          std::move(create_options.world_compatibility_service_stub)),
      simulation_service_stub_(
          std::move(create_options.simulation_service_stub)),
      clips_logger_(std::make_unique<ClipsLogger>(
          proto_mgr_.get(), object_world_service_stub_.get(),
          world_compatibility_service_stub_.get(), span_mgr_.get())),
      clips_world_(std::make_unique<ClipsWorld>(
          clips_->GetAssertFacade(), proto_mgr_.get(), span_mgr_.get(),
          object_world_service_stub_.get(), world_updater_stub_.get(),
          world_compatibility_service_stub_.get())),
      clips_pub_sub_(std::make_unique<ClipsPubSub>(proto_mgr_.get())),
      solution_service_stub_(std::move(create_options.solution_service_stub)),
      conductor_service_stub_(std::move(create_options.conductor_service_stub)),
      clips_conductor_client_(std::make_unique<ClipsConductorClient>(
          clips_->GetAssertFacade(), proto_mgr_.get(),
          conductor_service_stub_.get())),
      config_(create_options.config),
      loop_manually_(create_options.loop_manually) {
  skill_client_generator_ = std::make_unique<SkillClientGenerator>(
      skill_registry_client_.get(), resource_registry_client_.get(),
      ToAbslDurationNoValidation(config_.grpc_client_connection_timeout()),
      installed_assets_stub_.get(),
      std::move(create_options.asset_info_internal_stub), clips_.get(),
      proto_mgr_.get());
  skill_dispatcher_ = std::make_unique<ClipsSkillDispatcher>(
      clips_->GetAssertFacade(), proto_mgr_.get(), span_mgr_.get(),
      skill_client_generator_.get(), object_world_service_stub_.get(),
      simulation_service_stub_.get());
  code_execution_dispatcher_ = std::make_unique<CodeExecutionDispatcher>(
      clips_->GetAssertFacade(), proto_mgr_.get(), span_mgr_.get(),
      code_execution_service_stub_.get());
}

ClipsExecutor::ClipsExecutor()
    : clips_(std::make_unique<clips::Environment>()),
      proto_mgr_(clips::ProtobufManager::Create(clips_.get()).value()),
      cel_mgr_(
          clips::CelManager::Create(clips_.get(), proto_mgr_.get()).value()),
      span_mgr_(clips::TraceSpanManager::Create(clips_.get()).value()),
      blackboard_(*clips_, *proto_mgr_),
      clips_logger_(std::make_unique<ClipsLogger>(proto_mgr_.get(), nullptr,
                                                  nullptr, span_mgr_.get())),
      clips_world_(std::make_unique<ClipsWorld>(
          clips_->GetAssertFacade(), proto_mgr_.get(), span_mgr_.get(),
          /*object_stub=*/nullptr,
          /*world_updater_stub=*/nullptr, /*compatibility_stub=*/nullptr)),
      clips_pub_sub_(std::make_unique<ClipsPubSub>(proto_mgr_.get())) {
  skill_client_generator_ = std::make_unique<SkillClientGenerator>(
      nullptr, nullptr, std::nullopt, nullptr, nullptr, clips_.get(),
      proto_mgr_.get());
  skill_dispatcher_ = std::make_unique<ClipsSkillDispatcher>(
      clips_->GetAssertFacade(), proto_mgr_.get(), span_mgr_.get(),
      skill_client_generator_.get(),
      /*object_world_service_stub=*/nullptr, /*simulation_service=*/nullptr);
  code_execution_dispatcher_ = std::make_unique<CodeExecutionDispatcher>(
      clips_->GetAssertFacade(), proto_mgr_.get(), span_mgr_.get(), nullptr);
}

ClipsExecutor::~ClipsExecutor() {
  if (loop_manually_) {
    // For manual looping, tear down needs to happen from this thread.
    // Otherwise, TearDown needs to be called from thread_, see below.
    absl::MutexLock l(teardown_mutex_);
    if (auto s = TearDown(); !s.ok()) {
      LOG(ERROR) << "Error in TearDown for 'loop_manually': " << s;
    }
    return;
  }
  if (!thread_) {
    return;
  }

  absl::MutexLock l(teardown_mutex_);
  thread_->request_stop();
  // Trigger loop to allow the thread to stop
  clips_->NotifyRunner();
  if (!quit_) {
    terminate_cv_.Wait(&teardown_mutex_);
  }
  thread_->join();
  thread_.reset();
  proto_mgr_->RemoveAllProtos();
}

// Add functions to CLIPS environment. Requires access to this.
absl::Status ClipsExecutor::AddFunctions()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  clips_->GetFunctionFacade()->clips_mutex()->AssertHeld();
  INTR_RETURN_IF_ERROR(clips_->GetFunctionFacade()->AddFunction(
      "executive-quit", std::function<void()>([this]() { quit_ = true; })));
  INTR_RETURN_IF_ERROR(
      RegisterClipsCreateExtendedStatus(clips_.get(), proto_mgr_.get()));
  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitDispatchers()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  clips_->GetFunctionFacade()->clips_mutex()->AssertHeld();
  clips_->GetReadonlyFacade()->GetClipsMutex()->AssertHeld();
  INTR_RETURN_IF_ERROR(skill_dispatcher_->Init(clips_->GetFunctionFacade(),
                                               clips_->GetReadonlyFacade()));
  INTR_RETURN_IF_ERROR(code_execution_dispatcher_->Init(
      clips_->GetFunctionFacade(), clips_->GetReadonlyFacade()));
  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitSkillClientGenerator()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  clips_->GetFunctionFacade()->clips_mutex()->AssertHeld();
  clips_->GetReadonlyFacade()->GetClipsMutex()->AssertHeld();
  INTR_RETURN_IF_ERROR(skill_client_generator_->Init(
      clips_->GetFunctionFacade(), clips_->GetReadonlyFacade()));
  return absl::OkStatus();
}

absl::Status ClipsExecutor::SetWorldId(absl::string_view flag_name,
                                       absl::string_view world_id)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  // Remove existing flag and world to force a reconfiguration
  std::vector<clips::Fact> flag_facts =
      clips_->QueryFacts("flag", {{"name", clips::Symbol(flag_name)}});
  for (clips::Fact& fact : flag_facts) {
    clips_->RetractFact(std::move(fact)).IgnoreError();
  }
  std::vector<clips::Fact> world_facts = clips_->GetFacts("world");
  for (clips::Fact& fact : world_facts) {
    clips_->RetractFact(std::move(fact)).IgnoreError();
  }
  // Set new flag, on next loop will reconfigure
  return clips_
      ->AssertFact("flag", {{"name", clips::Symbol(flag_name)},
                            {"type", clips::Symbol("STRING")},
                            {"value", world_id}})
      .status();
}

absl::Status ClipsExecutor::InitWorld()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  clips_->GetFunctionFacade()->clips_mutex()->AssertHeld();
  INTR_RETURN_IF_ERROR(clips_world_->Init(clips_->GetFunctionFacade()));
  INTR_RETURN_IF_ERROR(SetWorldId(kWorldIdFlag, config_.world_id()));

  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitPubSub()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  clips_->GetFunctionFacade()->clips_mutex()->AssertHeld();
  INTR_RETURN_IF_ERROR(clips_pub_sub_->Init(clips_->GetFunctionFacade()));

  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitFlags()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  for (const intrinsic_proto::executive::ExecutiveConfig::Flag& flag :
       config_.flags()) {
    clips::Value value;
    clips::Symbol type;
    switch (flag.value_case()) {
      case intrinsic_proto::executive::ExecutiveConfig::Flag::kStringValue:
        value.Set(flag.string_value());
        type.Set("STRING");
        break;
      case intrinsic_proto::executive::ExecutiveConfig::Flag::kIntValue:
        value.Set(flag.int_value());
        type.Set("INTEGER");
        break;
      case intrinsic_proto::executive::ExecutiveConfig::Flag::kDoubleValue:
        value.Set(flag.double_value());
        type.Set("FLOAT");
        break;
      case intrinsic_proto::executive::ExecutiveConfig::Flag::kBoolValue:
        value =
            flag.bool_value() ? clips::Symbol::True() : clips::Symbol::False();
        type.Set("BOOL");
        break;
      case intrinsic_proto::executive::ExecutiveConfig::Flag::kSymbolValue:
        value.Set(flag.symbol_value(), clips::Value::Type::kSymbol);
        type.Set("SYMBOL");
        break;
      default:
        return absl::InvalidArgumentError("Unhandled flag value field");
    }
    INTR_RETURN_IF_ERROR(
        clips_
            ->AssertFact("flag", {{"name", clips::Symbol(flag.name())},
                                  {"type", type},
                                  {"value", value}})
            .status());
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitNoOpActions()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  for (const intrinsic_proto::executive::ExecutiveConfig::NoOpAction& noop :
       config_.noop_actions()) {
    INTR_RETURN_IF_ERROR(
        clips_
            ->AssertFact("noop-action-info",
                         {{"noop-action-names",
                           clips::Values{clips::Value(noop.name())}}})
            .status());
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::Init() {
  if (is_initialized_) {
    return absl::InternalError("ClipsExecutor can only be initialized once.");
  }

  if (!loop_manually_) {
    // Setup runner notification
    {
      absl::MutexLock lock(*clips_->mutex());
      INTR_RETURN_IF_ERROR(
          clips_->SetRunnerCallback([this] { loop_wait_cv_.SignalAll(); }));
    }

    // Run the thread and wait until the initialization is done.
    thread_ = std::make_unique<Thread>(
        [this](StopToken stop_token) { Run(stop_token); });

    {
      // Wait ensures that the thread completed the init and started to
      // continuously loop.
      absl::MutexLock lock(*clips_->mutex());
      WaitForLoop();
      INTR_RETURN_IF_ERROR(init_status_) << "Init failed.";
    }
  } else {
    LOG(WARNING) << "Manual executor looping mode entered. This should only be "
                    "done for debugging.";
    INTR_RETURN_IF_ERROR(InitExecutor());
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitClipsTracing()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  // check if we have a clips_trace_logfile from the config or a flag
  std::string clips_trace_logfile = absl::GetFlag(FLAGS_clips_trace_logfile);
  std::string clips_trace_logfile_config(config_.clips_trace_logfile());
  if (clips_trace_logfile.empty()) {
    clips_trace_logfile = clips_trace_logfile_config;
  } else if (!clips_trace_logfile_config.empty()) {
    // file given by two different sources
    LOG(WARNING) << "Clips Trace Logfile specified via config ("
                 << clips_trace_logfile_config << ") AND flag ("
                 << clips_trace_logfile << "). Will use flag, i.e., "
                 << clips_trace_logfile;
  }
  if (!clips_trace_logfile.empty()) {
    absl::StatusOr<std::string> trace_logfile =
        clips_->StartTraceFileLog(clips_trace_logfile);
    if (!trace_logfile.ok()) {
      LOG(WARNING) << "Failed to init trace log file: "
                   << trace_logfile.status();
    }
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitInternalLogger()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  if (absl::GetFlag(FLAGS_enable_internal_logging)) {
    INTR_RETURN_IF_ERROR(clips_->AddLogCallback(
        kInternalLogger,
        [](clips::Environment::LogLevel level, absl::string_view message) {
          switch (level) {
            case clips::Environment::LogLevel::kInternal:
              LOG(INFO) << message;
              break;
            default:
              break;  // ignore any other message
          }
        }));
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitGCloudProject()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  if (config_.google_cloud_project().empty()) {
    LOG(WARNING) << "No Google Cloud project defined";
    return absl::OkStatus();
  }
  return clips_
      ->AssertFact("flag", {{"name", clips::Symbol("google-cloud-project")},
                            {"type", clips::Symbol("STRING")},
                            {"value", config_.google_cloud_project()}})
      .status();
}

absl::Status ClipsExecutor::InitTracingAddTracingInfoToState()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  LOG(INFO) << (config_.tracing_add_info_to_state() ? "" : "Not ")
            << "Generating tracing infos.";
  return clips_
      ->AssertFact(
          "flag", {{"name", clips::Symbol("tracing-add-tracing-info-to-state")},
                   {"type", clips::Symbol("BOOL")},
                   {"value", config_.tracing_add_info_to_state()
                                 ? clips::Symbol::True()
                                 : clips::Symbol::False()}})
      .status();
}

absl::Status ClipsExecutor::InitTracingLinkOpencensus()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  return clips_
      ->AssertFact("flag", {{"name", clips::Symbol("tracing-link-opencensus")},
                            {"type", clips::Symbol("BOOL")},
                            {"value", config_.tracing_link_opencensus()
                                          ? clips::Symbol::True()
                                          : clips::Symbol::False()}})
      .status();
}

absl::Status ClipsExecutor::InitReportActiveActionsTimeout()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  absl::Duration report_active_actions_timeout =
      ToAbslDurationNoValidation(config_.report_active_actions_timeout());
  if (report_active_actions_timeout <= absl::Seconds(1)) {
    LOG(ERROR)
        << "Not setting report-active-actions-timeout as the given value of "
        << report_active_actions_timeout << " is too small (min 1s).";
    // It's OK that this is disabled, e.g., by passing 0.
    return absl::OkStatus();
  }
  return clips_
      ->AssertFact(
          "flag",
          {{"name", clips::Symbol("report-active-actions-timeout")},
           {"type", clips::Symbol("FLOAT")},
           {"value", absl::ToDoubleSeconds(report_active_actions_timeout)}})
      .status();
}

void ClipsExecutor::RetrieveSolutionInfoAndWarn() {
  if (solution_status_.has_value()) {
    return;
  }
  if (!solution_service_stub_) {
    LOG(WARNING) << "Not setting information from solution service as the "
                    "service is not configured or not available.";
    return;
  }

  intrinsic_proto::solution::v1::GetStatusRequest solution_status_request;
  intrinsic_proto::solution::v1::Status response;
  grpc::ClientContext grpc_context;
  intrinsic::ConfigureClientContext(&grpc_context);
  if (const absl::Status status =
          ToAbslStatus(solution_service_stub_->GetStatus(
              &grpc_context, solution_status_request, &response));
      !status.ok()) {
    LOG(WARNING) << "Unable to populate solution information: " << status;
    return;
  }

  solution_status_ = std::move(response);
}

absl::Status ClipsExecutor::InitConductor()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  clips_->GetFunctionFacade()->clips_mutex()->AssertHeld();
  INTR_ASSIGN_OR_RETURN(
      absl::Duration conductor_client_operation_poll_interval,
      ToAbslDuration(config_.conductor_client_operation_poll_interval()));
  INTR_RETURN_IF_ERROR(clips_conductor_client_->Init(
      clips_->GetFunctionFacade(), conductor_client_operation_poll_interval));
  return absl::OkStatus();
}

absl::Status ClipsExecutor::InitExecutor()
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  INTR_RETURN_IF_ERROR(ValidateConfig(config_));

  absl::MutexLock lock(*clips_->mutex());
  clips_->GetFunctionFacade()->clips_mutex()->AssertHeld();

  INTR_RETURN_IF_ERROR(clips_->AddDefaultLogCallback());

  INTR_RETURN_IF_ERROR(InitClipsTracing());
  INTR_RETURN_IF_ERROR(clips_->Watch(clips::Environment::WatchItem::kRules));
  INTR_RETURN_IF_ERROR(clips_->Watch(clips::Environment::WatchItem::kFacts));
  INTR_RETURN_IF_ERROR(clips_->SetRedefineDuringLoadIsError());
  INTR_RETURN_IF_ERROR(InitInternalLogger());
  INTR_RETURN_IF_ERROR(AddFunctions());
  INTR_RETURN_IF_ERROR(clips_logger_->Init(clips_->GetFunctionFacade()));
  INTR_RETURN_IF_ERROR(InitSkillClientGenerator());
  INTR_RETURN_IF_ERROR(DefaultClipsInitialize(
      clips_.get(), proto_mgr_.get(), span_mgr_.get(), config_.clips_dirs(0)));
  INTR_RETURN_IF_ERROR(SetSearchDirs(clips_.get(), config_.clips_dirs()));
  INTR_RETURN_IF_ERROR(InitWorld());
  INTR_RETURN_IF_ERROR(InitPubSub());
  INTR_RETURN_IF_ERROR(InitGCloudProject());
  INTR_RETURN_IF_ERROR(InitTracingAddTracingInfoToState());
  INTR_RETURN_IF_ERROR(InitTracingLinkOpencensus());
  INTR_RETURN_IF_ERROR(InitConductor());
  INTR_RETURN_IF_ERROR(InitDispatchers());
  INTR_RETURN_IF_ERROR(
      InitClipsBehaviorTreeSupport(clips_.get(), proto_mgr_.get()));
  INTR_RETURN_IF_ERROR(InitReportActiveActionsTimeout());
  INTR_RETURN_IF_ERROR(InitFlags());
  INTR_RETURN_IF_ERROR(LoadInitFiles(clips_.get(), config_.clips_init_files(),
                                     config_.clips_dirs()));
  INTR_RETURN_IF_ERROR(InitNoOpActions());
  INTR_RETURN_IF_ERROR(clips_->AssertFact("(executive-init)").status());
  RetrieveSolutionInfoAndWarn();

  // Run to trigger initialization rules
  clips_->RefreshAgenda();
  const int64_t invocations = clips_->Run(config_.clips_run_limit());
  RuleInvocationsMeasure().Record(
      invocations, opentelemetry::context::RuntimeContext::GetCurrent());
  if (invocations >= config_.clips_run_limit()) {
    return absl::InternalError("Initial rules caused loop");
  }

  INTR_RETURN_IF_ERROR(AssertNoFatalClipsErrors(clips_.get()));
  INTR_RETURN_IF_ERROR(AssertWorldDownloadSuccess(clips_.get()));

  quit_ = false;
  run_status_ = absl::OkStatus();
  is_initialized_ = true;
  LOG(INFO) << "ClipsExecutive successfully initialized.";
  return absl::OkStatus();
}

absl::Status ClipsExecutor::TearDown() {
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError,
                    code_execution_dispatcher_->TearDown());
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError, skill_dispatcher_->TearDown());
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError, clips_world_->TearDown());
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError, clips_pub_sub_->TearDown());
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError, clips_logger_->TearDown());
  INTR_LOG_IF_ERROR(absl::LogSeverity::kError,
                    clips_conductor_client_->TearDown());
  return absl::OkStatus();
}

absl::Status ClipsExecutor::RunStatus() {
  INTR_RETURN_IF_ERROR(init_status_) << "Error during Init.";
  if (quit_) {
    return absl::FailedPreconditionError("Executor looping is terminated.");
  }
  return run_status_;
}

std::shared_ptr<opentelemetry::trace::Span> ClipsExecutor::StartClipsRunSpan() {
  if (!span_mgr_) {
    return nullptr;
  }
  if (!config_.tracing_show_internal_calls()) {
    return nullptr;
  }

  absl::StatusOr<clips::TraceSpanReferenceId> status_or_span_id =
      GetExecutionSpanIdNoLock();
  // This should only fail when there is no operation-envelope fact, i.e.,
  // when there is not operation loaded.
  if (!status_or_span_id.ok()) {
    clips_runs_span_.reset();
    return nullptr;
  }
  clips::TraceSpanReferenceId execution_span_reference_id =
      status_or_span_id.value();

  // Handle the case that an execution_span was just started and this is the
  // first clips run. This clips run would then set the
  // execution_span_reference_id, so at this point it is still unset.
  // Ask the span manager if the last execution span that this executor started
  // still exists, then there is a current trace to capture.
  absl::StatusOr<opentelemetry::trace::SpanContext> last_execution_span_status =
      span_mgr_->GetSpanContext(last_execution_span_reference_id_);
  if (!last_execution_span_status.ok()) {
    last_execution_span_reference_id_ =
        clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  } else if (execution_span_reference_id ==
             clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    execution_span_reference_id = last_execution_span_reference_id_;
  }

  // first ensure that we have a parent span in clips_runs_span_.
  // Case 1: No execution span -> no current BT: stop clips_runs_span_
  // and do not start a clips run span.
  if (execution_span_reference_id ==
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    clips_runs_span_.reset();
    return nullptr;
  }
  // Case 2: Execution span changed, thus start a new clips_runs_span_
  // with the new one as parent.
  absl::StatusOr<std::string> status_or_current_execution_span_id =
      span_mgr_->GetSpanIdHex(execution_span_reference_id);
  if (!status_or_current_execution_span_id.ok()) {
    // should never happen
    LOG_EVERY_N_SEC(ERROR, 1)
        << "Could not get execution_span_id for span reference: "
        << execution_span_reference_id << " - "
        << status_or_current_execution_span_id.status();
    clips_runs_span_.reset();
    return nullptr;
  }
  const std::string& current_execution_span_id =
      status_or_current_execution_span_id.value();
  if (current_execution_span_id != clips_runs_span_parent_trace_id_) {
    // change of execution_span_id - reset the clips_runs_span.
    // The next block will create a new one
    clips_runs_span_.reset();
    clips_runs_span_parent_trace_id_ = "";
  }
  // Case 3: New execution span, start a new clips_runs_span_.
  if (!clips_runs_span_) {
    absl::StatusOr<std::shared_ptr<opentelemetry::trace::Span>>
        status_or_execution_span =
            span_mgr_->GetSpanForParenting(execution_span_reference_id);
    if (!status_or_execution_span.ok()) {
      return nullptr;
    }
    clips_runs_span_ = std::make_unique<intrinsic::stats::ScopedSpan>(
        "Clips Runs", *status_or_execution_span);
    clips_runs_span_parent_trace_id_ = current_execution_span_id;
  }

  if (!config_.tracing_show_internal_clips_runs_leaking_memory()) {
    // If disabled just don't create the Clips Run span.
    // Up to this point we are still handling the parent "Clips Runs" span
    // correctly and thus gRPC calls will all be captured by that one instead.
    return nullptr;
  }

  return stats::GetTracer()->StartSpan(
      "Clips Run", {.parent = clips_runs_span_->span()->GetContext()});
}

clips::TraceSpanReferenceId ClipsExecutor::SetClipsRunContext(
    std::shared_ptr<opentelemetry::trace::Span> run_span)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  if (!run_span) {
    return clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  }

  absl::StatusOr<clips::TraceSpanReferenceId> run_span_id =
      span_mgr_->AddSpan(std::move(run_span));
  if (!run_span_id.ok()) {
    LOG(ERROR) << "SetClipsRunContext could not add run span";
    return clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  }

  absl::StatusOr<clips::Fact> st =
      clips_->AssertFact("tracing-clips-run-context",
                         {{"span-reference-id", run_span_id->value()}});
  if (!st.ok()) {
    LOG_EVERY_N_SEC(ERROR, 1)
        << "SetClipsRunContext: asserting tracing-clips-run-context failed: "
        << st.status();
  }

  return *run_span_id;
}

void ClipsExecutor::RemoveClipsRunContext()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  absl::Status st = clips_->RetractFacts("tracing-clips-run-context", {});
  if (!st.ok()) {
    LOG(ERROR) << st;
  }
}

absl::Status ClipsExecutor::Loop() ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  if (thread_) {
    return absl::FailedPreconditionError(
        "The continuous runner thread was started, Loop() cannot be invoked "
        "manually. If you are running a test, you probably set "
        "loop_manually wrong.");
  }
  return InternalLoop();
}

absl::Status ClipsExecutor::InternalLoop()
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  if (!is_initialized_) {
    return absl::FailedPreconditionError("Must call Init() before Loop()");
  }
  if (!init_status_.ok()) return init_status_;

  std::shared_ptr<opentelemetry::trace::Span> run_span = StartClipsRunSpan();
  // run_span is started optionally, its lifetime is managed in the
  // TraceSpanManager, the Scope is only added to capture gRPC calls, but
  // does not end the run_span.
  clips::TraceSpanReferenceId run_span_id = SetClipsRunContext(run_span);
  std::unique_ptr<opentelemetry::trace::Scope> run_span_scope;
  if (run_span) {
    run_span_scope = std::make_unique<opentelemetry::trace::Scope>(run_span);
  }
  LOG_IF(WARNING, !clips::AssertTimeNow(clips_.get()).ok())
      << "Failed to assert time fact";

  const std::vector<clips::Fact> facts_before = clips_->GetFacts();
  clips_->RefreshAgenda();
  absl::Time clips_run_start = absl::Now();
  INTR_ASSIGN_OR_RETURN(clips::Trace trace,
                        clips_->RunWithTracing(config_.clips_run_limit()));
  absl::Time clips_run_end = absl::Now();
  ClipsRunTimeMeasure().Record(
      absl::ToDoubleMilliseconds(
          std::max(absl::ZeroDuration(), clips_run_end - clips_run_start)),
      opentelemetry::context::RuntimeContext::GetCurrent());

  RemoveClipsRunContext();

  size_t rules_fired = trace.CountRulesFired();
  RuleInvocationsMeasure().Record(
      rules_fired, opentelemetry::context::RuntimeContext::GetCurrent());

  if (rules_fired >= config_.clips_run_limit()) {
    LOG(ERROR) << "CLIPS run fired " << rules_fired
               << " rules (>= " << config_.clips_run_limit()
               << "), aborting process tree";
    LOG(ERROR) << "Full trace:";
    trace.PrintLineWise(absl::LogSeverity::kError, __FILE__, __LINE__);
    INTR_RETURN_IF_ERROR(
        clips_
            ->AssertFact("executive-force-cancel",
                         {{"message",
                           "Exceeded allowed rule activations per "
                           "internal update cycle. Is there an "
                           "infinite loop in the process?"}})
            .status());
    clips_->RefreshAgenda();
    int64_t recovery_rules_fired = clips_->Run(config_.clips_run_limit());
    if (recovery_rules_fired >= config_.clips_run_limit()) {
      LOG(ERROR) << "Recovering from non-stop loop failed";
      return absl::AbortedError("Failed to recover from non-stop loop");
    }
  }

  if (run_span) run_span->SetAttribute("rules_fired", rules_fired);
  run_span_scope.reset();
  if (rules_fired > 1 && span_mgr_) {
    // more than 1 rules (retract time) means 'something happened', thus record
    absl::Status st = span_mgr_->EndSpan(run_span_id);
    if (!st.ok()) {
      LOG(ERROR) << "Ending clips run span failed: " << st;
    }
  }
  // else: Leave run_span running, so it doesn't appear in the trace
  // If we do not do this high frequency (loop_time_ms) spans that do not
  // represent any relevant state changes would clutter the trace. This would
  // make it harder to find the spans that actually do represent a clips run to
  // look at.
  // Unfortunately the OpenTelemetry API does not have a way to 'undo' a Span
  // once started, without ending it, but destroying the Span object potentially
  // leaks memory.

  run_span.reset();

  // Signal threads that are waiting in WaitForLoop()
  loop_cv_.SignalAll();

  INTR_ASSIGN_OR_RETURN(std::vector<std::string> operations,
                        GetOperationNames());
  for (const std::string& op_name : operations) {
    INTR_RETURN_IF_ERROR(
        RecordOperationExtendedStatus(op_name, facts_before, trace));
  }

  return absl::OkStatus();
}

absl::Status ClipsExecutor::RecordOperationExtendedStatus(
    absl::string_view operation_name,
    const std::vector<clips::Fact>& facts_before, const clips::Trace& trace)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::executive::RunMetadata::State state,
                        GetOperationStateNoLock(operation_name));

  if (state != intrinsic_proto::executive::RunMetadata::FAILED) {
    return absl::OkStatus();
  }
  if (operation_extended_status_with_legacy_errors_.contains(operation_name)) {
    // has already been logged
    return absl::OkStatus();
  }

  std::optional<intrinsic_proto::status::ExtendedStatus>
      operation_extended_status_full;

  // On FAILED it is expected that there is an ExtendedStatus on the operation.
  // If not generate a generic failure.
  // In either case still add additional information to the full extended status
  // stored in operation_extended_status_with_legacy_errors_ and log and record
  // that.
  absl::StatusOr<intrinsic_proto::status::ExtendedStatus> operation_es =
      BuildOperationExtendedStatusWithLegacyErrors(
          clips_.get(), proto_mgr_.get(), operation_name);
  if (operation_es.ok()) {
    operation_extended_status_full.emplace(std::move(*operation_es));
  } else {
    LOG(ERROR) << "Failed to get operation extended status: "
               << operation_es.status();
    intrinsic_proto::status::ExtendedStatus op_es = CreateExtendedStatus(
        13000, "Failed to get additional error information.");
    op_es.mutable_debug_report()->set_message(operation_es.status().ToString());
    AddExtendedStatusLegacyErrors(clips_.get(), op_es);
    operation_extended_status_full.emplace(op_es);
  }

  AddExtendedStatusDebugInformation(*operation_extended_status_full,
                                    facts_before, trace);
  absl::Status log_status = LogExecutiveExtendedStatus(
      *operation_extended_status_full, operation_name);
  if (!log_status.ok()) {
    LOG(WARNING) << "Failed to log ExtendedStatus: " << log_status;
  }
  operation_extended_status_with_legacy_errors_[operation_name] =
      std::move(*operation_extended_status_full);

  if (quit_) {
    return absl::AbortedError(absl::StrFormat(
        "Execution has failed. \nFACTS (before loop):%s\n\nFACTS (after "
        "loop):\n%s\n\nTRACE: %s\n\nExtended Status:\n%s",
        absl::StrJoin(facts_before, "\n",
                      [](std::string* out, const clips::Fact& f) {
                        absl::StrAppend(
                            out, f.DebugString(/*include_identifier=*/true));
                      }),
        absl::StrJoin(clips_->GetFactsAsStrings(/*template_name=*/"",
                                                /*include_identifier=*/true),
                      "\n"),
        trace.ToString(),
        absl::StrCat(
            operation_extended_status_with_legacy_errors_[operation_name])));
  }

  return absl::OkStatus();
}

void ClipsExecutor::AddExtendedStatusDebugInformation(
    intrinsic_proto::status::ExtendedStatus& es,
    const std::vector<clips::Fact>& facts_before, const clips::Trace& trace)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  if (config_.google_cloud_project() != kIntrinsicInternalProject) {
    return;
  }
  std::stringstream report_stream;
  if (absl::GetFlag(FLAGS_enable_detailed_extended_status_debug)) {
    // Internally, we can add all information
    const std::vector<std::string> facts_after_str = clips_->GetFactsAsStrings(
        /*template_name=*/"", /*include_identifier=*/true);
    std::vector<std::string> facts_before_str;
    facts_before_str.reserve(facts_before.size());
    absl::c_transform(facts_before, std::back_inserter(facts_before_str),
                      [](const clips::Fact& f) {
                        return f.DebugString(/*include_identifier=*/true);
                      });

    report_stream << "Facts before run:" << std::endl
                  << absl::StrJoin(facts_before_str, "\n") << std::endl
                  << std::endl
                  << "Trace:" << std::endl
                  << trace << std::endl
                  << std::endl
                  << "Facts after run:" << std::endl
                  << absl::StrJoin(facts_after_str, "\n");
  } else {
    // Internally, by default add information about the operation-envelope
    // fact only
    const std::vector<std::string> facts_after_str = clips_->GetFactsAsStrings(
        /*template_name=*/"operation-envelope",
        /*include_identifier=*/true);
    std::vector<std::string> facts_before_str;
    for (const clips::Fact& f : facts_before) {
      if (f.GetTemplateName() != "operation-envelope") {
        continue;
      }
      facts_before_str.push_back(f.DebugString(/*include_identifier=*/true));
    }
    report_stream << "Facts before run:" << std::endl
                  << absl::StrJoin(facts_before_str, "\n") << std::endl
                  << std::endl
                  << "Facts after run:" << std::endl
                  << absl::StrJoin(facts_after_str, "\n");
  }

  es.mutable_debug_report()->mutable_message()->append(report_stream.str());
}

absl::Status ClipsExecutor::LogExecutiveExtendedStatus(
    const intrinsic_proto::status::ExtendedStatus& es,
    std::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  // TODO(b/201264807): Avoid blocking main thread.
  return data_logger::LogAndAwaitResponse(
      data_logger::Builder::From(es)
          .WithContext(GetStateLogContextNoLock(operation_name))
          .Item());
}

absl::StatusOr<clips::TraceSpanReferenceId>
ClipsExecutor::GetExecutionSpanIdNoLock()
    ABSL_ASSERT_EXCLUSIVE_LOCK(clips_->mutex()) {
  clips_->mutex()->AssertHeld();
  INTR_ASSIGN_OR_RETURN(std::vector<std::string> operation_names,
                        GetOperationNames());
  if (operation_names.empty()) {
    return clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  }
  // TODO(b/319835803) Decide if or how to handle this for multiple operations.
  // Also affects StartClipsRunSpan and possible SetClipsRunContext + its usage
  // in InternalLoop.
  // Technically CLIPS runs are attached to the "executive", as CLIPS
  // runs/handles all operations, but the executive does not have a trace, only
  // operations have. Although it is possible to create an executive span, the
  // resulting CLIPS runs spans would be a lot less useful as they have no
  // relation to an operation.
  INTR_ASSIGN_OR_RETURN(clips::Symbol tree_id,
                        GetProcessTreeId(operation_names.front()));
  INTR_ASSIGN_OR_RETURN(clips::Value span_value,
                        clips_->EvaluateExpectSingleReturn(absl::StrFormat(
                            "(operation-get-execution-span-by-tree \"%s\")",
                            tree_id.ToString())));
  INTR_ASSIGN_OR_RETURN(int64_t span_id_int, span_value.GetInteger());
  clips::TraceSpanReferenceId execution_span_id{span_id_int};
  return execution_span_id;
}

intrinsic_proto::data_logger::Context ClipsExecutor::GetStateLogContextNoLock(
    std::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Value proto_id_val,
      clips_->EvaluateExpectSingleReturn(absl::StrFormat(
          "(executive-state-create-context-proto \"%s\")", operation_name)),
      _.LogWarning().With(Return(intrinsic_proto::data_logger::Context())));
  INTR_ASSIGN_OR_RETURN(
      int64_t proto_id, proto_id_val.GetInteger(),
      _.LogWarning().With(Return(intrinsic_proto::data_logger::Context())));
  clips::ScopedProto scope_context_proto(clips::ProtoMessageId(proto_id),
                                         proto_mgr_.get());
  INTR_ASSIGN_OR_RETURN(
      auto proto,
      proto_mgr_->GetProtoAs<intrinsic_proto::data_logger::Context>(
          clips::ProtoMessageId(proto_id)),
      _.LogWarning().With(Return(intrinsic_proto::data_logger::Context())));
  return *proto;
}

intrinsic_proto::data_logger::Context ClipsExecutor::GetStateLogContext(
    std::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  return GetStateLogContextNoLock(operation_name);
}

absl::StatusOr<google::protobuf::Any> ClipsExecutor::GetBlackboardValue(
    absl::string_view key, absl::string_view scope,
    absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  return blackboard_.GetBlackboardValue(key, scope, operation_name);
}

absl::Status ClipsExecutor::UpdateBlackboardValue(
    absl::string_view key, absl::string_view scope,
    absl::string_view operation_name, const google::protobuf::Any& value)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  return blackboard_.UpdateBlackboardValue(key, scope, operation_name, value);
}

absl::StatusOr<std::vector<intrinsic_proto::executive::BlackboardValue>>
ClipsExecutor::ListBlackboardValues(
    absl::string_view request_scope, absl::string_view operation_name,
    intrinsic_proto::executive::ListBlackboardValuesRequest::View view)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  return blackboard_.ListBlackboardValues(request_scope, operation_name, view);
}

absl::Status ClipsExecutor::DeleteBlackboardValue(
    absl::string_view key, absl::string_view scope,
    absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  return blackboard_.DeleteBlackboardValue(key, scope, operation_name);
}

absl::StatusOr<intrinsic_proto::executive::BlackboardSnapshot>
ClipsExecutor::CreateBlackboardSnapshot(
    absl::string_view operation_name, absl::string_view display_name,
    intrinsic_proto::executive::BlackboardSnapshot::SnapshotSource
        snapshot_source) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  return blackboard_.CreateSnapshot(operation_name, display_name,
                                    snapshot_source);
}

absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
ClipsExecutor::LoadBlackboardSnapshot(const Blackboard::SnapshotHandle& handle,
                                      absl::string_view target_operation_name)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  return blackboard_.LoadSnapshot(handle, target_operation_name);
}

std::pair<std::vector<intrinsic_proto::executive::BlackboardSnapshot>, int>
ClipsExecutor::ListBlackboardSnapshots(int page_size, int page_offset)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  return blackboard_.ListSnapshots(page_size, page_offset);
}

absl::Status ClipsExecutor::DeleteBlackboardSnapshot(
    const Blackboard::SnapshotHandle& handle)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  return blackboard_.DeleteSnapshot(handle);
}

absl::StatusOr<std::vector<std::string>> ClipsExecutor::GetOperationNames()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  std::vector<clips::Fact> operation_facts =
      clips_->GetFacts("operation-envelope");
  std::vector<std::string> operation_names;
  for (const clips::Fact& operation_fact : operation_facts) {
    INTR_ASSIGN_OR_RETURN(clips::Value operation_name_val,
                          operation_fact.GetSlotValue("name"));
    INTR_ASSIGN_OR_RETURN(std::string operation_name,
                          operation_name_val.GetString());
    operation_names.push_back(operation_name);
  }
  return operation_names;
}

absl::StatusOr<clips::Symbol> ClipsExecutor::GetProcessTreeId(
    absl::string_view operation_name)
    ABSL_ASSERT_EXCLUSIVE_LOCK(clips_->mutex()) {
  clips_->mutex()->AssertHeld();
  INTR_ASSIGN_OR_RETURN(
      clips::Fact operation_fact,
      clips_->GetUniqueFact("operation-envelope", {{"name", operation_name}}),
      _ << "Is the executive not initialized?");
  INTR_ASSIGN_OR_RETURN(clips::Value process_tree_id_val,
                        operation_fact.GetSlotValue("operation-tree-id"));
  INTR_ASSIGN_OR_RETURN(clips::Symbol process_tree_id,
                        process_tree_id_val.GetSymbol());
  if (process_tree_id == clips::Symbol::Nil()) {
    return absl::NotFoundError(
        "Cannot determine the process tree id. Is no process tree loaded?");
  }
  return process_tree_id;
}

absl::StatusOr<BehaviorTree> ClipsExecutor::GetProcessTree(
    absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  return GetProcessTreeNoLock(operation_name);
}

absl::StatusOr<BehaviorTree> ClipsExecutor::GetProcessTreeNoLock(
    absl::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact operation_fact,
      clips_->GetUniqueFact("operation-envelope", {{"name", operation_name}}));

  INTR_ASSIGN_OR_RETURN(clips::Value run_metadata_id_val,
                        operation_fact.GetSlotValue("run-metadata-proto"));
  INTR_ASSIGN_OR_RETURN(int64_t run_metadata_id,
                        run_metadata_id_val.GetInteger());

  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic_proto::executive::RunMetadata> run_metadata,
      proto_mgr_->GetProtoAs<intrinsic_proto::executive::RunMetadata>(
          clips::ProtoMessageId(run_metadata_id)));

  if (!run_metadata->has_behavior_tree()) {
    return absl::NotFoundError(
        absl::StrCat("No behavior tree for operation: ", operation_name));
  }

  return run_metadata->behavior_tree();
}

absl::StatusOr<std::string> ClipsExecutor::GetProcessTreeScope(
    absl::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());

  INTR_ASSIGN_OR_RETURN(clips::Symbol process_tree_id,
                        GetProcessTreeId(operation_name));

  INTR_ASSIGN_OR_RETURN(
      clips::Fact process_tree_fact,
      clips_->GetUniqueFact("behavior-tree", {{"id", process_tree_id}}));
  INTR_ASSIGN_OR_RETURN(clips::Value process_tree_scope_val,
                        process_tree_fact.GetSlotValue("blackboard-scope"));
  INTR_ASSIGN_OR_RETURN(std::string process_tree_scope,
                        process_tree_scope_val.GetString());

  return process_tree_scope;
}

absl::StatusOr<intrinsic_proto::executive::RunMetadata::State>
ClipsExecutor::GetOperationStateNoLock(absl::string_view operation_name)
    ABSL_ASSERT_EXCLUSIVE_LOCK(clips_->mutex()) {
  clips_->mutex()->AssertHeld();

  INTR_ASSIGN_OR_RETURN(
      clips::Fact operation_fact,
      clips_->GetUniqueFact("operation-envelope", {{"name", operation_name}}),
      _.AttachExtendedStatus(CreateExtendedStatus(
          12800,
          absl::StrFormat("Operation %s does not exist", operation_name))));

  // operation-envelope has only a Symbol for the state. Create a dummy
  // RunMetadata proto and assign the state there to get an actual enum value.
  INTR_ASSIGN_OR_RETURN(
      clips::ProtoMessageId metadata_proto_id,
      proto_mgr_->CreateBuiltInProto("intrinsic_proto.executive.RunMetadata"),
      _.LogError());
  clips::ScopedProto scoped_metadata_proto(metadata_proto_id, proto_mgr_.get());
  INTR_ASSIGN_OR_RETURN(clips::Value state_val,
                        operation_fact.GetSlotValue("state"));
  INTR_RETURN_IF_ERROR(proto_mgr_->SetFieldValue(metadata_proto_id,
                                                 "operation_state", state_val));
  INTR_ASSIGN_OR_RETURN(
      std::unique_ptr<intrinsic_proto::executive::RunMetadata> metadata_ptr,
      proto_mgr_->GetProtoAs<intrinsic_proto::executive::RunMetadata>(
          metadata_proto_id));

  intrinsic_proto::executive::RunMetadata::State operation_state =
      metadata_ptr->operation_state();

  return operation_state;
}

absl::StatusOr<intrinsic_proto::executive::RunMetadata::State>
ClipsExecutor::GetOperationState(absl::string_view operation_name)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  return GetOperationStateNoLock(operation_name);
}

absl::StatusOr<google::protobuf::Any> ClipsExecutor::GetOperationReturnValue(
    absl::string_view operation_name) const {
  absl::MutexLock lock(*clips_->mutex());
  INTR_ASSIGN_OR_RETURN(
      clips::Fact operation_fact,
      clips_->GetUniqueFact("operation-envelope", {{"name", operation_name}}));
  INTR_ASSIGN_OR_RETURN(clips::Value return_proto_id_val,
                        operation_fact.GetSlotValue("return-value-proto"));
  INTR_ASSIGN_OR_RETURN(int64_t return_proto_id,
                        return_proto_id_val.GetInteger());
  INTR_ASSIGN_OR_RETURN(
      google::protobuf::Any return_any,
      proto_mgr_->CastToAny(clips::ProtoMessageId(return_proto_id)));
  return return_any;
}

absl::StatusOr<intrinsic_proto::status::ExtendedStatus>
ClipsExecutor::GetOperationExtendedStatusWithLegacyErrors(
    absl::string_view operation_name) const
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  if (operation_extended_status_with_legacy_errors_.contains(operation_name)) {
    return operation_extended_status_with_legacy_errors_.at(operation_name);
  }
  return absl::NotFoundError(absl::StrFormat(
      "Operation name %s has not reported an error or does not exist",
      operation_name));
}

absl::Status ClipsExecutor::CreateBreakpoint(
    const std::string& operation_name, const std::string& tree_id,
    uint32_t node_id, BehaviorTree::Breakpoint::Type breakpoint_type)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  clips::Symbol breakpoint_type_sym;
  switch (breakpoint_type) {
    case BehaviorTree::Breakpoint::BEFORE:
      breakpoint_type_sym = clips::Symbol("BEFORE");
      break;
    case BehaviorTree::Breakpoint::AFTER:
      breakpoint_type_sym = clips::Symbol("AFTER");
      break;
    default:
      return absl::InvalidArgumentError("Invalid breakpoint type");
  }

  absl::MutexLock lock(*clips_->mutex());
  INTR_ASSIGN_OR_RETURN(clips::Fact node_fact,
                        GetNodeFact(operation_name, tree_id, node_id));
  INTR_ASSIGN_OR_RETURN(clips::Value current_breakpoint,
                        node_fact.GetSlotValue("breakpoint-type"));
  if (current_breakpoint != clips::Symbol("NONE")) {
    return CreateStatus(
        12903,
        absl::StrFormat("Node %s:%d already has breakpoint of type %s", tree_id,
                        node_id, current_breakpoint.ToString()),
        absl::StatusCode::kAlreadyExists);
  }
  INTR_RETURN_IF_ERROR(clips_
                           ->AssertFact("behavior-tree-breakpoint-update",
                                        {{"operation-name", operation_name},
                                         {"op", clips::Symbol("ADD")},
                                         {"tree-id", clips::Symbol(tree_id)},
                                         {"node-id", node_id},
                                         {"add-type", breakpoint_type_sym}})
                           .status());
  return absl::OkStatus();
}

absl::Status ClipsExecutor::DeleteBreakpoint(const std::string& operation_name,
                                             const std::string& tree_id,
                                             uint32_t node_id)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(GetNodeFact(operation_name, tree_id, node_id).status());
  INTR_RETURN_IF_ERROR(clips_
                           ->AssertFact("behavior-tree-breakpoint-update",
                                        {{"operation-name", operation_name},
                                         {"op", clips::Symbol("REMOVE")},
                                         {"tree-id", clips::Symbol(tree_id)},
                                         {"node-id", node_id}})
                           .status());
  return absl::OkStatus();
}

absl::Status ClipsExecutor::DeleteAllBreakpoints(
    const std::string& operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(clips_
                           ->AssertFact("behavior-tree-breakpoint-update",
                                        {{"operation-name", operation_name},
                                         {"op", clips::Symbol("CLEAR")},
                                         {"tree-id", clips::Symbol::Nil()},
                                         {"node-id", 0}})
                           .status());
  return absl::OkStatus();
}

absl::StatusOr<std::vector<BehaviorTree::Breakpoint>>
ClipsExecutor::ListBreakpoints(const std::string& operation_name)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  std::vector<clips::Fact> breakpoint_facts = clips_->QueryFacts(
      "behavior-tree-node",
      {{"breakpoint-type", clips::Symbol("NONE"), /*negated=*/true}});
  std::vector<BehaviorTree::Breakpoint> breakpoints;
  breakpoints.reserve(breakpoint_facts.size());

  // TODO(b/319835803): List only breakpoints in the current operation.
  // Currently lists all breakpoints in all operations and system tree.
  for (const clips::Fact& fact : breakpoint_facts) {
    INTR_ASSIGN_OR_RETURN(clips::Value tree_id_slot,
                          fact.GetSlotValue("tree-id"));
    INTR_ASSIGN_OR_RETURN(clips::Value node_id_slot, fact.GetSlotValue("id"));
    INTR_ASSIGN_OR_RETURN(clips::Value type_slot,
                          fact.GetSlotValue("breakpoint-type"));
    INTR_ASSIGN_OR_RETURN(std::string tree_id,
                          tree_id_slot.GetSymbolAsString());
    INTR_ASSIGN_OR_RETURN(int64_t node_id, node_id_slot.GetInteger());
    INTR_ASSIGN_OR_RETURN(std::string type_sym, type_slot.GetSymbolAsString());
    if (node_id < 0 || node_id > std::numeric_limits<uint32_t>::max()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Tree %s has node ID %i out of uint32 limits", tree_id, node_id));
    }
    BehaviorTree::Breakpoint breakpoint;
    breakpoint.set_tree_id(tree_id);
    breakpoint.set_node_id(node_id);
    if (type_sym == "BEFORE") {
      breakpoint.set_type(BehaviorTree::Breakpoint::BEFORE);
    } else if (type_sym == "AFTER") {
      breakpoint.set_type(BehaviorTree::Breakpoint::AFTER);
    }
    breakpoints.push_back(breakpoint);
  }
  return breakpoints;
}

absl::StatusOr<clips::Fact> ClipsExecutor::GetNodeFact(
    absl::string_view operation_name, absl::string_view tree_id,
    uint32_t node_id) {
  auto create_extended_status = [operation_name, tree_id,
                                 node_id](absl::string_view error_message)
      -> intrinsic_proto::status::ExtendedStatus {
    return CreateExtendedStatus(
        12901, error_message,
        {.debug_message = absl::StrFormat(
             "Inputs: operation_name: %s tree id: %s node id: %d",
             operation_name, tree_id, node_id),
         .severity = intrinsic_proto::status::ExtendedStatus::ERROR});
  };
  auto invalid_argument_with_extended_status =
      [&create_extended_status](
          absl::string_view error_message) -> absl::Status {
    intrinsic::StatusBuilder status =
        intrinsic::StatusBuilder(absl::InvalidArgumentError(error_message))
            .AttachExtendedStatus(create_extended_status(error_message));
    return status;
  };

  if (tree_id.empty()) {
    return invalid_argument_with_extended_status(
        "The tree id must not be empty.");
  }

  // Make sure the requested 'tree_id' exists
  INTR_RETURN_IF_ERROR(
      clips_->GetUniqueFact("behavior-tree", {{"id", clips::Symbol(tree_id)}})
          .status())
      .AttachExtendedStatus(create_extended_status(
          absl::StrFormat("The tree id '%s' does not exist.", tree_id)));

  // Find the operation and make sure the 'tree_id' is in the operation's
  // process tree
  if (!operation_name.empty()) {
    // Note: We must always do the check that the given tree_id is valid/exists
    // before this to prevent injecting arbitrary user inputs into the Evaluate
    // call.
    INTR_ASSIGN_OR_RETURN(
        clips::Value toplevel_tree_id,
        clips_->EvaluateExpectSingleReturn(absl::StrFormat(
            "(behavior-tree-get-top-level-tree-id %s)", tree_id)));
    if (toplevel_tree_id == clips::Symbol("INVALID-TREE-ID")) {
      return invalid_argument_with_extended_status(absl::StrFormat(
          "The specified tree id '%s' does not exist.", tree_id));
    }

    INTR_ASSIGN_OR_RETURN(
        clips::Fact operation_fact,
        clips_->GetUniqueFact("operation-envelope", {{"name", operation_name}}),
        _.AttachExtendedStatus(create_extended_status(absl::StrFormat(
            "The operation '%s' does not exist", operation_name))));
    INTR_ASSIGN_OR_RETURN(clips::Value operation_tree_id_val,
                          operation_fact.GetSlotValue("operation-tree-id"));
    INTR_ASSIGN_OR_RETURN(clips::Symbol operation_tree_id,
                          operation_tree_id_val.GetSymbol());
    if (operation_tree_id == clips::Symbol::Nil()) {
      return invalid_argument_with_extended_status(absl::StrFormat(
          "The operation %s does not have a process tree", operation_name));
    }

    if (toplevel_tree_id != operation_tree_id) {
      return invalid_argument_with_extended_status(absl::StrFormat(
          "The tree id %s is not a tree in the given operation %s", tree_id,
          operation_name));
    }
  }

  // Make sure the requested 'node_id' exists in the given 'tree_id'
  INTR_ASSIGN_OR_RETURN(
      clips::Fact node_fact,
      clips_->GetUniqueFact(
          "behavior-tree-node",
          {{"id", node_id}, {"tree-id", clips::Symbol(tree_id)}}),
      _.AttachExtendedStatus(create_extended_status(
          absl::StrFormat("The node with id %d does not exist in tree %s.",
                          node_id, tree_id))));

  return node_fact;
}

absl::Status ClipsExecutor::SetNodeExecutionSettings(
    absl::string_view operation_name, absl::string_view tree_id,
    uint32_t node_id,
    const intrinsic_proto::executive::BehaviorTree::Node::ExecutionSettings&
        execution_settings) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::ACCEPTED,
       intrinsic_proto::executive::RunMetadata::SUSPENDED}))
      << "Cannot set node execution mode in current state";

  clips::Symbol execution_mode("NORMAL");
  clips::Symbol execution_mode_result_state("AUTO");

  switch (execution_settings.mode()) {
    case BehaviorTree::Node::ExecutionSettings::UNSPECIFIED:
      ABSL_FALLTHROUGH_INTENDED;
    case BehaviorTree::Node::ExecutionSettings::NORMAL:
      execution_mode = clips::Symbol("NORMAL");
      break;
    case BehaviorTree::Node::ExecutionSettings::DISABLED:
      execution_mode = clips::Symbol("DISABLED");
      break;
    default:
      return absl::InvalidArgumentError(
          absl::StrFormat("Cannot set node execution mode: %s",
                          BehaviorTree::Node::ExecutionSettings::Mode_Name(
                              execution_settings.mode())));
      break;
  }
  if (execution_settings.has_disabled_result_state()) {
    switch (execution_settings.disabled_result_state()) {
      case BehaviorTree::Node::ExecutionSettings::SUCCEEDED:
        execution_mode_result_state = clips::Symbol("SUCCEEDED");
        break;
      case BehaviorTree::Node::ExecutionSettings::FAILED:
        execution_mode_result_state = clips::Symbol("FAILED");
        break;
      default: {
        std::string message = absl::StrFormat(
            "Cannot set node execution result state: %s - "
            "only SUCCEEDED or FAILED are valid.",
            BehaviorTree::Node::ExecutionSettings::DisabledResultState_Name(
                execution_settings.disabled_result_state()));
        return intrinsic::StatusBuilder(absl::InvalidArgumentError(message))
            .AttachExtendedStatus(CreateExtendedStatus(12902, message));
      } break;
    }
  }

  INTR_ASSIGN_OR_RETURN(clips::Fact node_fact,
                        GetNodeFact(operation_name, tree_id, node_id));
  INTR_ASSIGN_OR_RETURN(clips::Value node_state_val,
                        node_fact.GetSlotValue("state"));
  if (node_state_val == clips::Symbol("SUSPENDED")) {
    return absl::FailedPreconditionError(
        "Cannot set node execution as the given node is suspended. Changing "
        "the node execution is only allowed for nodes that either have "
        "not been executed yet or have finished execution.");
  }

  INTR_RETURN_IF_ERROR(
      clips_
          ->AssertFact(
              "behavior-tree-selective-execution-settings-update",
              {{"operation-name", operation_name},
               {"tree-id", clips::Symbol(tree_id)},
               {"node-id", node_id},
               {"execution-mode", execution_mode},
               {"execution-mode-result-state", execution_mode_result_state}})
          .status());

  return absl::OkStatus();
}

// Calls `Conductor::PrepareProcessStart` without holding the clips mutex to
// avoid blocking the runloop. In order to avoid calling `PrepareProcessStart`
// twice for the same operation while the operation is in ACCEPTED state, the
// clips env is first checked for an existing
// conductor-preparation-client-operation fact. If this fact exists, an error is
// returned. If not, a placeholder fact is asserted and then the
// `PrepareProcessStart` RPC is called. If the RPC fails, the placeholder fact
// is retracted and the error is returned. If the RPC succeeds, the placeholder
// fact is modified with the conductor operation name to track the progress of
// the preparation.
absl::StatusOr<std::string> ClipsExecutor::CallConductorPrepareProcessStart(
    absl::string_view operation_name, absl::string_view scene_id,
    clips::TraceSpanReferenceId execution_span_id)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  clips::TraceSpanReferenceId preparation_span_id =
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId;
  if (execution_span_id !=
      clips::TraceSpanManager::kInvalidTraceSpanReferenceId) {
    INTR_ASSIGN_OR_RETURN(
        std::shared_ptr<opentelemetry::trace::Span> execution_span,
        span_mgr_->GetSpanForParenting(execution_span_id));
    std::shared_ptr<opentelemetry::trace::Span> preparation_span =
        stats::GetTracer()->StartSpan("Wait for conductor preparations",
                                      {.parent = execution_span->GetContext()});

    INTR_ASSIGN_OR_RETURN(preparation_span_id,
                          span_mgr_->AddSpan(preparation_span));
  }

  // If preparation_span_id is kInvalidTraceSpanReferenceId, this span will
  // never be logged.
  clips::TraceSpanManager::ScopedSpan preparation_span{
      preparation_span_id, span_mgr_.get(),
      opentelemetry::trace::StatusCode::kError,
      "Failed to prepare for process start"};

  {
    absl::MutexLock lock(*clips_->mutex());
    INTR_RETURN_IF_ERROR(RunStatus());

    INTR_RETURN_IF_ERROR(VerifyCompatibleState(
        GetOperationStateNoLock(operation_name),
        {intrinsic_proto::executive::RunMetadata::ACCEPTED}))
        << "Cannot prepare to start execution in current state";

    if (auto conductor_preparation_facts =
            clips_->QueryFacts("conductor-preparation-client-operation",
                               {{"operation-name", operation_name}});
        !conductor_preparation_facts.empty()) {
      return absl::FailedPreconditionError(
          "A previous request to start this operation is still pending.");
    }
    // Return kInternal error if fact cannot be asserted since we already
    // verified that a fact with the same type and slot values does not exist.
    INTR_RETURN_IF_ERROR(
        clips_
            ->AssertFact("conductor-preparation-client-operation",
                         {{"operation-name", operation_name}})
            .status())
        .LogError()
        .SetCode(absl::StatusCode::kInternal);
  }

  absl::StatusOr<ClipsConductorClient::ConductorClientOperationInfo>
      rpc_result = clips_conductor_client_->PrepareConductorProcessStart(
          operation_name, scene_id,
          /*all_executive_operations=*/{});

  {
    absl::MutexLock lock(*clips_->mutex());
    // Return kInternal error if a unique conductor-preparation-client-operation
    // fact cannot be found for this operation, since we asserted one earlier.
    INTR_ASSIGN_OR_RETURN(
        clips::Fact conductor_preparation_fact,
        clips_->GetUniqueFact("conductor-preparation-client-operation",
                              {{"operation-name", operation_name}}),
        _.LogError().SetCode(absl::StatusCode::kInternal));

    if (!rpc_result.ok()) {
      INTR_RET_CHECK_OK(
          clips_->RetractFact(std::move(conductor_preparation_fact)));
      return rpc_result.status();
    }

    INTR_RET_CHECK_OK(clips_->ModifyFact(
        std::move(conductor_preparation_fact),
        {{"conductor-operation-name", rpc_result->name},
         {"is-done",
          rpc_result->is_done ? clips::Symbol::True() : clips::Symbol::False()},
         {"span-reference-id", preparation_span_id.value()},
         {"scene-id", scene_id}}));
  }

  // Release preparation span since it will be ended in CLIPS.
  preparation_span.Release();
  return rpc_result->name;
}

absl::Status ClipsExecutor::RequestToStart(absl::string_view operation_name,
                                           const StartOptions& options)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  // `Conductor::PrepareProcessStart` RPC must be done synchronously here
  // since the conductor snapshots the scene as a starting point for the process
  // before starting its LRO to prepare backend services. This avoids race
  // conditions between starting a process with the scene and subsequently
  // editing the scene.
  INTR_ASSIGN_OR_RETURN(
      std::string conductor_operation_name,
      CallConductorPrepareProcessStart(operation_name, options.scene_id,
                                       options.execution_span_id),
      _.LogError());

  absl::MutexLock lock(*clips_->mutex());

  // If the operation is not set to RUNNING state due to an error, we need to
  // notify the conductor to stop the preparation LRO.
  absl::Cleanup cleanup_on_error = [this, &conductor_operation_name] {
    clips_->mutex()->AssertHeld();
    clips_conductor_client_->GetAssertFacade()->GetClipsMutex()->AssertHeld();
    INTR_RETURN_IF_ERROR(
        clips_conductor_client_->AssertStopConductorPreparationFact(
            conductor_operation_name))
        .LogWarning()
        .With(intrinsic::ExtraMessage()
              << "Failed to signal conductor to stop client operation.")
        .With(ReturnVoid());
  };

  INTR_RETURN_IF_ERROR(RunStatus());

  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::ACCEPTED}))
      << "Cannot start execution in current state";

  clips::ProtoMessageId parameter_proto = clips::ProtobufManager::kInvalidId;
  if (options.parameters.has_value()) {
    parameter_proto = proto_mgr_->AddGeneratedProto(*options.parameters);
    if (parameter_proto == clips::ProtobufManager::kInvalidId) {
      return absl::InternalError(
          "Failed to create parameter proto in execution engine.");
    }
  }
  clips::Values resources;
  for (const auto& [resource_key, resource_handle] : options.resources) {
    resources.push_back(clips::Value(resource_key));
    resources.push_back(clips::Value(resource_handle));
  }

  clips::ProtoMessageId recovery_state_proto_id =
      clips::ProtobufManager::kInvalidId;
  if (!options.recovery_nodes.empty()) {
    INTR_ASSIGN_OR_RETURN(BehaviorTree state_proto,
                          GetProcessTreeNoLock(operation_name));
    INTR_RETURN_IF_ERROR(
        ApplyRecoveryPaths(state_proto, options.recovery_nodes));
    recovery_state_proto_id = proto_mgr_->AddGeneratedProto(state_proto);
  }

  INTR_RETURN_IF_ERROR(
      clips_
          ->AssertFact(
              "operation-update-state",
              {{"operation-name", operation_name},
               {"target-state", clips::Symbol("PREPARING")},
               {"parameter-proto", parameter_proto.value()},
               {"resources", resources},
               {"recovery-state-proto", recovery_state_proto_id.value()},
               {"span-reference-id", options.execution_span_id.value()}})
          .status());
  // Changes RunStatus() so that executive starts looping again.
  quit_ = false;
  clips_->NotifyRunner();

  std::move(cleanup_on_error).Cancel();
  return absl::OkStatus();
}

absl::Status ClipsExecutor::RequestToResume(
    absl::string_view operation_name,
    intrinsic_proto::executive::ResumeOperationRequest::ResumeMode mode)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());

  clips::Symbol resume_mode(
      intrinsic_proto::executive::ResumeOperationRequest::ResumeMode_Name(
          mode));

  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::SUSPENDED}))
      << "Cannot resume execution in current state";
  INTR_RETURN_IF_ERROR(
      clips_
          ->AssertFact("operation-update-state",
                       {{"operation-name", operation_name},
                        {"target-state", clips::Symbol("RESUME")},
                        {"resume-mode", resume_mode},
                        {"span-reference-id", 0}})
          .status());
  clips_->NotifyRunner();
  return absl::OkStatus();
}

absl::Status ClipsExecutor::RequestToSuspend(absl::string_view operation_name)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(
      VerifyCompatibleState(GetOperationStateNoLock(operation_name),
                            {intrinsic_proto::executive::RunMetadata::PREPARING,
                             intrinsic_proto::executive::RunMetadata::RUNNING}))
      << "Cannot suspend execution in current state";
  INTR_RETURN_IF_ERROR(
      clips_
          ->AssertFact("operation-update-state",
                       {{"operation-name", operation_name},
                        {"target-state", clips::Symbol("SUSPENDING")},
                        {"span-reference-id", 0}})
          .status());
  clips_->NotifyRunner();
  return absl::OkStatus();
}

absl::Status ClipsExecutor::RequestToCancel(absl::string_view operation_name) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::PREPARING,
       intrinsic_proto::executive::RunMetadata::RUNNING,
       intrinsic_proto::executive::RunMetadata::SUSPENDING,
       intrinsic_proto::executive::RunMetadata::SUSPENDED}))
      << "Cannot cancel execution in current state";
  INTR_RETURN_IF_ERROR(
      clips_
          ->AssertFact("operation-update-state",
                       {{"operation-name", operation_name},
                        {"target-state", clips::Symbol("CANCELING")},
                        {"span-reference-id", 0}})
          .status());
  clips_->NotifyRunner();
  return absl::OkStatus();
}

absl::Status ClipsExecutor::RequestToAbandon(absl::string_view operation_name) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::CANCELING}))
      << "Cannot abandon execution in current state. Must be canceling.";
  INTR_RETURN_IF_ERROR(clips_
                           ->Evaluate(absl::StrFormat(
                               "(operation-abandon \"%s\")", operation_name))
                           .status());
  clips_->NotifyRunner();
  return absl::OkStatus();
}

void ClipsExecutor::WaitForLoop() {
  if (init_status_.ok()) {
    loop_cv_.Wait(clips_->mutex());
  }
}

absl::Status ClipsExecutor::SetExecutionMode(
    absl::string_view operation_name,
    intrinsic_proto::executive::ExecutionMode mode)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::ACCEPTED,
       intrinsic_proto::executive::RunMetadata::SUSPENDED}))
      << "Cannot set execution mode in current state";
  switch (mode) {
    case intrinsic_proto::executive::EXECUTION_MODE_NORMAL: {
      INTR_RETURN_IF_ERROR(
          clips_
              ->AssertFact("operation-update-mode",
                           {{"operation-name", operation_name},
                            {"target-execution-mode", clips::Symbol("NORMAL")}})
              .status());
      break;
    }
    case intrinsic_proto::executive::EXECUTION_MODE_STEP_WISE: {
      INTR_RETURN_IF_ERROR(clips_
                               ->AssertFact("operation-update-mode",
                                            {{"operation-name", operation_name},
                                             {"target-execution-mode",
                                              clips::Symbol("STEP-WISE")}})
                               .status());
      break;
    }
    default: {
      return absl::InvalidArgumentError("Invalid mode");
    }
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::SetSimulationMode(
    absl::string_view operation_name,
    intrinsic_proto::executive::SimulationMode mode)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::ACCEPTED}))
      << "Cannot set simulation mode in current state";
  switch (mode) {
    case intrinsic_proto::executive::SIMULATION_MODE_REALITY: {
      INTR_RETURN_IF_ERROR(clips_
                               ->AssertFact("operation-update-mode",
                                            {{"operation-name", operation_name},
                                             {"target-simulation-mode",
                                              clips::Symbol("REALITY")}})
                               .status());
      break;
    }
    case intrinsic_proto::executive::SIMULATION_MODE_PREVIEW: {
      INTR_RETURN_IF_ERROR(clips_
                               ->AssertFact("operation-update-mode",
                                            {{"operation-name", operation_name},
                                             {"target-simulation-mode",
                                              clips::Symbol("PREVIEW")}})
                               .status());
      break;
    }
    case intrinsic_proto::executive::SIMULATION_MODE_FAST_PREVIEW: {
      INTR_RETURN_IF_ERROR(clips_
                               ->AssertFact("operation-update-mode",
                                            {{"operation-name", operation_name},
                                             {"target-simulation-mode",
                                              clips::Symbol("FAST-PREVIEW")}})
                               .status());
      break;
    }
    default: {
      return absl::InvalidArgumentError("Invalid mode");
    }
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::SetStartNode(absl::string_view operation_name,
                                         absl::string_view start_tree_id,
                                         uint32_t start_node_id)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::ACCEPTED}))
      << "Cannot set start node in current state";

  clips::Symbol start_tree = clips::Symbol(start_tree_id);
  // verify start_tree_id/start_node_id exists and is in operation_name.
  if (!start_tree_id.empty()) {
    INTR_RETURN_IF_ERROR(
        GetNodeFact(operation_name, start_tree_id, start_node_id).status());
  } else {
    if (start_node_id != 0) {
      std::string message = absl::StrFormat(
          "Cannot set start node %d, when no tree id is given. Pass an empty "
          "tree id along with node id 0 to reset to running the full tree.",
          start_node_id);
      return intrinsic::StatusBuilder(absl::InvalidArgumentError(message))
          .AttachExtendedStatus(CreateExtendedStatus(
              12901, message,
              {.severity = intrinsic_proto::status::ExtendedStatus::ERROR}));
    }
    start_tree = clips::Symbol::Nil();
  }

  INTR_RETURN_IF_ERROR(clips_
                           ->AssertFact("operation-update-start-node",
                                        {{"operation-name", operation_name},
                                         {"tree-id", start_tree},
                                         {"node-id", start_node_id}})
                           .status());

  return absl::OkStatus();
}

absl::Status ClipsExecutor::UpdateInitialMetadata(
    std::string_view operation_name,
    intrinsic_proto::executive::RunMetadata* metadata)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  INTR_ASSIGN_OR_RETURN(
      clips::Fact op_fact,
      clips_->GetUniqueFact("operation-envelope", {{"name", operation_name}}),
      _ << "Is an operation loaded?");
  INTR_ASSIGN_OR_RETURN(clips::Value execution_mode_val,
                        op_fact.GetSlotValue("execution-mode"));
  INTR_ASSIGN_OR_RETURN(clips::Symbol execution_mode_sym,
                        execution_mode_val.GetSymbol());
  intrinsic_proto::executive::ExecutionMode execution_mode =
      intrinsic_proto::executive::EXECUTION_MODE_UNSPECIFIED;
  if (execution_mode_sym == clips::Symbol("NORMAL")) {
    execution_mode = intrinsic_proto::executive::EXECUTION_MODE_NORMAL;
  } else if (execution_mode_sym == clips::Symbol("STEP-WISE")) {
    execution_mode = intrinsic_proto::executive::EXECUTION_MODE_STEP_WISE;
  }

  INTR_ASSIGN_OR_RETURN(clips::Value simulation_mode_val,
                        op_fact.GetSlotValue("simulation-mode"));
  INTR_ASSIGN_OR_RETURN(clips::Symbol simulation_mode_sym,
                        simulation_mode_val.GetSymbol());
  intrinsic_proto::executive::SimulationMode simulation_mode =
      intrinsic_proto::executive::SIMULATION_MODE_UNSPECIFIED;
  if (simulation_mode_sym == clips::Symbol("REALITY")) {
    simulation_mode = intrinsic_proto::executive::SIMULATION_MODE_REALITY;
  } else if (simulation_mode_sym == clips::Symbol("PREVIEW")) {
    simulation_mode = intrinsic_proto::executive::SIMULATION_MODE_PREVIEW;
  } else if (simulation_mode_sym == clips::Symbol("FAST-PREVIEW")) {
    simulation_mode = intrinsic_proto::executive::SIMULATION_MODE_FAST_PREVIEW;
  }

  INTR_ASSIGN_OR_RETURN(clips::Value skill_trace_handling_val,
                        op_fact.GetSlotValue("skill-trace-handling"));
  INTR_ASSIGN_OR_RETURN(clips::Symbol skill_trace_handling_sym,
                        skill_trace_handling_val.GetSymbol());
  intrinsic_proto::executive::RunMetadata::TracingInfo::SkillTraceHandling
      skill_trace_handling = intrinsic_proto::executive::RunMetadata::
          TracingInfo::SKILL_TRACES_UNSPECIFIED;
  if (skill_trace_handling_sym == clips::Symbol("LINK")) {
    skill_trace_handling =
        intrinsic_proto::executive::RunMetadata::TracingInfo::SKILL_TRACES_LINK;
  } else if (skill_trace_handling_sym == clips::Symbol("EMBED")) {
    skill_trace_handling = intrinsic_proto::executive::RunMetadata::
        TracingInfo::SKILL_TRACES_EMBED;
  }

  INTR_ASSIGN_OR_RETURN(clips::Value process_tree_id_val,
                        op_fact.GetSlotValue("operation-tree-id"));
  INTR_ASSIGN_OR_RETURN(clips::Symbol process_tree_id_sym,
                        process_tree_id_val.GetSymbol());
  INTR_ASSIGN_OR_RETURN(clips::Value start_tree_id_val,
                        op_fact.GetSlotValue("start-tree-id"));
  INTR_ASSIGN_OR_RETURN(clips::Symbol start_tree_id_sym,
                        start_tree_id_val.GetSymbol());

  INTR_ASSIGN_OR_RETURN(clips::Value scene_id_val,
                        op_fact.GetSlotValue("scene-id"));
  INTR_ASSIGN_OR_RETURN(std::string scene_id, scene_id_val.GetString());

  INTR_ASSIGN_OR_RETURN(
      clips::Fact start_tree_fact,
      clips_->GetUniqueFact("behavior-tree", {{"id", start_tree_id_sym}}),
      _ << "The referred start-tree-id (" << start_tree_id_sym.ToString()
        << ") of the operation does not exist or "
           "is not unique.");
  INTR_ASSIGN_OR_RETURN(clips::Value root_node_id_val,
                        start_tree_fact.GetSlotValue("root"));
  INTR_ASSIGN_OR_RETURN(int64_t root_node_id, root_node_id_val.GetInteger());
  INTR_ASSIGN_OR_RETURN(clips::Value start_node_id_val,
                        start_tree_fact.GetSlotValue("start-node-id"));
  INTR_ASSIGN_OR_RETURN(int64_t start_node_id, start_node_id_val.GetInteger());

  if (process_tree_id_sym == start_tree_id_sym &&
      root_node_id == start_node_id) {
    metadata->clear_start_tree_id();
    metadata->clear_start_node_id();
  } else {
    metadata->set_start_tree_id(start_tree_id_sym.ToString());
    metadata->set_start_node_id(start_node_id);
  }

  if (config_.tracing_add_info_to_state()) {
    INTR_ASSIGN_OR_RETURN(clips::Value trace_id_val,
                          op_fact.GetSlotValue("trace-id"));
    INTR_ASSIGN_OR_RETURN(std::string trace_id, trace_id_val.GetString());
    INTR_ASSIGN_OR_RETURN(clips::Value trace_url_val,
                          op_fact.GetSlotValue("trace-url"));
    INTR_ASSIGN_OR_RETURN(std::string trace_url, trace_url_val.GetString());
    metadata->mutable_tracing_info()->set_trace_id(trace_id);
    metadata->mutable_tracing_info()->set_trace_url(trace_url);
  }

  metadata->set_execution_mode(execution_mode);
  metadata->set_simulation_mode(simulation_mode);
  metadata->mutable_tracing_info()->set_skill_trace_handling(
      skill_trace_handling);
  metadata->set_scene_id(scene_id);

  absl::StatusOr<absl::Time> start_time = GetStartTimeNoLock(operation_name);
  if (!start_time.ok()) {
    LOG(WARNING) << "Could not get start time: " << start_time.status();
    metadata->clear_start_time();
  } else if (!FromAbslTime(*start_time, metadata->mutable_start_time()).ok()) {
    LOG(ERROR) << "Could not encode start time " << *start_time
               << " into start_time proto";
    metadata->clear_start_time();
  }

  return absl::OkStatus();
}

absl::Status ClipsExecutor::SetSkillTraceHandling(
    absl::string_view operation_name,
    intrinsic_proto::executive::RunMetadata::TracingInfo::SkillTraceHandling
        skill_trace_handling) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name),
      {intrinsic_proto::executive::RunMetadata::ACCEPTED}))
      << "Cannot set skill trace handling in current state";

  switch (skill_trace_handling) {
    case intrinsic_proto::executive::RunMetadata::TracingInfo::
        SKILL_TRACES_LINK: {
      INTR_RETURN_IF_ERROR(
          clips_
              ->AssertFact("operation-update-skill-trace-handling",
                           {{"operation-name", operation_name},
                            {"target-handling", clips::Symbol("LINK")}})
              .status());
      break;
    }
    case intrinsic_proto::executive::RunMetadata::TracingInfo::
        SKILL_TRACES_EMBED: {
      INTR_RETURN_IF_ERROR(
          clips_
              ->AssertFact("operation-update-skill-trace-handling",
                           {{"operation-name", operation_name},
                            {"target-handling", clips::Symbol("EMBED")}})
              .status());
      break;
    }
    default: {
      return absl::InvalidArgumentError("Invalid skill-trace-handling");
    }
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::CreateOperation(
    absl::string_view operation_name, const BehaviorTree& process_tree,
    intrinsic_proto::status::ExtendedStatus* absl_nullable
        diagnostics_for_create,
    bool test_skip_parameter_validation) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());

  absl::Status operation_status =
      GetOperationStateNoLock(operation_name).status();
  if (operation_status.code() != absl::StatusCode::kNotFound) {
    return absl::AlreadyExistsError(
        "Failed to import an operation as there is already an operation with "
        "the same name.");
  }

  INTR_RETURN_IF_ERROR(clips::ImportProcessTree(
      process_tree, operation_name, clips_.get(), proto_mgr_.get(),
      skill_client_generator_.get(), diagnostics_for_create,
      test_skip_parameter_validation));
  clips_->NotifyRunner();
  return absl::OkStatus();
}

absl::Status ClipsExecutor::CreateOperationFromProcessId(
    absl::string_view operation_name, intrinsic_proto::assets::Id process_id,
    intrinsic_proto::status::ExtendedStatus* absl_nullable
        diagnostics_for_create,
    bool test_skip_parameter_validation) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::executive::BehaviorTree tree_proto,
                        skill_client_generator_->GetProcessAsset(process_id));
  return CreateOperation(operation_name, tree_proto, diagnostics_for_create,
                         test_skip_parameter_validation);
}

absl::Status ClipsExecutor::ResetOperation(absl::string_view operation_name,
                                           bool keep_blackboard)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name), GetWaitingStates()))
      << "Cannot reset process behavior tree in current state";

  INTR_RETURN_IF_ERROR(
      clips_
          ->EvaluateResult(absl::StrFormat(
              "(operation-reset \"%s\" %s)", operation_name,
              keep_blackboard ? clips::Symbol::True().ToString()
                              : clips::Symbol::False().ToString()))
          .status());

  operation_extended_status_with_legacy_errors_.erase(operation_name);

  // TODO(b/319835803): What should this do in the context of multiple
  // operations?
  INTR_RETURN_IF_ERROR(clips_->Evaluate("(world-reset)").status());

  // Run to trigger reset rules.
  clips_->RefreshAgenda();
  const int64_t invocations = clips_->Run();
  RuleInvocationsMeasure().Record(
      invocations, opentelemetry::context::RuntimeContext::GetCurrent());

  return absl::OkStatus();
}

absl::Status ClipsExecutor::DeleteOperation(std::string_view operation_name)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_RETURN_IF_ERROR(RunStatus());
  INTR_RETURN_IF_ERROR(GetOperationStateNoLock(operation_name).status());
  INTR_RETURN_IF_ERROR(VerifyCompatibleState(
      GetOperationStateNoLock(operation_name), GetWaitingStates()))
      << "Cannot delete process behavior tree in current state";

  const std::vector<clips::Fact> facts_before = clips_->GetFacts();

  absl::Status delete_status =
      clips_
          ->EvaluateResult(
              absl::StrFormat("(operation-delete \"%s\")", operation_name))
          .status();

  // Run to trigger reset rules.
  // Call this even if operation-delete failed. Whatever errors that might have
  // caused will be caught be the next Run, i.e., this one.
  clips_->RefreshAgenda();
  absl::StatusOr<clips::Trace> run_status =
      clips_->RunWithTracing(config_.clips_run_limit());
  // TODO(b/325616328) The next block catches any unexpected errors present
  // after operation-delete to catch spurious bugs when they are caused here.
  if (!delete_status.ok() || !run_status.ok() ||
      !clips_
           ->QueryFacts("error",
                        {{"trigger-full-report", clips::Symbol::True()}})
           .empty()) {
    intrinsic::StatusBuilder full_status =
        intrinsic::StatusBuilder(absl::InternalError(
            "Failed to delete the operation. This might indicate an internal "
            "error. Please report this error, and if the problem persists "
            "restart "
            "the application. "));
    clips::Trace trace(clips_.get());
    if (run_status.ok()) {
      trace = *run_status;
    }

    intrinsic_proto::status::ExtendedStatus delete_fail_es =
        CreateExtendedStatus(12010, "");
    if (!delete_status.ok()) {
      delete_fail_es.mutable_user_report()->set_message(
          delete_status.message());
    }
    if (!run_status.ok()) {
      delete_fail_es.mutable_user_report()->mutable_message()->append(
          run_status.status().message());
    }
    absl::StatusOr<intrinsic_proto::status::ExtendedStatus> operation_es =
        GetOperationExtendedStatus(clips_.get(), proto_mgr_.get(),
                                   operation_name);
    if (operation_es.ok()) {
      *delete_fail_es.add_context() = std::move(*operation_es);
    }
    AddExtendedStatusLegacyErrors(clips_.get(), delete_fail_es);
    AddExtendedStatusDebugInformation(delete_fail_es, facts_before, trace);
    LogExecutiveExtendedStatus(delete_fail_es, operation_name).IgnoreError();

    full_status.AttachExtendedStatus(std::move(delete_fail_es));

    absl::Status ret_status = full_status;
    LOG(ERROR) << "POSSIBLE INCONSISTENT STATE: " << ret_status;

    // Retract errors that trigger a full report as that has happened here once
    // already.
    clips_
        ->RetractFacts("error",
                       {{"trigger-full-report", clips::Symbol::True()}})
        .IgnoreError();
    return ret_status;
  }

  const int64_t invocations = run_status->CountRulesFired();
  RuleInvocationsMeasure().Record(
      invocations, opentelemetry::context::RuntimeContext::GetCurrent());

  operation_extended_status_with_legacy_errors_.erase(operation_name);

  // Force memory allocator to release unused memory. This should happen
  // automatically by the allocator, but often seems to not be triggered and
  // thus the process accumulates freed memory.
  // TODO(b/522727421): Remove this when switching to tcmalloc.
  malloc_trim(0);

  return absl::OkStatus();
}

absl::StatusOr<clips::TraceSpanReferenceId>
ClipsExecutor::StartExecutionSpan() {
  if (!span_mgr_) {
    return absl::FailedPreconditionError("span_mgr_ was nullptr");
  }
  std::shared_ptr<opentelemetry::trace::Span> execution_span =
      stats::StartSampledRootSpan("Executive Run");
  INTR_ASSIGN_OR_RETURN(clips::TraceSpanReferenceId execution_span_id,
                        span_mgr_->AddSpan(execution_span));
  if (config_.google_cloud_project().empty()) {
    LOG(WARNING) << "No Google cloud project set, cannot generate tracing link";
  } else {
    INTR_ASSIGN_OR_RETURN(
        std::string trace_url,
        span_mgr_->GetGoogleCloudTracingLink(execution_span_id,
                                             config_.google_cloud_project()));
    LOG(INFO) << "Started execution span. Trace: " << trace_url;
  }

  RetrieveSolutionInfoAndWarn();
  if (solution_status_.has_value()) {
    execution_span->SetAttribute("cluster_name",
                                 solution_status_->cluster_name());
    execution_span->SetAttribute("platform_version",
                                 solution_status_->platform_version());
    execution_span->SetAttribute("solution_name", solution_status_->name());
    execution_span->SetAttribute("solution_display_name",
                                 solution_status_->display_name());
  }

  last_execution_span_reference_id_ = execution_span_id;
  return execution_span_id;
}

absl::StatusOr<absl::Duration> ClipsExecutor::GetExecutionDuration(
    std::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());

  std::vector<clips::Fact> time_meas_fact = clips_->QueryFacts(
      "time-measurement", {{"operation-name", operation_name}});
  if (time_meas_fact.size() != 1) {
    return absl::FailedPreconditionError(
        absl::StrFormat("Exactly one time-measurement fact should have "
                        "been found for %s, found %d. Is no operation loaded?",
                        operation_name, time_meas_fact.size()));
  }

  INTR_ASSIGN_OR_RETURN(clips::Value duration_val,
                        time_meas_fact.front().GetSlotValue("duration"));
  INTR_ASSIGN_OR_RETURN(double duration, duration_val.GetDouble());

  INTR_ASSIGN_OR_RETURN(intrinsic_proto::executive::RunMetadata::State state,
                        GetOperationStateNoLock(operation_name));
  if (duration <= 0.0 &&
      (state == intrinsic_proto::executive::RunMetadata::ACCEPTED ||
       state == intrinsic_proto::executive::RunMetadata::PREPARING)) {
    return absl::FailedPreconditionError("Execution not started");
  }
  return absl::Seconds(duration);
}

absl::StatusOr<absl::Time> ClipsExecutor::GetStartTimeNoLock(
    std::string_view operation_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(clips_->mutex()) {
  std::vector<clips::Fact> desired_fact = clips_->QueryFacts(
      "time-measurement", {{"operation-name", operation_name}});
  if (desired_fact.size() != 1) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Requested time-measurement is not unique, but returned %d results.",
        desired_fact.size()));
  }

  INTR_ASSIGN_OR_RETURN(clips::Values start_time_vals,
                        desired_fact.front().GetSlotValues("start-time"));
  INTR_ASSIGN_OR_RETURN(int64_t start_s, start_time_vals[0].GetInteger());
  INTR_ASSIGN_OR_RETURN(int64_t start_ns, start_time_vals[1].GetInteger());
  if (start_s == 0 && start_ns == 0) {
    return absl::FailedPreconditionError(
        "start-time is not set. Is the operation not started?");
  }
  google::protobuf::Timestamp ts;
  ts.set_seconds(start_s);
  ts.set_nanos(start_ns);
  INTR_ASSIGN_OR_RETURN(absl::Time start_time, ToAbslTime(ts));
  return start_time;
}

absl::StatusOr<absl::Time> ClipsExecutor::GetStartTime(
    std::string_view operation_name) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  return GetStartTimeNoLock(operation_name);
}

void ClipsExecutor::Run(StopToken stop_token)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  auto local_init_status = InitExecutor();
  if (!local_init_status.ok()) {
    absl::MutexLock lock(*clips_->mutex());
    init_status_ = local_init_status;
    LOG(ERROR) << "Init status: " << init_status_;
    absl::MutexLock l(teardown_mutex_);
    if (absl::Status teardown_status = TearDown(); !teardown_status.ok()) {
      LOG(ERROR) << "executor teardown failed: " << teardown_status;
    }
    // Signal threads that are waiting in WaitForLoop()
    loop_cv_.SignalAll();
    quit_ = true;
    terminate_cv_.SignalAll();
    return;
  }

  // Thread should keep looping in this call until terminated.
  auto local_run_status = RunWithStatus(stop_token);
  if (!local_run_status.ok()) {
    absl::MutexLock lock(*clips_->mutex());
    run_status_ = local_run_status;
  }

  LOG(ERROR) << "Tearing down Run() loop that terminated with status: "
             << local_run_status;

  absl::MutexLock l(teardown_mutex_);
  if (absl::Status teardown_status = TearDown(); !teardown_status.ok()) {
    LOG(ERROR) << "executor teardown failed: " << teardown_status;
  }
  quit_ = true;
  terminate_cv_.SignalAll();
  LOG(ERROR) << "Tear down completed. Run() loop stopped. Requests will not be "
                "executed any more.";
}

absl::Status ClipsExecutor::RunWithStatus(StopToken stop_token)
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  while (!stop_token.stop_requested()) {
    auto expected_end_time =
        absl::Now() + absl::Milliseconds(config_.loop_time_ms());
    absl::Status local_run_status;
    {
      absl::MutexLock lock(*clips_->mutex());
      local_run_status = RunStatus();
    }
    if (local_run_status.ok()) {
      INTR_RETURN_IF_ERROR(InternalLoop()) << " in executor loop";
    }

    {
      absl::MutexLock lock(loop_wait_mutex_);
      loop_wait_cv_.WaitWithDeadline(&loop_wait_mutex_, expected_end_time);
    }

    // This sleep is here to ensure that this thread yields to others to
    // acquire the mutex, e.g., to assert CLIPS facts or to call
    // WaitForLoop(). If the loop ran longer than loop_time_ms, other
    // threads could otherwise starve.
    absl::SleepFor(kRunThreadSleepTime);
  }
  return absl::OkStatus();
}

absl::Status ClipsExecutor::SetClipsTracing(
    intrinsic_proto::executive::SetClipsTracingRequest::ClipsTracing
        clips_tracing) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  switch (clips_tracing) {
    case intrinsic_proto::executive::SetClipsTracingRequest::DISABLED:
      return clips_->CloseTraceFileLog();
      break;
    case intrinsic_proto::executive::SetClipsTracingRequest::ENABLED:
      // request to enable and trace already running -> everything is OK
      if (clips_->GetTraceFileLogName().status().ok()) {
        return absl::OkStatus();
      }
      return clips_
          ->StartTraceFileLog(kClipsDebugTraceLogDefault, /*always_flush=*/true)
          .status();
      break;
    default:
      return absl::InvalidArgumentError(absl::StrFormat(
          "Called SetClipsTracing with unhandled value: %d", clips_tracing));
      break;
  }
  return absl::OkStatus();
}

absl::StatusOr<uint64_t> ClipsExecutor::GetClipsTracingPosition()
    ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  return clips_->GetTraceFilePosition();
}

absl::StatusOr<std::string> ClipsExecutor::GetClipsTraceFileContents(
    uint64_t start_offset) ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  return clips_->GetTraceFileContents(start_offset);
}

absl::StatusOr<intrinsic_proto::executive::GetClipsDataResponse>
ClipsExecutor::GetClipsData() ABSL_LOCKS_EXCLUDED(clips_->mutex()) {
  absl::MutexLock lock(*clips_->mutex());
  if (!is_initialized_) {
    return absl::FailedPreconditionError(
        "ClipsExecutor not initialized. Cannot get internal data.");
  }
  intrinsic_proto::executive::GetClipsDataResponse internal_data;
  for (const std::string& fact_string : clips_->GetFactsAsStrings()) {
    *internal_data.add_facts() = fact_string;
  }
  for (const std::string& fact_string : clips_->GetFactsAsStrings("", true)) {
    *internal_data.add_facts_with_identifiers() = fact_string;
  }
  for (const auto& [id, proto_str] : proto_mgr_->DebugGetAllProtosAsStrings()) {
    (*internal_data.mutable_protos())[id.value()] = proto_str;
  }
  return internal_data;
}

absl::StatusOr<std::string> ClipsExecutor::GetSceneId(
    absl::string_view operation_name) {
  absl::MutexLock lock(*clips_->mutex());
  INTR_ASSIGN_OR_RETURN(
      clips::Fact op_fact,
      clips_->GetUniqueFact("operation-envelope", {{"name", operation_name}}),
      _ << "Is an operation loaded?");
  INTR_ASSIGN_OR_RETURN(clips::Value scene_id_val,
                        op_fact.GetSlotValue("scene-id"));
  return scene_id_val.GetString();
}

}  // namespace executive
}  // namespace intrinsic
