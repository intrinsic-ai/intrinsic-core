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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_ENVIRONMENT_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_ENVIRONMENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/node_hash_map.h"
#include "absl/log/log.h"
#include "absl/log/log_streamer.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/function_util.h"
#include "intrinsic/executive/clips_cpp/slot_value.h"
#include "intrinsic/executive/clips_cpp/template.h"
#include "intrinsic/executive/clips_cpp/trace.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/file.h"

namespace intrinsic {
namespace executive {
namespace clips {

// CLIPS Environment: this is the main context to use CLIPS.
// All initial interactions go through this class. Once created, all other
// datatypes are associated to an environment. It must hence outlive all
// other related data structures.
// This C++ wrapper uses a mutex so it can be used with multiple threads.
// For this, caller needs to lock 'mutex()', and regularly unlock it to allow
// other threads to make progress.
// (CLIPS internally is not thread-safe.)
//
// Usage Examples:
// Environment env;
// absl::MutexLock lock(env.mutex());
// env.AssertFact("(myfact)");
// auto facts = env.GetFacts();
// env.AddFunction("sum", +[](int a, int b) -> int) {
//                            return a + b;
//                        });
// INTR_ASSIGN_OR_RETURN(auto return_values, env.Evaluate("(sum 1 2)"));
// INTR_ASSIGN_OR_RETURN(int sum_value, return_values[0].GetInteger());
//
// For details on CLIPS concepts read the CLIPS Basic Programming Manual.

class EnvironmentFunctionFacade;  // defined in function_facade.h
class EnvironmentAssertFacade;    // defined in assert_facade.h
class EnvironmentReadonlyFacade;  // defined in readonly_facade.h

class Environment {
 public:
  Environment();
  ~Environment();

  // non-copyable
  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;

  // Access to the mutex that guards the entire environment so caller can take
  // a lock, which is required for all functions.
  absl::Mutex* mutex() const ABSL_LOCK_RETURNED(mutex_) { return &mutex_; }

  // Access to raw C pointer, avoid usage if possible, but necessary internally.
  void* GetCPtr() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) { return c_env_; }

  // Functional interface wrapping the CLIPS C API.

