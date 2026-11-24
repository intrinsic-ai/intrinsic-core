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

#include "intrinsic/executive/clips_cpp/trace.h"

#include <cstdint>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "absl/base/log_severity.h"
#include "absl/log/log.h"
#include "absl/log/log_streamer.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/clips_snapshot.pb.h"
#include "re2/re2.h"

namespace intrinsic {
namespace executive {
namespace clips {
namespace {
// LINT.IfChange(clips_trace_regex)
// Output like: FIRE    1 test_rule: f-1
const char kPatternRuleFire[] = "FIRE\\s+(\\d+) ([^:]+): ([f0-9,*-]+)";
// Output like: <== f-1     (one)
const char kPatternFactRetract[] = "<== f-(\\d+)\\s+(.+)";
// Output like: ==> f-2     (two)
const char kPatternFactAssert[] = "==> f-(\\d+)\\s+(.+)";
// Output like: :== ?*GLOBAL-INT* ==> 2 <== 1
// Output like: :== ?*GLOBAL-STR* ==> "bar" <== "foo"
// Output like: :== ?*GLOBAL-SYM* ==> BARSYM <== FOOSYM
const char kPatternGlobalVar[] = ":== \\?\\*([^*]+)\\* ==> (.+) <== (.+)";
// LINT.ThenChange(
//     //intrinsic_runtime/intrinsic/executive/clips_cpp/parse_clips_trace_log.py:clips_trace_regex)
// )

Value StringToValue(std::string_view str) {
  // TODO(b/213855172): include support for MULTIFIELD, i.e., lists of values
  int64_t integer_value;
  double double_value;
  if (absl::SimpleAtoi(str, &integer_value)) {
    return Value(integer_value);
  }
  if (absl::SimpleAtod(str, &double_value)) {
    return Value(double_value);
  }
  if (str.empty()) {
    return Value("");
  }
  if (str[0] == '"') {
    return Value(str.substr(1, str.length() - 2));
  }
  return Value(str, Value::Type::kSymbol);
}
}  // namespace

Trace::Trace(Environment* env) : env_(env) {}

void Trace::AddLogLine(std::string_view line) {
  if (line.length() < 4) return;
  std::string_view indicator = line.substr(0, 4);
  if (indicator == "FIRE") {
    rules_fired_count_ += 1;
  }

  log_messages_.push_back(std::string(line));
}

void Trace::EnsureLogMessagesParsed() const {
  // Although the const_casts suggest the non-constness
  // of this function, we are not changing the nature
  // of the Trace, only transferring where the information
  // is stored.
  for (const auto& log_message : log_messages_) {
    const_cast<Trace*>(this)->ParseTraceLine(log_message);
  }
  const_cast<Trace*>(this)->log_messages_.clear();
}

void Trace::ParseTraceLine(std::string_view line) {
  // LazyRE2 are ok for static initialization, cf. //third_party/re2/re2.h:
  // "Helper for writing global or static RE2s safely."
  static LazyRE2 re_rule_fire = {kPatternRuleFire};
  static LazyRE2 re_fact_assert = {kPatternFactAssert};
  static LazyRE2 re_fact_retract = {kPatternFactRetract};
  static LazyRE2 re_global_var = {kPatternGlobalVar};

  if (line.length() < 4) return;

  std::string_view indicator = line.substr(0, 4);

  if (indicator == "FIRE") {
    int seqnum;
    std::string rule_name;
    std::string facts;
    if (RE2::FullMatch(line, *re_rule_fire, &seqnum, &rule_name, &facts)) {
      entries_.emplace_back(
          RuleFireEntry(seqnum, std::move(rule_name), std::move(facts), line));
    }
  } else if (indicator == "==> ") {
    int64_t fact_index;
    std::string fact_string;
    if (RE2::FullMatch(line, *re_fact_assert, &fact_index, &fact_string)) {
      entries_.emplace_back(FactEntry(FactEntry::Op::kAssert, fact_index,
                                      std::move(fact_string), line));
    }
  } else if (indicator == "<== ") {
    int64_t fact_index;
    std::string fact_string;
    if (RE2::FullMatch(line, *re_fact_retract, &fact_index, &fact_string)) {
      entries_.emplace_back(FactEntry(FactEntry::Op::kRetract, fact_index,
                                      std::move(fact_string), line));
    }
  } else if (indicator == ":== ") {
    std::string global_name;
    std::string new_value;
    std::string old_value;
    if (RE2::FullMatch(line, *re_global_var, &global_name, &new_value,
                       &old_value)) {
      entries_.emplace_back(GlobalVarEntry(std::move(global_name),
                                           StringToValue(new_value),
                                           StringToValue(old_value)));
    } else {
      LOG(WARNING) << "Failed to parse " << line;
    }
  }
}

void Trace::Print(std::ostream* ostream) const {
  EnsureLogMessagesParsed();
  for (const Entry& e : entries_) {
    PrintTraceEntry(e, ostream);
  }
}

void Trace::PrintLineWise(absl::LogSeverity severity, absl::string_view file,
                          int line) const {
  EnsureLogMessagesParsed();
  for (const Entry& e : entries_) {
    PrintTraceEntry(e, &absl::LogStreamer(severity, file, line).stream());
  }
}

void Trace::PrintTraceEntry(const Entry& entry, std::ostream* ostream) const {
  if (std::holds_alternative<RuleFireEntry>(entry)) {
    const auto& rule_entry = std::get<RuleFireEntry>(entry);
    *ostream << "Rule   : " << rule_entry.rule_name << " fired due to "
             << rule_entry.facts_string << "\n";
  } else if (std::holds_alternative<FactEntry>(entry)) {
    const auto& fact_entry = std::get<FactEntry>(entry);
    *ostream << absl::StrFormat(
        "%s: f-%li Fact: %s\n",
        fact_entry.op == FactEntry::Op::kAssert ? "Assert " : "Retract",
        fact_entry.fact_index, fact_entry.fact_string);
  } else if (std::holds_alternative<GlobalVarEntry>(entry)) {
    const auto& global_entry = std::get<GlobalVarEntry>(entry);
    *ostream << absl::StrFormat(
        "GlobalVar: ?*%s* = %s (was %s)\n", global_entry.global_name,
        global_entry.new_value.ToString(), global_entry.old_value.ToString());
  }
}

void Trace::IterateView::Reset() { iteration_pos_ = -1; }

bool Trace::IterateView::Next() {
  if (iteration_pos_ < 0) {
    if (trace_->GetEntries().empty()) {
      return false;
    }
    iteration_pos_ = 0;
  } else {
    if (iteration_pos_ >= trace_->GetEntries().size() - 1) {
      iteration_pos_ = trace_->GetEntries().size();
      return false;
    }
    iteration_pos_ += 1;
  }
  return true;
}

absl::StatusOr<Trace::Entry> Trace::IterateView::Current() const {
  if (iteration_pos_ < 0) {
    return absl::OutOfRangeError("Trace iteration not started");
  }
  if (iteration_pos_ >= trace_->GetEntries().size()) {
    return absl::OutOfRangeError("Trace iteration has ended");
  }
  return trace_->GetEntries().at(iteration_pos_);
}

std::ostream& operator<<(std::ostream& os, const Trace& trace) {
  trace.Print(&os);
  return os;
}

intrinsic_proto::executive::ClipsTrace Trace::ToProto() const {
  EnsureLogMessagesParsed();

  intrinsic_proto::executive::ClipsTrace proto;

  for (const auto& e : entries_) {
    if (std::holds_alternative<RuleFireEntry>(e)) {
      auto* entry = proto.add_entries()->mutable_rule_fired_entry();
      const auto& cpp_entry = std::get<RuleFireEntry>(e);
      entry->set_sequence_number(cpp_entry.sequence_number);
      entry->set_rule_name(cpp_entry.rule_name);
      entry->set_facts_string(cpp_entry.facts_string);
      entry->set_line(cpp_entry.line);
    } else if (std::holds_alternative<FactEntry>(e)) {
      auto* entry = proto.add_entries()->mutable_fact_entry();
      const auto& cpp_entry = std::get<FactEntry>(e);
      switch (cpp_entry.op) {
        case FactEntry::Op::kDontCare:
          entry->set_op(
              intrinsic_proto::executive::ClipsTrace::FactEntry::OP_UNSET);
          break;
        case FactEntry::Op::kAssert:
          entry->set_op(
              intrinsic_proto::executive::ClipsTrace::FactEntry::OP_ASSERT);
          break;
        case FactEntry::Op::kRetract:
          entry->set_op(
              intrinsic_proto::executive::ClipsTrace::FactEntry::OP_RETRACT);
          break;
        default:
          LOG(ERROR) << "Unknown Op type in trace";
      }
      entry->set_fact_index(cpp_entry.fact_index);
      entry->set_fact_string(cpp_entry.fact_string);
      entry->set_line(cpp_entry.line);
    } else if (std::holds_alternative<GlobalVarEntry>(e)) {
      auto* entry = proto.add_entries()->mutable_global_var_entry();
      const auto& cpp_entry = std::get<GlobalVarEntry>(e);
      entry->set_global_name(cpp_entry.global_name);
      entry->set_new_value(cpp_entry.new_value.ToString());
      entry->set_old_value(cpp_entry.old_value.ToString());
    }
  }

  std::stringstream trace_stream;
  Print(&trace_stream);
  proto.set_human_readable(trace_stream.str());
  return proto;
}

std::string Trace::ToString() const {
  std::stringstream trace_stream;
  Print(&trace_stream);
  return trace_stream.str();
}

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic
