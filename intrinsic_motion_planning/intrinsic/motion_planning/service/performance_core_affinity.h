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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_PERFORMANCE_CORE_AFFINITY_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_PERFORMANCE_CORE_AFFINITY_H_

#include <optional>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace intrinsic {

// Default sysfs path listing Intel hybrid performance CPU indices.
inline constexpr absl::string_view kDefaultPerformanceCpusPath =
    "/sys/devices/cpu_core/cpus";

// Default kernel command line path used to detect isolated real-time CPUs.
inline constexpr absl::string_view kDefaultProcCmdlinePath = "/proc/cmdline";

// Minimum number of non-RT, allowed performance cores required before pinning,
// so multithreaded workloads are not inadvertently forced onto a single core.
inline constexpr int kMinPerformanceCoresForPinning = 2;

// Computes the sorted list of allowed non-RT performance CPUs to pin to, or
// `std::nullopt` if pinning should be skipped (e.g. homogeneous CPU without
// `performance_cpus_path` or fewer than `min_performance_cores` allowed P-cores
// after intersecting with `current_allowed_cpus` and excluding `rcu_nocbs` RT
// cores from `cmdline_path`).
// Returns `absl::InvalidArgumentError` if `min_performance_cores` is less than
// 1 or if `performance_cpus_path` contains a malformed CPU list.
absl::StatusOr<std::optional<std::vector<int>>> SelectPerformanceCoresToPin(
    const absl::flat_hash_set<int>& current_allowed_cpus,
    absl::string_view performance_cpus_path = kDefaultPerformanceCpusPath,
    absl::string_view cmdline_path = kDefaultProcCmdlinePath,
    int min_performance_cores = kMinPerformanceCoresForPinning);

// On hybrid CPUs, pins the calling thread (and all subsequently spawned child
// threads) to the allowed performance cores selected by
// `SelectPerformanceCoresToPin()` using `performance_cpus_path`,
// `cmdline_path`, and `min_performance_cores`.
// Returns `std::nullopt` without modifying affinity on homogeneous CPUs or when
// safety checks indicate pinning should be skipped. Otherwise returns the
// sorted list of pinned CPU indices.
absl::StatusOr<std::optional<std::vector<int>>>
PinCurrentThreadToPerformanceCores(
    absl::string_view performance_cpus_path = kDefaultPerformanceCpusPath,
    absl::string_view cmdline_path = kDefaultProcCmdlinePath,
    int min_performance_cores = kMinPerformanceCoresForPinning);

// Best-effort wrapper around `PinCurrentThreadToPerformanceCores()` that logs
// an `INFO` message with the pinned CPU indices when pinning is applied, or a
// `WARNING` on failure instead of returning an error. Forwards
// `performance_cpus_path`, `cmdline_path`, and `min_performance_cores`.
void TryPinCurrentThreadToPerformanceCores(
    absl::string_view performance_cpus_path = kDefaultPerformanceCpusPath,
    absl::string_view cmdline_path = kDefaultProcCmdlinePath,
    int min_performance_cores = kMinPerformanceCoresForPinning);

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_PERFORMANCE_CORE_AFFINITY_H_
