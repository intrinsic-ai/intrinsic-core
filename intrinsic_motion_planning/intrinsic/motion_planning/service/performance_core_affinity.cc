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

#include "intrinsic/motion_planning/service/performance_core_affinity.h"

#include <pthread.h>
#include <sched.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread_options.h"
#include "intrinsic/util/thread/thread_utils.h"
#include "intrinsic/util/thread/util.h"

namespace intrinsic {

namespace {

// Maximum valid CPU index supported by `cpu_set_t` and `sched_getaffinity()`.
constexpr int kMaxCpuIndex = CPU_SETSIZE - 1;

// Reads the full contents of `path`. Returns `absl::NotFoundError` if `path`
// does not exist, `absl::PermissionDeniedError` if access is denied, or
// `absl::InternalError` on other I/O failures.
absl::StatusOr<std::string> ReadSmallTextFile(const absl::string_view path) {
  std::ifstream input_stream{std::string(path)};
  if (!input_stream.is_open()) {
    const int open_errno = errno;
    if (open_errno == ENOENT) {
      return absl::NotFoundError(absl::StrCat("File not found: ", path));
    }
    if (open_errno == EACCES) {
      return absl::PermissionDeniedError(
          absl::StrCat("Permission denied reading file: ", path));
    }
    return absl::InternalError(absl::StrCat("Failed to open file '", path,
                                            "': ", std::strerror(open_errno)));
  }
  std::ostringstream buffer;
  buffer << input_stream.rdbuf();
  if (input_stream.bad()) {
    return absl::InternalError(absl::StrCat("Failed to read file '", path,
                                            "': ", std::strerror(errno)));
  }
  return buffer.str();
}

// Queries the calling thread's current allowed CPU set via
// `sched_getaffinity()`. Returns `absl::InternalError` if the syscall fails.
absl::StatusOr<absl::flat_hash_set<int>> GetCurrentAllowedCpus() {
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  if (sched_getaffinity(/*pid=*/0, sizeof(cpu_set_t), &cpu_set) != 0) {
    return absl::InternalError(
        absl::StrCat("sched_getaffinity failed: ", std::strerror(errno)));
  }
  absl::flat_hash_set<int> allowed_cpus;
  for (int cpu_index = 0; cpu_index < CPU_SETSIZE; ++cpu_index) {
    if (CPU_ISSET(cpu_index, &cpu_set)) {
      allowed_cpus.insert(cpu_index);
    }
  }
  return allowed_cpus;
}

// Parses a single cpulist `token` (either a single index like `"4"` or an
// inclusive range like `"0-7"`) into `{range_start, range_end}`.
// Returns `absl::InvalidArgumentError` on malformed or out-of-range entries.
absl::StatusOr<std::pair<int, int>> ParseCpuRangeToken(
    const absl::string_view token) {
  const size_t dash_position = token.find('-');
  if (dash_position == absl::string_view::npos) {
    int cpu_index = -1;
    const bool is_valid_cpu = absl::SimpleAtoi(token, &cpu_index) &&
                              cpu_index >= 0 && cpu_index <= kMaxCpuIndex;
    if (!is_valid_cpu) {
      return absl::InvalidArgumentError(
          absl::StrCat("Invalid CPU index '", token, "' in CPU list."));
    }
    return std::make_pair(cpu_index, cpu_index);
  }

  // Ensure there is only a single `'-'` in a range token.
  const bool has_multiple_dashes =
      token.find('-', dash_position + 1) != absl::string_view::npos;
  if (has_multiple_dashes) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid CPU range '", token, "' in CPU list."));
  }

  int range_start = -1;
  int range_end = -1;
  const bool is_valid_start =
      absl::SimpleAtoi(token.substr(0, dash_position), &range_start);
  const bool is_valid_end =
      absl::SimpleAtoi(token.substr(dash_position + 1), &range_end);
  const bool is_valid_range = is_valid_start && is_valid_end &&
                              range_start >= 0 && range_end >= range_start &&
                              range_end <= kMaxCpuIndex;
  if (!is_valid_range) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid CPU range '", token, "' in CPU list."));
  }
  return std::make_pair(range_start, range_end);
}

// Parses a Linux cpulist string `cpu_list` (e.g. `"0-7"` or `"0-3,8,10-11"`)
// into sorted unique CPU indices. Returns `absl::InvalidArgumentError` on
// malformed or out-of-range entries.
absl::StatusOr<std::vector<int>> ParseCpuList(
    const absl::string_view cpu_list) {
  const absl::string_view trimmed = absl::StripAsciiWhitespace(cpu_list);
  if (trimmed.empty()) {
    return absl::InvalidArgumentError("CPU list is empty.");
  }

  std::set<int> unique_cpus;
  for (const absl::string_view raw_token : absl::StrSplit(trimmed, ',')) {
    const absl::string_view token = absl::StripAsciiWhitespace(raw_token);
    if (token.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Empty token in CPU list: '", cpu_list, "'."));
    }

    INTR_ASSIGN_OR_RETURN(const auto cpu_range, ParseCpuRangeToken(token));
    for (int cpu_index = cpu_range.first; cpu_index <= cpu_range.second;
         ++cpu_index) {
      unique_cpus.insert(cpu_index);
    }
  }

  return std::vector<int>(unique_cpus.begin(), unique_cpus.end());
}

}  // namespace

