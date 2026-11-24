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

#include "intrinsic/executive/clips_cpp/environment.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic/executive/clips_cpp/assert_facade.h"
#include "intrinsic/executive/clips_cpp/fact.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/readonly_facade.h"
#include "intrinsic/executive/clips_cpp/slot_value.h"
#include "intrinsic/executive/clips_cpp/template.h"
#include "intrinsic/executive/clips_cpp/trace.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/clips_cpp/value_util.h"
#include "intrinsic/util/log_lines.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_macros.h"
#include "ortools/base/file.h"
#include "ortools/base/filesystem.h"
#include "ortools/base/helpers.h"
#include "ortools/base/options.h"
// Keep CLIPS at the bottom to prevent macro pollution.
#include "clips/clips.h"
#include "clips/src/factmngr.h"

namespace intrinsic {
namespace executive {
namespace clips {
namespace {
std::string WatchItemToString(Environment::WatchItem item) {
  switch (item) {
    case Environment::WatchItem::kAll:
      return "all";
    case Environment::WatchItem::kRules:
      return "rules";
    case Environment::WatchItem::kFacts:
      return "facts";
    case Environment::WatchItem::kGlobals:
      return "globals";
    case Environment::WatchItem::kCompilations:
      return "compilations";
    case Environment::WatchItem::kStatistics:
      return "statistics";
    case Environment::WatchItem::kDefFunctions:
      return "deffunctions";
    case Environment::WatchItem::kActivations:
      return "activations";
  }
}

const char kDefaultLogger[] = "__DefaultLogger__";
const char kMuteLogger[] = "__MuteLogger__";
const char kErrorLogger[] = "__ErrorLogger__";
const char kRedefineLogger[] = "__LoadRedefineError__";
const char kRedefineWarnPrefix[] =
    "[CSTRCPSR1] WARNING: Redefining deftemplate: ";
const char kRouterNameLogCallback[] = "__LOG_CALLBACK__";
const char kTraceLogger[] = "__RunWithTrace__";
const char kTraceFileLogger[] = "__TraceFile__";
}  // namespace

Environment::Environment()
    : function_facade_(std::make_unique<EnvironmentFunctionFacade>(this)),
      assert_facade_(std::make_unique<EnvironmentAssertFacade>(this)),
      readonly_facade_(std::make_unique<EnvironmentReadonlyFacade>(this)) {
  c_env_ = CreateEnvironment();

  absl::MutexLock lock(mutex_);
  InstallLogRouter();

  if (auto s = AddErrorLogCallback(); !s.ok()) {
    LOG(ERROR) << "Failed to add error log callback";
  }
}

Environment::~Environment() {
  {
    absl::MutexLock lock(mutex_);
    absl::Status close_trace_file_status = CloseTraceFileLog();
    if (!close_trace_file_status.ok()) {
      LOG(ERROR) << "Failed to stop tracing: " << close_trace_file_status;
    }
  }

  function_facade_.reset();
  assert_facade_.reset();
  readonly_facade_.reset();

  DestroyEnvironment(c_env_);
}

namespace {

absl::Status BatchEvaluateImpl(void* c_env, absl::string_view filename,
                               bool is_test) {
  std::string resolved_filename =
      is_test ? PathResolver::ResolveRunfilesPathForTest(filename)
              : PathResolver::ResolveRunfilesPath(filename);

  if (EnvBatchStar(c_env, resolved_filename.c_str()) == 0) {
    return absl::NotFoundError(
        absl::StrFormat("Could not open file '%s'", resolved_filename));
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status Environment::BatchEvaluate(absl::string_view filename) {
  return BatchEvaluateImpl(c_env_, filename, /*is_test=*/false);
}

absl::Status Environment::BatchEvaluateForTest(absl::string_view filename) {
  return BatchEvaluateImpl(c_env_, filename, /*is_test=*/true);
}

absl::Status Environment::SetRedefineDuringLoadIsError(bool is_error) {
  redefine_during_load_is_error_ = is_error;
  if (is_error) {
    INTR_RETURN_IF_ERROR(Watch(WatchItem::kCompilations));
    INTR_RETURN_IF_ERROR(AddLogCallback(
        kRedefineLogger,
        [this](Environment::LogLevel level, absl::string_view message) {
          if (level != Environment::LogLevel::kWarn) return;
          if (absl::StartsWith(message, kRedefineWarnPrefix)) {
            redefine_errors_.emplace_back(
                std::string(message.substr(strlen(kRedefineWarnPrefix))));
          }
        }));
  } else {
    INTR_RETURN_IF_ERROR(Unwatch(WatchItem::kCompilations));
    RemoveLogCallback(kRedefineLogger);
  }
  return absl::OkStatus();
}

absl::Status Environment::LoadRunfileImpl(absl::string_view filename,
                                          bool is_test) {
  std::string resolved_filename =
      is_test ? PathResolver::ResolveRunfilesPathForTest(filename)
              : PathResolver::ResolveRunfilesPath(filename);

  INTR_RETURN_IF_ERROR(file::Exists(resolved_filename, file::Defaults()));

  if (redefine_during_load_is_error_) {
    SetPrintWhileLoading(c_env_, TRUE);
  }
  int load_rv = EnvLoad(c_env_, resolved_filename.c_str());
  if (redefine_during_load_is_error_) {
    SetPrintWhileLoading(c_env_, FALSE);
  }
  if (load_rv == 0) {
    return absl::NotFoundError(
        absl::StrFormat("File '%s' could not be opened", resolved_filename));
  }
  if (load_rv == -1) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Error while parsing file '%s'", resolved_filename));
  }
  if (!redefine_errors_.empty()) {
    return absl::AlreadyExistsError(absl::StrFormat(
        "Failed to load %s: redefinition of [%s]", resolved_filename,
        absl::StrJoin(redefine_errors_, ", ")));
  }

  return absl::OkStatus();
}

absl::Status Environment::LoadRunfile(absl::string_view filename) {
  return LoadRunfileImpl(filename, /*is_test=*/false);
}

absl::Status Environment::LoadTestRunfile(absl::string_view filename) {
  return LoadRunfileImpl(filename, /*is_test=*/true);
}

absl::StatusOr<Values> Environment::Evaluate(
    const std::string& clips_expression) {
  DATA_OBJECT return_value;
  // result is whether EnvEval failed or succeeded, the return_value is what the
  // expression yielded, e.g., "(+ 1 1)" would yield the integer 2.
  int result = EnvEval(c_env_, clips_expression.c_str(), &return_value);
  if (result == 0) {
    return absl::AbortedError(
        absl::StrFormat("Failed to evaluate '%s'", clips_expression));
  }

  return internal::DataObjectToValues(&return_value);
}

absl::StatusOr<Values> Environment::EvaluateResult(
    const std::string& clips_expression) {
  INTR_ASSIGN_OR_RETURN(Values values, Evaluate(clips_expression));
  if (values.empty()) {
    return absl::InternalError(absl::StrFormat(
        "Evaluating %s did not return a result (was empty)", clips_expression));
  }
  Value success_val = values.front();
  INTR_ASSIGN_OR_RETURN(Symbol success, success_val.GetSymbol());
  if (success == Symbol::False()) {
    if (values.size() == 2) {
      return absl::AbortedError(values[1].ToString());
    }
    absl::Span<const Value> error_messages_values =
        absl::Span<const Value>(values).subspan(1);
    std::vector<std::string> error_messages;
    absl::c_transform(error_messages_values, std::back_inserter(error_messages),
                      [](const Value& val) { return val.ToString(); });
    std::string error_message =
        absl::StrFormat(
            "Got an unexpected number of values (%d) for a FALSE result, "
            "expected 2. The values are: ",
            values.size()) +
        absl::StrJoin(error_messages, " ");
    return absl::InternalError(error_message);
  }
  // CLIPS result must start with FALSE or TRUE
  if (success != Symbol::True()) {
    return absl::InternalError(absl::StrFormat(
        "Evaluating %s did not return a result (first entry was %s)",
        clips_expression, success_val.ToString()));
  }
  values.erase(values.begin());
  return values;
}

absl::StatusOr<Value> Environment::EvaluateExpectSingleReturn(
    const std::string& clips_expression) {
  INTR_ASSIGN_OR_RETURN(auto results, Evaluate(clips_expression));
  if (results.size() != 1) {
    return absl::InternalError(
        absl::StrCat("EvaluateExpectSingleReturn expected 1 values in Evaluate "
                     "result but got ",
                     results.size()));
  }
  return results[0];
}

std::vector<std::string> Environment::GetFactsAsStrings(
    absl::string_view template_name, bool include_identifier) const {
  std::vector<std::string> fact_strings;

  if (template_name.empty()) {
    fact_strings.reserve(GetNumberOfFacts(c_env_));
  }

  // nullptr: start a new retrieval of all facts
  void* fact_p = EnvGetNextFact(c_env_, nullptr);
  while (fact_p != nullptr) {
    Fact fact(const_cast<Environment*>(this), fact_p);
    if (template_name.empty() || template_name == fact.GetTemplateName()) {
      fact_strings.emplace_back(fact.DebugString(include_identifier));
    }
    fact_p = EnvGetNextFact(c_env_, fact_p);
  }
  return fact_strings;
}

std::vector<Fact> Environment::GetFacts(absl::string_view template_name) const {
  std::vector<Fact> facts;

  if (template_name.empty()) {
    facts.reserve(GetNumberOfFacts(c_env_));
  }

  // nullptr: start a new retrieval of all facts
  void* fact_p = EnvGetNextFact(c_env_, nullptr);
  while (fact_p != nullptr) {
    void* template_p = EnvFactDeftemplate(c_env_, fact_p);
    std::string fact_template = EnvGetDeftemplateName(c_env_, template_p);
    if (template_name.empty() || template_name == fact_template) {
      facts.emplace_back(const_cast<Environment*>(this), fact_p);
    }
    fact_p = EnvGetNextFact(c_env_, fact_p);
  }
  return facts;
}

std::vector<Fact> Environment::QueryFacts(
    absl::string_view template_name, absl::Span<const SlotValue> slots) const {
  std::vector<Fact> facts = GetFacts(template_name);
  facts.erase(std::remove_if(
                  facts.begin(), facts.end(),
                  [slots](const Fact& fact) { return !fact.Matches(slots); }),
              facts.end());
  return facts;
}

absl::StatusOr<Fact> Environment::GetUniqueFact(
    absl::string_view template_name, absl::Span<const SlotValue> slots) const {
  std::vector<Fact> facts = QueryFacts(template_name, slots);
  if (facts.empty()) {
    std::string slots_str;
    for (const SlotValue& sv : slots) {
      absl::StrAppend(&slots_str,
                      absl::StrFormat("'%s': '%s', ", sv.GetSlotName(),
                                      sv.GetSlotValue()
                                          .value_or(clips::Value("<no value>"))
                                          .ToString()));
    }
    return absl::NotFoundError(absl::StrFormat(
        "No %s fact found with slots: %s.", template_name, slots_str));
  }
  if (facts.size() != 1) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Exactly one %s fact should have been found, found %d.",
                        template_name, facts.size()));
  }
  return std::move(facts.front());
}

std::vector<Template> Environment::GetTemplates() const {
  std::vector<Template> templates;

  // nullptr: start a new retrieval of all templates
  void* template_p = EnvGetNextDeftemplate(c_env_, nullptr);
  while (template_p != nullptr) {
    templates.emplace_back(const_cast<Environment*>(this), template_p);
    template_p = EnvGetNextDeftemplate(c_env_, template_p);
  }
  return templates;
}

absl::StatusOr<Template> Environment::GetTemplate(
    const std::string& template_name) const {
  void* template_p = EnvFindDeftemplate(c_env_, template_name.c_str());
  if (template_p == nullptr) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Template '%s' does not exist", template_name));
  }
  return Template(const_cast<Environment*>(this), template_p);
}

std::vector<std::string> Environment::GetTemplateNames() const {
  DATA_OBJECT dataobj;
  EnvGetDeftemplateList(c_env_, &dataobj, /* module */ nullptr);
  return internal::FilterDataObjectStrings(&dataobj);
}

absl::Status Environment::RemoveTemplate(const Template& clips_template) {
  if (EnvUndeftemplate(c_env_, clips_template.GetCPtr()) == FALSE) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Template '%s' cannot be removed", clips_template.GetName()));
  }
  return absl::OkStatus();
}

absl::StatusOr<ValueOrValues> Environment::GetGlobal(
    const std::string& global_name) const {
  DATA_OBJECT dataobj;
  if (EnvGetDefglobalValue(c_env_, global_name.c_str(), &dataobj) == 0) {
    return absl::NotFoundError(
        absl::StrFormat("Global '%s' does not exist", global_name));
  }
  auto values = internal::DataObjectToValues(&dataobj);
  return values.size() == 1 ? ValueOrValues(values[0]) : ValueOrValues(values);
}

absl::Status Environment::SetGlobal(const std::string& global_name,
                                    const Value& value) const {
  DATA_OBJECT dataobj;
  if (!internal::ValueToDataObject(c_env_, value, &dataobj).ok()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Failed to set global '%s': cannot convert value to data object",
        global_name));
  }

  if (EnvSetDefglobalValue(c_env_, global_name.c_str(), &dataobj) == 0) {
    return absl::NotFoundError(
        absl::StrFormat("Global '%s' does not exist", global_name));
  }
  return absl::OkStatus();
}

absl::Status Environment::SetGlobal(const std::string& global_name,
                                    const Values& values) const {
  DATA_OBJECT dataobj;
  if (!internal::ValuesToDataObject(c_env_, values, &dataobj).ok()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Failed to set global '%s': cannot convert values to data object",
        global_name));
  }

  if (EnvSetDefglobalValue(c_env_, global_name.c_str(), &dataobj) == 0) {
    return absl::NotFoundError(
        absl::StrFormat("Global '%s' does not exist", global_name));
  }
  return absl::OkStatus();
}