  // Make redefinitions errors instead of warnings (CLIPS default).
  // This will make Load() return an error if a contsruct is redefined. Once
  // enabled, this also applies to the CLIPS function (load).
  // As a side effect, will enable/add (is_error==true) or disable/remove
  // (is_error==false) watching of WatchItem::kCompilations/log callback.
  absl::Status SetRedefineDuringLoadIsError(bool is_error = true)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Load clp file with constructs (rules, templates). 'filename' is expected to
  // be a runfiles path (e.g. intrinsic/executive/clips/utils.clp). Returns
  // NotFoundError if file cannot be opened, InvalidArgumentError on syntax
  // error, or Ok otherwise. Load can load an arbitrary number of files, but
  // requires care to be taken to not access undefined constructs.
  absl::Status LoadRunfile(absl::string_view filename)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as LoadRunfile() but for usage from a test context.
  absl::Status LoadTestRunfile(absl::string_view filename)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Load and evaluate a CLIPS file, cf. CLIPS (batch*) command. 'filename' is
  // expected to be a runfiles path (e.g.
  // intrinsic/executive/clips/utils.clp). Note that for loading constructs
  // you must use Load(). Returns true on success, NOT_FOUND if file could not
  // be opened. Note that unfortunately, only a limited number of errors is
  // reported. That is, syntax errors are typically not, invoking non-existent
  // functions is. The recommendation is to prefer Load() whenever possible, and
  // use rules to run commands during initialization.
  absl::Status BatchEvaluate(absl::string_view filename)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as BatchEvaluate() but for usage from a test context.
  absl::Status BatchEvaluateForTest(absl::string_view filename)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Evaluate a CLIPS expression and return its result.
  absl::StatusOr<Values> Evaluate(const std::string& clips_expression)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Evaluates an expression that returns a CLIPS result, i.e., a multi-field
  // that starts with TRUE/FALSE and then a result or an error message on FALSE.
  // The function returns the multifield values after TRUE on success and
  // absl::AbortedError with the error message on FALSE.
  absl::StatusOr<Values> EvaluateResult(const std::string& clips_expression)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Same as 'Evaluate' but returns an error if the Evaluate result does not
  // contain exactly one value.
  absl::StatusOr<Value> EvaluateExpectSingleReturn(
      const std::string& clips_expression)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Build a construct such as a rule or template.
  // TODO(intrinsic-opx): Capture error string from CLIPS (which is sent to
  // stderr) and return in absl::Status.
  absl::Status Build(const std::string& construct_string)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Assert a fact into the fact base.
  // Note that the behavior also depends on the environment configuration,
  // which can be modified from CLIPS code within the environment. For
  // example, (set-fact-duplication) can be used to modify this behavior.
  // We generally recommend to keep it disabled and use other means to
  // disambiguate facts.
  absl::StatusOr<Fact> AssertFact(const std::string& fact_string)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Assert a fact into the fact base from template and slot values.
  // Note that the behavior also depends on the environment configuration,
  // which can be modified from CLIPS code within the environment. For
  // example, (set-fact-duplication) can be used to modify this behavior.
  // We generally recommend to keep it disabled and use other means to
  // disambiguate facts.
  absl::StatusOr<Fact> AssertFact(const std::string& template_name,
                                  absl::Span<const SlotValue> slots)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::StatusOr<Fact> AssertFact(const Template& fact_template,
                                  absl::Span<const SlotValue> slots)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Modify the given fact. This clones the fact, makes modifications based on
  // the given slot values, retracts the given fact, and asserts the new
  // modified fact.
  absl::StatusOr<Fact> ModifyFact(Fact&& fact,
                                  const std::vector<SlotValue>& modifications)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Call AssertFact on a number of facts. Aborts insertion as soon as any
  // fact fails to be asserted.
  absl::Status AssertFacts(absl::Span<const std::string> fact_strings)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  absl::Status AssertFacts(
      absl::Span<
          const std::pair<const std::string, const std::vector<SlotValue>>>
          facts) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Retract fact from working memory.
  absl::Status RetractFact(Fact&& fact) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Retract facts matching template_name and the given SlotValues.
  // This is O(N*K) with N = number of facts in the fact base and K being the
  // number of slots to query.
  absl::Status RetractFacts(absl::string_view template_name,
                            absl::Span<const SlotValue> slots)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Enable tracing of the given items.
  // This function provides a useful subset of items which can be watched.
  // Using the (watch)/(unwatch) CLIPS functions enables more fine-grained
  // control, e.g., watching specific fact templates or specific rules.
  enum class WatchItem {
    kAll,
    kRules,
    kFacts,
    kGlobals,
    kCompilations,
    kStatistics,
    kDefFunctions,
    kActivations
  };
  absl::Status Watch(WatchItem item) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status Unwatch(WatchItem item) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // This enables to watch or unwatch more specifically. For example:
  // environment->Watch(Environment::WatchItem::kFacts);
  // environment->Unwatch(Environment::WatchItem::kFacts, "time");
  // This watches all fact updates but not the ones for time facts.
  absl::Status Watch(WatchItem item, absl::string_view item_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status Unwatch(WatchItem item, absl::string_view item_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Add a callback for (text) log messages.
  enum class LogLevel {
    kDebug,
    kClips,  // corresponds to CLIPS' WDIALOG printing of internal info
    kTrace,
    kInfo,
    kWarn,
    kError,
    kInternal,  // to be printed only in privileged trusted contexts
  };
  using LogCallback = std::function<void(LogLevel, absl::string_view)>;
  // Add or remove log callbacks. The name is a unique identifier passed to
  // CLIPS to identify the logger. Will return INVALID_ARGUMENT if name has
  // already been registered.
  absl::Status AddLogCallback(absl::string_view name, LogCallback callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void RemoveLogCallback(absl::string_view name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Environment::Run() (or RunWithTracing) must be called periodically by some
  // entity to ensure updates are processed (rules triggered for new facts).
  // This typically happens in some dedicated thread at a given frequency, that
  // we designate as the "runner" thread. That thread may register a callback
  // that can be notified on update to run as soon as possible rather than at a
  // high frequency.
  absl::Status SetRunnerCallback(std::function<void()> callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void NotifyRunner() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // This adds a default callback that will log:
  // - debug via VLOG(1)
  // - trace via VLOG(2)
  // - info via LOG(INFO)
  // - warn via LOG(WARNING)
  // - error via LOG(ERROR)
  // Setting verbose to true sets compile-time verbosity, i.e., all log messages
  // will be printed at WARNING/ERROR/INFO levels, no VLOG is used.
  // Will remove mute or error logger if previously installed.
  absl::Status AddDefaultLogCallback(bool verbose = false)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Mute logger (ignores log messages, avoid no callback warning).
  // Will remove default or error logger if previously installed.
  absl::Status AddMuteLogCallback() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Print warnings and errors to log, ignore anything else.
  // This logger is added by default. Adding a default or mute logger will
  // remove the error logger. Adding the error logger will remove an installed
  // default or mute logger.
  absl::Status AddErrorLogCallback() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Set up to log traces into logfile.
  // The logfile_name parameter may contain the following tokens:
  // - %timestamp%: replaced by date and time
  // - %count%: replaced by 3-digit counter, will try files in counter order and
  //            use the first file that it can open that does not exist, yet.
  //            Will error if no non-existing file found after trying counter
  //            1 to 999.
  // If always_flush is true then the file will be explicitly flushed after
  // every line.
  // Returns the full path to the log file after substitution.
  absl::StatusOr<std::string> StartTraceFileLog(absl::string_view logfile_name,
                                                bool always_flush = false)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Get the name of the current trace file log.
  // Returns an error, if not trace file is opened.
  absl::StatusOr<std::string> GetTraceFileLogName();

  // Get the position in the trace file.
  // Returns an error if there is no trace file open.
  absl::StatusOr<uint64_t> GetTraceFilePosition();

  // Get the full contents of the currently opened trace file starting at
  // start_offset.
  // Returns an error if no trace file is open.
  absl::StatusOr<std::string> GetTraceFileContents(uint64_t start_offset)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Add a line to the CLIPS trace file log
  absl::Status AddTraceFileLogLine(absl::string_view log_message);

  // Manually flush the trace file log.
  absl::Status FlushTraceFileLog();

  // Close the current trace log, if it exists.
  absl::Status CloseTraceFileLog() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Log a message to the configured log callbacks.
  void LogText(LogLevel level, const char* str)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Get all facts currently stored in the environment.
  // Returns only facts of matching template_name, all if passed name is empty.
  std::vector<Fact> GetFacts(absl::string_view template_name = "") const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Gets the list of facts as formatted strings
  // Returns only facts of matching template_name, all if passed name is empty.
  // Including identifiers prefixes fact strings with "f-<index>" fact indexes.
  std::vector<std::string> GetFactsAsStrings(
      absl::string_view template_name = "",
      bool include_identifier = false) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Query facts currently stored in the environment for specific facts.
  // Returns only facts of matching template_name and the given SlotValues.
  // This is O(N*K) with N = number of facts in the fact base and K being the
  // number of slots to query.
  std::vector<Fact> QueryFacts(absl::string_view template_name,
                               absl::Span<const SlotValue> slots) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Performs the same call as QueryFacts with the same inputs and restrictions.
  // However, it requires QueryFacts to return exactly one fact and returns that
  // or an error otherwise.
  absl::StatusOr<Fact> GetUniqueFact(absl::string_view template_name,
                                     absl::Span<const SlotValue> slots) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Get all templates.
  std::vector<std::string> GetTemplateNames() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  std::vector<Template> GetTemplates() const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::StatusOr<Template> GetTemplate(const std::string& template_name) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Removes this template from the environment
  absl::Status RemoveTemplate(const Template& clips_template)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Access to global variables
  absl::StatusOr<ValueOrValues> GetGlobal(const std::string& global_name) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status SetGlobal(const std::string& global_name,
                         const Value& value) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status SetGlobal(const std::string& global_name,
                         const Values& values) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Resets the CLIPS environment:
  // - removes all activations from agenda
  // - removes all facts
  // - assigns global variables to their initial value
  // - asserts all facts listed in deffacts
  // - rules may be activated by such facts
  // Cf. the Basic Programming Guide on (reset) for further details.
  void Reset() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Refresh the agenda such that a consecutive Run() will trigger rules.
  void RefreshAgenda() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Process the agenda, i.e., actually run it.
  int64_t Run(int64_t run_limit = -1) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Process the agenda and record a trace of firing rules and fact
  // modifications. The environment must outlive the return Trace.
  absl::StatusOr<Trace> RunWithTracing(int64_t run_limit = -1)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Add a function which may then be called with the given name from CLIPS.
  // An error is returned if a function with that name has already been
  // registered.
  template <typename ReturnType, typename... Args>
  absl::Status AddFunction(const std::string& name,
                           const std::function<ReturnType(Args...)>& callback)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Check if a certain function name has been registered
  bool HasFunction(const std::string& name) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Remove a function from the CLIPS environment.
  absl::Status RemoveFunction(const std::string& name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Add a function which may then be called with the given name from CLIPS.
  // An error is returned if a function with that name has already been
  // registered.
  // This instance can be used to invoke with a lambda with empty closure.
  // Prefix lambda with + to convert to function pointer, e.g.:
  // AddFunction("one", +[]() -> int { return 1; });
  template <typename ReturnType, typename... Args>
  absl::Status AddFunction(const std::string& name,
                           ReturnType (*callback)(Args...))
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Returns a pointer to a facade object that only allows the caller to
  // register functions in CLIPS.
  EnvironmentFunctionFacade* GetFunctionFacade() const {
    return function_facade_.get();
  }

  // Returns a pointer to a facade object that only allows the caller to
  // assert new facts.
  EnvironmentAssertFacade* GetAssertFacade() const {
    return assert_facade_.get();
  }

  // Returns a pointer to a facade object that only allows the caller to
  // access read-only information.
  EnvironmentReadonlyFacade* GetReadonlyFacade() const {
    return readonly_facade_.get();
  }

  void DebugPrintAllFacts(
      std::ostream* stream = &absl::LogInfoStreamer(__builtin_FILE(),
                                                    __builtin_LINE())
                                  .stream()) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  void DebugPrintAllFacts(
      absl::string_view template_name,
      std::ostream* stream = &absl::LogInfoStreamer(__builtin_FILE(),
                                                    __builtin_LINE())
                                  .stream()) const
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

 private:
  void InstallLogRouter() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  absl::Status LoadRunfileImpl(absl::string_view filename, bool is_test)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  mutable absl::Mutex mutex_;
  // void* is the native type used by the CLIPS API
  void* c_env_ ABSL_GUARDED_BY(mutex_);
  absl::node_hash_map<std::string, detail::CallbackInfo> function_callbacks_
      ABSL_GUARDED_BY(mutex_);
  absl::node_hash_map<std::string, LogCallback> log_callbacks_
      ABSL_GUARDED_BY(mutex_);
  absl::node_hash_map<LogLevel, std::string> log_buffers_
      ABSL_GUARDED_BY(mutex_);
  bool redefine_during_load_is_error_ ABSL_GUARDED_BY(mutex_) = false;
  std::vector<std::string> redefine_errors_;
  bool no_log_callback_warning_printed_ = false;

  std::unique_ptr<EnvironmentFunctionFacade> function_facade_;
  std::unique_ptr<EnvironmentAssertFacade> assert_facade_;
  std::unique_ptr<EnvironmentReadonlyFacade> readonly_facade_;

  std::optional<std::function<void()>> notify_runner_callback_
      ABSL_GUARDED_BY(mutex_);

  File* clips_trace_file_ = nullptr;
  uint64_t clips_trace_file_position_ = 0;
};

namespace detail {
// Forward declaration to avoid having to include clips.h and pollute callers
// namespace with C functions.
extern "C" {
int EnvDefineFunction2WithContext(void* env, const char* name, int return_code,
                                  int (*callback)(void*),
                                  const char* actual_name,
                                  const char* arg_restriction, void* context);
}
}  //  namespace detail

template <typename ReturnType, typename... Args>
absl::Status Environment::AddFunction(
    const std::string& name,
    const std::function<ReturnType(Args...)>& callback) {
  INTR_ASSIGN_OR_RETURN(const char return_code, GetReturnCode<ReturnType>());
  INTR_ASSIGN_OR_RETURN(const std::string arg_string,
                        GetArgumentRestriction<Args...>());
  auto callback_lambda = GetCallbackLambda(name, callback);
  // get a stable pointer to the name, CLIPS does *not* create a copy.
  // Keep function object alive.
  auto [cb_iterator, ok] = function_callbacks_.emplace(
      name, detail::CallbackInfo{.function = callback_lambda,
                                 .argument_string = arg_string});
  if (!ok) {
    return absl::AlreadyExistsError(
        absl::StrFormat("Function '%s' already registered", name));
  }
  const std::string& name_from_map = cb_iterator->first;
  int rv = detail::EnvDefineFunction2WithContext(
      c_env_, name_from_map.c_str(), return_code,
      ClipsCallback<ReturnType, Args...>(), name_from_map.c_str(),
      cb_iterator->second.argument_string.c_str(), &cb_iterator->second);

  if (rv == 0) {
    return absl::InternalError("Failed to register function with CLIPS");
  }
  return absl::OkStatus();
}

template <typename ReturnType, typename... Args>
absl::Status Environment::AddFunction(const std::string& name,
                                      ReturnType (*callback)(Args...)) {
  std::function<ReturnType(Args...)> func_wrapper(callback);
  return AddFunction(name, func_wrapper);
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_ENVIRONMENT_H_
