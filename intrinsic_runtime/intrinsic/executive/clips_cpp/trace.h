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

#ifndef INTRINSIC_EXECUTIVE_CLIPS_CPP_TRACE_H_
#define INTRINSIC_EXECUTIVE_CLIPS_CPP_TRACE_H_

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/log_severity.h"
#include "absl/log/log.h"
#include "absl/log/log_streamer.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/executive/proto/clips_snapshot.pb.h"

namespace intrinsic {
namespace executive {
namespace clips {

class Environment;

// This class collects traces for Environment::RunWithTracing().
// It parses relevant trace log lines and converts them into accessible entries.
// It provides an iterator to walk the traces to analyze them for expectations.
// Commonly used with trace matcher testing utilities, but could also be used
// otherwise.
class Trace {
 public:
  struct RuleFireEntry {
   public:
    RuleFireEntry(int sequence_number, std::string&& rule_name,
                  std::string&& facts_string, std::string_view line)
        : sequence_number(sequence_number),
          rule_name(rule_name),
          facts_string(facts_string),
          line(line) {}

    bool operator==(const RuleFireEntry& other) const {
      return (sequence_number == other.sequence_number &&
              rule_name == other.rule_name &&
              facts_string == other.facts_string && line == other.line);
    }

    int sequence_number;
    std::string rule_name;
    std::string facts_string;
    std::string line;
  };

  struct FactEntry {
   public:
    enum class Op {
      kDontCare,  // needed when filtering a trace for fact matches
      kAssert,
      kRetract,
    };
    FactEntry(Op op, int64_t index, std::string&& fact_string,
              std::string_view line)
        : op(op),
          fact_index(index),
          fact_string(std::move(fact_string)),
          line(line) {}

    bool operator==(const FactEntry& other) const {
      return (op == other.op && fact_index == other.fact_index &&
              fact_string == other.fact_string && line == other.line);
    }

    Op op;
    int64_t fact_index;
    std::string fact_string;
    std::string line;
  };

  struct GlobalVarEntry {
   public:
    GlobalVarEntry(std::string&& global_name, const Value& new_value,
                   const Value& old_value)
        : global_name(std::move(global_name)),
          new_value(new_value),
          old_value(old_value) {}

    bool operator==(const GlobalVarEntry& other) const {
      return (global_name == other.global_name &&
              new_value == other.new_value && old_value == other.old_value);
    }

    std::string global_name;
    Value new_value;
    Value old_value;
  };
  using Entry = std::variant<RuleFireEntry, FactEntry, GlobalVarEntry>;

  // Class to iterate over a trace. The trace is not modified and any number
  // of iterate views can be created. Using a view avoids copying of a trace
  // which can potentially contain quite some data.
  class IterateView {
   public:
    explicit IterateView(Trace* trace) : trace_(trace), iteration_pos_(-1) {}

    void Reset();
    bool Next();
    absl::StatusOr<Entry> Current() const;
    Environment* GetEnvironment() const { return trace_->GetEnvironment(); }

   private:
    Trace* trace_;
    int64_t iteration_pos_;
  };

  explicit Trace(Environment* env);

  void AddLogLine(std::string_view line);
  void Print(std::ostream* stream = &absl::LogInfoStreamer(__builtin_FILE(),
                                                           __builtin_LINE())
                                         .stream()) const;
  void PrintLineWise(absl::LogSeverity severity = absl::LogSeverity::kInfo,
                     absl::string_view file = __builtin_FILE(),
                     int line = __builtin_LINE()) const;
  intrinsic_proto::executive::ClipsTrace ToProto() const;
  std::string ToString() const;

  size_t CountRulesFired() const { return rules_fired_count_; }
  const std::vector<Entry>& GetEntries() const {
    EnsureLogMessagesParsed();
    return entries_;
  }

  Environment* GetEnvironment() const { return env_; }
  IterateView CreateIterateView() const {
    EnsureLogMessagesParsed();
    return IterateView(const_cast<Trace*>(this));
  }

 private:
  void EnsureLogMessagesParsed() const;
  void ParseTraceLine(std::string_view line);
  void PrintTraceEntry(const Entry& entry, std::ostream* ostream) const;

 private:
  Environment* env_;
  std::vector<std::string> log_messages_;
  std::vector<Entry> entries_;
  size_t rules_fired_count_ = 0;
};

std::ostream& operator<<(std::ostream& os, const Trace& trace);

}  // namespace clips
}  // namespace executive
}  // namespace intrinsic

#endif  // INTRINSIC_EXECUTIVE_CLIPS_CPP_TRACE_H_