absl::Status Environment::Build(const std::string& construct_string) {
  if (EnvBuild(c_env_, construct_string.c_str()) != 1) {
    return absl::InvalidArgumentError(
        absl::StrFormat("EnvBuild failed on '%s'", construct_string));
  }
  return absl::OkStatus();
}

absl::StatusOr<Fact> Environment::AssertFact(const std::string& fact_string) {
  void* c_fact = EnvAssertString(c_env_, fact_string.c_str());
  if (c_fact == nullptr) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Failed to create fact %s, invalid syntax or exists already?",
        fact_string.c_str()));
  }

  return Fact(this, c_fact);
}

absl::StatusOr<Fact> Environment::AssertFact(
    const std::string& template_name, absl::Span<const SlotValue> slots) {
  INTR_ASSIGN_OR_RETURN(Template fact_template, GetTemplate(template_name));
  return AssertFact(fact_template, slots);
}

absl::StatusOr<Fact> Environment::AssertFact(
    const Template& fact_template, absl::Span<const SlotValue> slots) {
  return Fact::AssertFact(this, fact_template, slots);
}

absl::StatusOr<Fact> Environment::ModifyFact(
    Fact&& fact, const std::vector<SlotValue>& modifications) {
  std::vector<SlotValue> slots_to_assert = modifications;
  // Also collect all slots from fact to assert
  for (const std::string& slot_name : fact.GetSlotNames()) {
    // Skip the slots that have been modified
    auto slot_is_modified = [&slot_name](const SlotValue& modified_slot) {
      return modified_slot.GetSlotName() == slot_name;
    };
    if (absl::c_find_if(modifications, slot_is_modified) !=
        modifications.end()) {
      continue;
    }
    if (fact.GetTemplate().IsSinglefieldSlot(slot_name)) {
      absl::StatusOr<clips::Value> slot_val = fact.GetSlotValue(slot_name);
      if (slot_val.ok()) {
        slots_to_assert.push_back(SlotValue(slot_name, *slot_val));
      }
    } else {
      absl::StatusOr<clips::Values> slot_vals = fact.GetSlotValues(slot_name);
      if (slot_vals.ok()) {
        slots_to_assert.push_back(SlotValue(slot_name, *slot_vals));
      }
    }
  }
  std::string template_name = fact.GetTemplateName();
  INTR_RETURN_IF_ERROR(RetractFact(std::move(fact)));

  return AssertFact(template_name, slots_to_assert);
}