absl::StatusOr<std::optional<std::vector<int>>> SelectPerformanceCoresToPin(
    const absl::flat_hash_set<int>& current_allowed_cpus,
    const absl::string_view performance_cpus_path,
    const absl::string_view cmdline_path, const int min_performance_cores) {
  if (min_performance_cores < 1) {
    return absl::InvalidArgumentError(
        absl::StrCat("min_performance_cores must be at least 1, got ",
                     min_performance_cores, "."));
  }

  const absl::StatusOr<std::string> performance_cpus_text =
      ReadSmallTextFile(performance_cpus_path);
  if (absl::IsNotFound(performance_cpus_text.status())) {
    return std::nullopt;
  }
  INTR_RETURN_IF_ERROR(performance_cpus_text.status());

  INTR_ASSIGN_OR_RETURN(const std::vector<int> performance_cpus,
                        ParseCpuList(*performance_cpus_text));

  // Exclude any real-time isolated cores (`rcu_nocbs`) as an extra safeguard
  // alongside `current_allowed_cpus`. Non-OK statuses are treated as an empty
  // set because `ReadCpuAffinitySetFromCommandLine()` returns
  // `FailedPreconditionError` on non-RT kernels where `rcu_nocbs` is absent,
  // and `NotFoundError` when `/proc/cmdline` is unavailable in containers.
  const absl::flat_hash_set<int> realtime_cores =
      ReadCpuAffinitySetFromCommandLine(cmdline_path)
          .value_or(absl::flat_hash_set<int>{});

  std::vector<int> allowed_performance_cpus;
  allowed_performance_cpus.reserve(performance_cpus.size());
  for (const int cpu_index : performance_cpus) {
    const bool is_allowed = current_allowed_cpus.contains(cpu_index);
    const bool is_realtime_core = realtime_cores.contains(cpu_index);
    if (is_allowed && !is_realtime_core) {
      allowed_performance_cpus.push_back(cpu_index);
    }
  }

  if (static_cast<int>(allowed_performance_cpus.size()) <
      min_performance_cores) {
    LOG(INFO) << "Skipping P-core pinning: only "
              << allowed_performance_cpus.size()
              << " allowed non-RT performance core(s) available (minimum "
              << min_performance_cores << ").";
    return std::nullopt;
  }

  // Because `allowed_performance_cpus` is a subset of `current_allowed_cpus`
  // with unique elements (from `ParseCpuList()`), equal sizes mean every
  // currently allowed CPU is already an allowed P-core and no affinity change
  // is needed.
  if (allowed_performance_cpus.size() == current_allowed_cpus.size()) {
    return std::nullopt;
  }

  return allowed_performance_cpus;
}

absl::StatusOr<std::optional<std::vector<int>>>
PinCurrentThreadToPerformanceCores(
    const absl::string_view performance_cpus_path,
    const absl::string_view cmdline_path, const int min_performance_cores) {
  INTR_ASSIGN_OR_RETURN(const absl::flat_hash_set<int> current_allowed_cpus,
                        GetCurrentAllowedCpus());
  INTR_ASSIGN_OR_RETURN(
      const std::optional<std::vector<int>> target_cpus,
      SelectPerformanceCoresToPin(current_allowed_cpus, performance_cpus_path,
                                  cmdline_path, min_performance_cores));
  if (!target_cpus.has_value()) {
    return std::nullopt;
  }

  ThreadOptions thread_options;
  thread_options.SetAffinity(*target_cpus);
  INTR_RETURN_IF_ERROR(SetAffinity(thread_options, pthread_self()));
  return target_cpus;
}

void TryPinCurrentThreadToPerformanceCores(
    const absl::string_view performance_cpus_path,
    const absl::string_view cmdline_path, const int min_performance_cores) {
  const absl::StatusOr<std::optional<std::vector<int>>> pinned_cpus =
      PinCurrentThreadToPerformanceCores(performance_cpus_path, cmdline_path,
                                         min_performance_cores);
  if (!pinned_cpus.ok()) {
    LOG(WARNING) << "Failed to pin calling thread to performance cores: "
                 << pinned_cpus.status();
  } else if (pinned_cpus->has_value()) {
    LOG(INFO) << "Pinned calling thread (and future child threads) to "
              << pinned_cpus->value().size() << " performance core(s): "
              << absl::StrJoin(pinned_cpus->value(), ",");
  }
}

}  // namespace intrinsic