absl::Status Environment::AssertFacts(
    absl::Span<const std::string> fact_strings) {
  for (const auto& fact_string : fact_strings) {
    INTR_RETURN_IF_ERROR(AssertFact(fact_string).status()).SetPrepend()
        << absl::StrFormat("Failed to assert %s", fact_string);
  }
  return absl::OkStatus();
}

absl::Status Environment::AssertFacts(
    absl::Span<const std::pair<const std::string, const std::vector<SlotValue>>>
        facts) {
  for (const auto& [template_name, slot_values] : facts) {
    INTR_RETURN_IF_ERROR(AssertFact(template_name, slot_values).status());
  }
  return absl::OkStatus();
}

absl::Status Environment::RetractFact(Fact&& fact) {
  if (EnvRetract(c_env_, fact.GetCPtr()) == FALSE) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Cannot retract fact %s (already retracted, or not asserted?)",
        fact.DebugString()));
  }
  return absl::OkStatus();
}

absl::Status Environment::RetractFacts(absl::string_view template_name,
                                       absl::Span<const SlotValue> slots)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  std::vector<Fact> facts = GetFacts(template_name);
  for (Fact& fact : facts) {
    if (fact.Matches(slots)) {
      INTR_RETURN_IF_ERROR(RetractFact(std::move(fact)));
    }
  }
  return absl::OkStatus();
}

void Environment::RefreshAgenda() { EnvRefreshAgenda(c_env_, nullptr); }

static int ClipsLogCallbackQuery(void* env, const char* logical_name) {
  return (strcmp(logical_name, "debug") == 0 ||
          strcmp(logical_name, "info") == 0 ||
          strcmp(logical_name, "warn") == 0 ||
          strcmp(logical_name, "internal") == 0 ||
          strcmp(logical_name, "error") == 0 ||
          strcmp(logical_name, "stdout") == 0 ||
          strcmp(logical_name, WDIALOG) == 0 ||
          strcmp(logical_name, WTRACE) == 0 ||
          strcmp(logical_name, WWARNING) == 0 ||
          strcmp(logical_name, WERROR) == 0 ||
          strcmp(logical_name, WDISPLAY) == 0)
             ? 1
             : 0;
}

static int ClipsLogCallbackExit(void* env, int exit_code) { return TRUE; }

static int ClipsLogCallbackPrint(void* env, const char* logical_name,
                                 const char* str) {
  void* context = GetEnvironmentRouterContext(env);
  auto* environment = static_cast<Environment*>(context);
  environment->mutex()->AssertHeld();
  Environment::LogLevel level = Environment::LogLevel::kInfo;
  if (strcmp(logical_name, WTRACE) == 0) {
    level = Environment::LogLevel::kTrace;
  } else if (strcmp(logical_name, "debug") == 0) {
    level = Environment::LogLevel::kDebug;
  } else if (strcmp(logical_name, "warn") == 0 ||
             strcmp(logical_name, WWARNING) == 0) {
    level = Environment::LogLevel::kWarn;
  } else if (strcmp(logical_name, "error") == 0 ||
             strcmp(logical_name, WERROR) == 0) {
    level = Environment::LogLevel::kError;
  } else if (strcmp(logical_name, "internal") == 0) {
    level = Environment::LogLevel::kInternal;
  } else if (strcmp(logical_name, WDIALOG) == 0) {
    level = Environment::LogLevel::kClips;
  }
  environment->LogText(level, str);
  return TRUE;
}

void Environment::InstallLogRouter() {
  int rv = EnvAddRouterWithContext(
      c_env_, kRouterNameLogCallback,
      /* priority */ 30, ClipsLogCallbackQuery, ClipsLogCallbackPrint,
      /* getc func */ nullptr,
      /* ungetc func */ nullptr, ClipsLogCallbackExit, this);
  LOG_IF(ERROR, !rv) << "Failed to install CLIPS log router";
}

absl::Status Environment::AddLogCallback(absl::string_view name,
                                         Environment::LogCallback callback) {
  auto [cb_iterator, ok] = log_callbacks_.emplace(name, callback);
  if (!ok) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Logger '%s' already registered", name));
  }

  return absl::OkStatus();
}

void Environment::RemoveLogCallback(absl::string_view name) {
  auto cb_iterator = log_callbacks_.find(name);
  if (cb_iterator == log_callbacks_.end()) {
    return;
  }

  log_callbacks_.erase(cb_iterator->first);
}

void Environment::LogText(Environment::LogLevel level, const char* str) {
  const auto len = strlen(str);
  if (len == 0) {
    return;
  }
  bool flush = false;

  if (str[len - 1] == '\n') {
    flush = true;
    log_buffers_[level].append(str, len - 1);
  } else {
    log_buffers_[level].append(str);
  }

  if (flush) {
    if (log_callbacks_.empty() && !no_log_callback_warning_printed_) {
      LOG(WARNING) << "Received CLIPS Environment log message "
                   << "but no callback registered "
                   << "(this warning will print only once)";
      no_log_callback_warning_printed_ = true;
    }
    for (const auto& [name, log_callback] : log_callbacks_) {
      log_callback(level, log_buffers_[level]);
    }
    log_buffers_[level].clear();
  }
}

absl::Status Environment::AddDefaultLogCallback(bool verbose) {
  RemoveLogCallback(kErrorLogger);
  RemoveLogCallback(kMuteLogger);
  if (verbose) {
    INTR_RETURN_IF_ERROR(AddLogCallback(
        kDefaultLogger,
        [](Environment::LogLevel level, absl::string_view message) {
          switch (level) {
            case LogLevel::kDebug:
              LOG_LINES(INFO, message);
              break;
            case LogLevel::kTrace:
              LOG_LINES(INFO, message);
              break;
            case LogLevel::kInfo:
              LOG_LINES(INFO, message);
              break;
            case LogLevel::kWarn:
              LOG_LINES(WARNING, message);
              break;
            case LogLevel::kError:
              LOG_LINES(ERROR, message);
              break;
            case LogLevel::kClips:
              [[fallthrough]];
            case LogLevel::kInternal:
              break;  // ignore internal printing
          }
        }));
  } else {
    INTR_RETURN_IF_ERROR(AddLogCallback(
        kDefaultLogger,
        [](Environment::LogLevel level, absl::string_view message) {
          switch (level) {
            case LogLevel::kDebug:
              VLOG_LINES(1, message);
              break;
            case LogLevel::kTrace:
              VLOG_LINES(2, message);
              break;
            case LogLevel::kInfo:
              LOG_LINES(INFO, message);
              break;
            case LogLevel::kWarn:
              LOG_LINES(WARNING, message);
              break;
            case LogLevel::kError:
              LOG_LINES(ERROR, message);
              break;
            case LogLevel::kClips:
              [[fallthrough]];
            case LogLevel::kInternal:
              break;  // ignore internal printing
          }
        }));
  }
  return absl::OkStatus();
}

absl::Status Environment::AddMuteLogCallback() {
  RemoveLogCallback(kDefaultLogger);
  RemoveLogCallback(kErrorLogger);
  INTR_RETURN_IF_ERROR(AddLogCallback(
      kMuteLogger, [](Environment::LogLevel, absl::string_view) {}));
  return absl::OkStatus();
}

absl::Status Environment::AddErrorLogCallback() {
  RemoveLogCallback(kDefaultLogger);
  RemoveLogCallback(kMuteLogger);
  INTR_RETURN_IF_ERROR(AddLogCallback(
      kErrorLogger, [](Environment::LogLevel level, absl::string_view message) {
        switch (level) {
          case LogLevel::kWarn:
            LOG_LINES(WARNING, message);
            break;
          case LogLevel::kError:
            LOG_LINES(ERROR, message);
            break;
          default:
            break;  // ignore anything else
        }
      }));
  return absl::OkStatus();
}

absl::StatusOr<std::string> Environment::StartTraceFileLog(
    absl::string_view logfile_name, bool always_flush) {
  if (clips_trace_file_ != nullptr) {
    return absl::AlreadyExistsError(
        absl::StrFormat("Tracing already started."));
  }

  // Enable these watches here to get a complete log of all events that
  // assert or retract facts and if applicable the rules that caused this.
  INTR_RETURN_IF_ERROR(Watch(WatchItem::kRules));
  INTR_RETURN_IF_ERROR(Watch(WatchItem::kFacts));
  INTR_RETURN_IF_ERROR(Watch(WatchItem::kGlobals));

  std::string date_time_str =
      absl::FormatTime("%Y%m%d-%H%M%S", absl::Now(), absl::LocalTimeZone());

  std::string logfile_name_processed;
  if (!absl::StrContains(logfile_name, "%count%")) {
    // Don't check for existence (if file exists, overwrite it).
    logfile_name_processed =
        absl::StrReplaceAll(logfile_name, {{"%timestamp%", date_time_str}});
  } else {
    for (int i = 1; i < 1000; ++i) {
      logfile_name_processed = absl::StrReplaceAll(
          logfile_name, {{"%timestamp%", date_time_str},
                         {"%count%", absl::StrFormat("%03d", i)}});

      // Note that this file::Exists() check has a potential race condition
      // with the file::Open() call below (someone else could create the file in
      // between). However, this is not considered a problem since this method
      // requires a lock on the environment mutex.
      absl::Status exists_status =
          file::Exists(logfile_name_processed, file::Defaults());

      if (exists_status.ok()) {
        // File i already exists, try the next number.
        logfile_name_processed.clear();
        continue;
      } else if (absl::IsInvalidArgument(exists_status)) {
        // File i does not exist, create and use it.
        break;
      } else {
        // Other error while checking for file existence, return error.
        return exists_status;
      }
    }
    if (logfile_name_processed.empty()) {
      return absl::AlreadyExistsError(absl::StrFormat(
          "All files for pattern '%s' already exist", logfile_name));
    }
  }

  INTR_RETURN_IF_ERROR(file::Open(logfile_name_processed, "w",
                                  &clips_trace_file_, file::Defaults()));
  clips_trace_file_position_ = 0;

  LOG(INFO) << absl::StrFormat(R"(Opened CLIPS trace log file "%s" as "%s")",
                               logfile_name, logfile_name_processed);

  INTR_RETURN_IF_ERROR(AddTraceFileLogLine("TraceLog: Begin"));
  INTR_RETURN_IF_ERROR(FlushTraceFileLog());
  INTR_RETURN_IF_ERROR(AddLogCallback(
      kTraceFileLogger,
      [this, always_flush](LogLevel level, absl::string_view message) {
        if (level != LogLevel::kTrace) return;
        INTR_RETURN_IF_ERROR(AddTraceFileLogLine(message))
            .LogWarning()
            .With(ReturnVoid());
        if (always_flush) {
          INTR_RETURN_IF_ERROR(FlushTraceFileLog())
              .LogWarning()
              .With(ReturnVoid());
        }
      }));
  return logfile_name_processed;
}

absl::StatusOr<std::string> Environment::GetTraceFileLogName() {
  if (clips_trace_file_ == nullptr) {
    return absl::FailedPreconditionError("Tracing has not been started.");
  }
  return std::string(clips_trace_file_->filename());
}

absl::StatusOr<uint64_t> Environment::GetTraceFilePosition() {
  if (clips_trace_file_ == nullptr) {
    return absl::FailedPreconditionError("No clips_trace_file_ is open.");
  }
  return clips_trace_file_position_;
}

absl::StatusOr<std::string> Environment::GetTraceFileContents(
    uint64_t start_offset) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  if (clips_trace_file_ == nullptr) {
    return absl::FailedPreconditionError("No clips_trace_file_ is open.");
  }

  int64_t read_size = static_cast<int64_t>(clips_trace_file_position_) -
                      static_cast<int64_t>(start_offset);
  if (read_size <= 0) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "requested start offset %d must be smaller than the current size "
        "%d of the trace file.",
        start_offset, clips_trace_file_position_));
  }

  INTR_RETURN_IF_ERROR(FlushTraceFileLog());

  FILE* file = fopen(clips_trace_file_->filename().data(), "r");
  if (!file) {
    return absl::InternalError(
        absl::StrFormat("Failed to open trace file '%s' for reading.",
                        clips_trace_file_->filename()));
  }

  if (fseek(file, start_offset, SEEK_SET) != 0) {
    fclose(file);
    return absl::InternalError(
        absl::StrFormat("Failed to seek trace file '%s' to offset %d.",
                        clips_trace_file_->filename(), start_offset));
  }

  std::string file_contents(read_size, '\0');
  size_t actually_read = fread(file_contents.data(), 1, read_size, file);
  fclose(file);

  if (actually_read == 0) {
    return absl::InternalError(
        absl::StrFormat("Failed to read trace file '%s' contents.",
                        clips_trace_file_->filename()));
  } else if (actually_read < read_size) {
    LOG(ERROR) << absl::StrFormat(
        "Incomplete log read: only %d bytes of %d expected read", actually_read,
        read_size);
  }

  file_contents.resize(actually_read);
  return file_contents;
}

absl::Status Environment::AddTraceFileLogLine(absl::string_view log_message) {
  // Format like 15:30:35.312354
  std::string time_str =
      absl::FormatTime("%H:%M:%E6S ", absl::Now(), absl::LocalTimeZone());
  constexpr std::string_view kNewLine = "\n";

  INTR_RETURN_IF_ERROR(
      file::WriteString(clips_trace_file_, time_str, file::Defaults()));
  INTR_RETURN_IF_ERROR(
      file::WriteString(clips_trace_file_, log_message, file::Defaults()));
  INTR_RETURN_IF_ERROR(
      file::WriteString(clips_trace_file_, kNewLine, file::Defaults()));

  clips_trace_file_position_ +=
      time_str.size() + log_message.size() + kNewLine.size();

  return absl::OkStatus();
}

absl::Status Environment::FlushTraceFileLog() {
  if (clips_trace_file_ == nullptr) {
    return absl::OkStatus();
  }
  if (clips_trace_file_->Flush()) {
    return absl::OkStatus();
  }
  return absl::InternalError("Failed to flush trace file.");
}

absl::Status Environment::CloseTraceFileLog()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  if (clips_trace_file_ == nullptr) {
    return absl::OkStatus();
  }
  RemoveLogCallback(kTraceFileLogger);

  AddTraceFileLogLine("TraceLog: End").IgnoreError();
  FlushTraceFileLog().IgnoreError();
  INTR_RETURN_IF_ERROR(clips_trace_file_->Close(file::Defaults()));
  clips_trace_file_ = nullptr;
  clips_trace_file_position_ = 0;
  return absl::OkStatus();
}

void Environment::Reset() { EnvReset(c_env_); }

absl::Status Environment::Watch(WatchItem item) {
  std::string item_name = WatchItemToString(item);
  if (EnvWatch(c_env_, item_name.c_str()) == 0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Failed to watch item '%s'", item_name));
  }
  return absl::OkStatus();
}

absl::Status Environment::Unwatch(WatchItem item) {
  std::string item_name = WatchItemToString(item);
  if (EnvUnwatch(c_env_, item_name.c_str()) == 0) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Failed to unwatch item '%s'", item_name));
  }
  return absl::OkStatus();
}

absl::Status Environment::Watch(WatchItem item, absl::string_view item_name) {
  std::string item_str = WatchItemToString(item);
  return Evaluate(absl::StrFormat("(watch %s %s)", item_str, item_name))
      .status();
}

absl::Status Environment::Unwatch(WatchItem item, absl::string_view item_name) {
  std::string item_str = WatchItemToString(item);
  return Evaluate(absl::StrFormat("(unwatch %s %s)", item_str, item_name))
      .status();
}

int64_t Environment::Run(int64_t run_limit) {
  if (clips_trace_file_ != nullptr) {
    if (absl::Status s = AddTraceFileLogLine("TraceLog: LoopStart"); !s.ok()) {
      LOG(WARNING) << "TraceLog failed at loop start: " << s;
    }
  }
  auto run_return = EnvRun(c_env_, run_limit);
  if (clips_trace_file_ != nullptr) {
    if (absl::Status s = AddTraceFileLogLine("TraceLog: LoopEnd"); !s.ok()) {
      LOG(WARNING) << "TraceLog failed at loop end: " << s;
    }
  }
  return run_return;
}

absl::StatusOr<Trace> Environment::RunWithTracing(int64_t run_limit) {
  INTR_RETURN_IF_ERROR(Watch(WatchItem::kRules));
  INTR_RETURN_IF_ERROR(Watch(WatchItem::kFacts));
  INTR_RETURN_IF_ERROR(Watch(WatchItem::kGlobals));

  Trace trace(this);
  INTR_RETURN_IF_ERROR(AddLogCallback(
      kTraceLogger, [&trace](LogLevel level, absl::string_view message) {
        if (level != LogLevel::kTrace) return;
        trace.AddLogLine(message);
      }));
  Run(run_limit);
  RemoveLogCallback(kTraceLogger);
  return trace;
}

absl::Status Environment::RemoveFunction(const std::string& name) {
  function_callbacks_.erase(name);
  if (UndefineFunction(c_env_, name.c_str()) != TRUE) {
    return absl::InternalError(
        absl::StrFormat("Failed to remove function %s from CLIPS", name));
  }
  return absl::OkStatus();
}

bool Environment::HasFunction(const std::string& name) const {
  return (FindFunction(c_env_, name.c_str()) != nullptr);
}

absl::Status Environment::SetRunnerCallback(std::function<void()> callback) {
  if (notify_runner_callback_.has_value()) {
    return absl::AlreadyExistsError(
        "CLIPS environment runner has already been set.");
  }
  notify_runner_callback_ = callback;
  return absl::OkStatus();
}

void Environment::NotifyRunner() {
  if (!notify_runner_callback_.has_value()) return;
  (*notify_runner_callback_)();
}

void Environment::DebugPrintAllFacts(std::ostream* stream) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  *stream << absl::StrJoin(GetFactsAsStrings(), "\n");
}

void Environment::DebugPrintAllFacts(absl::string_view template_name,
                                     std::ostream* stream) const
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_) {
  *stream << absl::StrJoin(GetFactsAsStrings(template_name), "\n");
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
