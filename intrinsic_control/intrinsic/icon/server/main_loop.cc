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

#include "intrinsic/icon/server/main_loop.h"

#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <variant>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/control/realtime_control_manager.h"
#include "intrinsic/icon/control/services/service_collection.h"
#include "intrinsic/icon/hal/hardware_module_manager.h"
#include "intrinsic/icon/hal/hardware_module_proxy.h"
#include "intrinsic/icon/hal/realtime_clock.h"
#include "intrinsic/icon/interprocess/shared_memory_lockstep/shared_memory_lockstep.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/memory_segment.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/config/realtime_control_config.pb.h"
#include "intrinsic/icon/server/runtime_options.h"
#include "intrinsic/icon/server/time_slice.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/current_cycle.h"
#include "intrinsic/icon/utils/duration.h"
#include "intrinsic/icon/utils/fixed_str_cat.h"
#include "intrinsic/icon/utils/log.h"
#include "intrinsic/icon/utils/malloc_guard.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_stack_trace.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/icon/utils/realtime_status_macro.h"
#include "intrinsic/util/page_fault_info.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/rt_trace.h"
#include "intrinsic/util/thread/thread_options.h"

namespace intrinsic {
namespace icon {
namespace {

constexpr absl::Duration kNonRealtimeHardCycleTimeout = absl::Seconds(20);
// TODO(b/352520848): Reduce to cycle time.
constexpr absl::Duration kHardwareModuleActivationTimeout = absl::Seconds(10);
constexpr absl::Duration kHardwareModulePrepareTimeout = absl::Seconds(90);
constexpr absl::string_view kMainLoopThreadName = "IconMainLoop";
}  // namespace

MainLoop::MainLoop(
    double control_frequency_hz, absl::Duration hard_cycle_time_timeout,
    absl::Duration read_write_timeout, const Context& context,
    std::unique_ptr<RealtimeControlManager> realtime_control_manager,
    std::unique_ptr<ServiceCollection> service_collection,
    std::unique_ptr<HardwareModuleManager> hardware_module_manager,
    std::variant<InternalClockParams, std::unique_ptr<RemoteClock>> clock_data,
    MainLoop::ClockMode clock_mode, ThreadOptions main_loop_thread_options,
    bool malloc_guarded)
    : hardware_module_manager_(std::move(hardware_module_manager)),
      service_collection_(std::move(service_collection)),
      context_(context),
      realtime_control_manager_(std::move(realtime_control_manager)),
      clock_data_(std::move(clock_data)),
      cycle_period_(fromHz(control_frequency_hz)),
      hard_cycle_time_timeout_(hard_cycle_time_timeout),
      hwm_read_write_timeout_(read_write_timeout),
      main_loop_thread_options_(
          std::move(main_loop_thread_options.SetName(kMainLoopThreadName))),
      clock_mode_(clock_mode),
      malloc_guarded_(malloc_guarded) {}

absl::StatusOr<std::unique_ptr<MainLoop>> MainLoop::Create(
    double control_frequency_hz, bool internal_clock_hard_deadline,
    absl::Duration read_write_timeout, Context context,
    const intrinsic_proto::icon::RealtimeControlConfig&
        realtime_control_manager_config,
    std::unique_ptr<ServiceCollection> service_collection,
    std::unique_ptr<HardwareModuleManager> hardware_module_manager,
    LoggingMode default_logging_mode, MainLoop::ClockMode clock_mode,
    absl::string_view memory_namespace,
    absl::string_view hardware_module_that_drives_clock,
    ThreadOptions main_loop_thread_options, bool malloc_guarded) {
  if (control_frequency_hz <= 0) {
    return absl::InvalidArgumentError(absl::StrCat(
        " Cycle frequency is ", control_frequency_hz, ", should be >0"));
  }

  if (clock_mode == ClockMode::kHardwareModuleDrivenClock &&
      hardware_module_that_drives_clock.empty()) {
    return absl::InvalidArgumentError(
        "clock_mode is kHardwareModuleDrivenClock; "
        "hardware_module_that_drives_clock must be provided.");
  }
  if (clock_mode != ClockMode::kHardwareModuleDrivenClock &&
      !hardware_module_that_drives_clock.empty()) {
    return absl::InvalidArgumentError(
        "clock_mode is not kHardwareModuleDrivenClock; "
        "hardware_module_that_drives_clock must be empty.");
  }
  if (hardware_module_manager == nullptr) {
    return absl::InvalidArgumentError(
        "hardware_module_manager must not be null.");
  }
  absl::Duration hard_cycle_time_timeout = kNonRealtimeHardCycleTimeout;
  if ((context.GetRuntimeOptions().mode == ServerRuntimeMode::kNormal)) {
    // TODO(b/274907790) Gradually reduce the cycle timeout to, e.g. twice the
    // cycle time.
    hard_cycle_time_timeout =
        100 *
        absl::Nanoseconds(ToInt64Nanoseconds(fromHz(control_frequency_hz)));
  }

  if (context.GetRuntimeOptions().mode == ServerRuntimeMode::kNonRealtime &&
      internal_clock_hard_deadline) {
    return absl::InvalidArgumentError(
        "The option 'internal_clock_hard_deadline' is only "
        "supported in a realtime context.");
  }

  std::variant<InternalClockParams, std::unique_ptr<RemoteClock>> clock_data;
  // If the clock is driven by a Hardware module acquire the shared memory
  // lockstep based on the hardware module name.
  if (clock_mode == ClockMode::kHardwareModuleDrivenClock) {
    auto remote_clock = std::make_unique<RemoteClock>(
        RemoteClock{.hardware_module_that_drives_clock =
                        std::string(hardware_module_that_drives_clock)});
    const HardwareModuleProxy* clock_driving_hwm =
        hardware_module_manager->GetHardwareModuleProxy(
            hardware_module_that_drives_clock);

    if (!clock_driving_hwm) {
      return absl::FailedPreconditionError(
          absl::StrCat("The clock driving hardware module '",
                       hardware_module_that_drives_clock,
                       "' is not registered with the HardwareModuleManager."));
    }

    // Get the shared memory lockstep object for cross-process
    // synchronization.
    INTR_ASSIGN_OR_RETURN(
        remote_clock->lockstep,
        GetSharedMemoryLockstep(*clock_driving_hwm,
                                kRealtimeClockLockstepInterfaceName));
    // Get the shared memory segment for communicating cycle time.
    INTR_ASSIGN_OR_RETURN(
        remote_clock->realtime_clock_update,
        clock_driving_hwm->GetReadOnlyMemorySegment<RealtimeClockUpdate>(
            kRealtimeClockUpdateInterfaceName));

    clock_data = std::move(remote_clock);
  } else {
    clock_data =
        InternalClockParams{.hard_deadline = internal_clock_hard_deadline};
  }

  ::intrinsic_proto::icon::v1::ServerConfig server_config;
  server_config.set_name(context.GetRuntimeOptions().server_name);
  server_config.set_frequency_hz(control_frequency_hz);

  INTR_ASSIGN_OR_RETURN(
      auto realtime_control_manager,
      RealtimeControlManager::Create(
          realtime_control_manager_config, server_config, context,
          *hardware_module_manager, default_logging_mode));

  return absl::WrapUnique(new MainLoop(
      control_frequency_hz, hard_cycle_time_timeout, read_write_timeout,
      context, std::move(realtime_control_manager),
      std::move(service_collection), std::move(hardware_module_manager),
      std::move(clock_data), clock_mode, std::move(main_loop_thread_options),
      malloc_guarded));
}

RealtimeStatus MainLoop::Read() {
  INTRINSIC_TRACE_SCOPED("MainLoop::Read");

  // Trigger each hardware module to update their status.
  absl::Time deadline = absl::Now() + hwm_read_write_timeout_;
  INTRINSIC_RT_RETURN_IF_ERROR(hardware_module_manager_->Read(deadline));

  return OkStatus();
}

RealtimeStatus MainLoop::Write(const TimeSlice& time_slice) {
  INTRINSIC_TRACE_SCOPED("MainLoop::Write");

  INTRINSIC_RT_RETURN_IF_ERROR(
      hardware_module_manager_->ProcessTransitionRequestAsync());

  absl::Time deadline = absl::Now() + hwm_read_write_timeout_;
  INTRINSIC_RT_RETURN_IF_ERROR(hardware_module_manager_->Write(deadline));

  return OkStatus();
}

absl::Status MainLoop::Run() {
  if (malloc_guarded_) {
    LOG(INFO) << "Malloc guarded MainLoop";
    intrinsic::icon::SetThreadLocalMallocGuardReaction(
        intrinsic::icon::MallocGuardReaction::kStoreViolation);
  }
  ScopedMallocGuardHook malloc_hook(!malloc_guarded_);
  InitRtStackTrace();

  // Give all hardware modules some time to prepare for realtime operation.
  if (auto status = hardware_module_manager_->Prepare(
          absl::Now() + kHardwareModulePrepareTimeout);
      !status.ok()) {
    exit_status_ = RealtimeStatus(status.code(), status.message());
    shutdown_notification_ = true;
    return Shutdown();
  }
  LOG(INFO) << "Preparing hardware modules succeeded.";

  exit_status_ = RunRt();
  shutdown_notification_ = true;
  return Shutdown();
}

RealtimeStatus MainLoop::RunRt() {
  MallocGuard malloc_guard(!malloc_guarded_);
  icon::RealTimeGuard rt_guard;
  INTRINSIC_RT_RETURN_IF_ERROR(ActivateHardwareModules());

  INTRINSIC_RT_LOG(INFO) << "Activated hardware modules and entered RT mode.";

  // Initialize the cycle start and end times.
  cycle_start_ = Clock::now();
  cycle_end_ = cycle_start_ + cycle_period_;

  while (!shutdown_notification_.load()) {
    INTRINSIC_RT_RETURN_IF_ERROR(CyclicUpdate());
  }
  return OkStatus();
}

RealtimeStatus MainLoop::ActivateHardwareModules() {
  // If its time to shut down, get out immediately.
  if (shutdown_notification_.load()) {
    return OkStatus();
  }

  // Give the HWM some time to activate.
  if (auto activate_status = hardware_module_manager_->Activate(
          absl::Now() + kHardwareModuleActivationTimeout);
      !activate_status.ok()) {
    INTRINSIC_RT_LOG(ERROR)
        << "Activating hardware modules failed: " << activate_status.message();
    return activate_status;
  }

  return OkStatus();
}

void MainLoop::CancelClockIfNeeded() {
  if (clock_mode() != ClockMode::kInternalClock) {
    if (std::holds_alternative<std::unique_ptr<RemoteClock>>(clock_data_)) {
      std::get<std::unique_ptr<RemoteClock>>(clock_data_)->lockstep->Cancel();
    }
  }
}

absl::Status MainLoop::Shutdown() {
  INTRINSIC_RT_LOG(INFO) << "Icon main loop shutting down, exit status: "
                         << (exit_status_.ok() ? absl::string_view("OK")
                                               : exit_status_.message());

  INTRINSIC_RT_LOG(INFO) << "Deactivating hardware modules";
  // Deactivate all hardware modules.
  auto status =
      intrinsic::StatusBuilder(hardware_module_manager_->TryDeactivate(
                                   absl::Now() + absl::Seconds(1)))
          .SetPrepend()
      << "Failed to deactivate hardware modules during shutdown: ";

  CancelClockIfNeeded();
  realtime_control_manager_->Shutdown(exit_status_);

  return status;
}

RealtimeStatus MainLoop::BeginCycle() {
  if (shutdown_notification_) {
    return AbortedError("BeginCycle aborted due to a shutdown notification.");
  }
  icon::Cycle::IncrementCurrentCycle();

  // No synchronization needed when internally driven.
  if (clock_mode() == ClockMode::kHardwareModuleDrivenClock) {
    RemoteClock& remote_clock =
        *std::get<std::unique_ptr<RemoteClock>>(clock_data_);
    if (RealtimeStatus status =
            remote_clock.lockstep->StartOperationBWithTimeout(
                /*timeout=*/hard_cycle_time_timeout_);
        !status.ok()) {
      RealtimeStatus combined(
          status.code(),
          RealtimeStatus::StrCat(
              "BeginCycle: ", remote_clock.hardware_module_that_drives_clock,
              ": ", status.message()));
      INTRINSIC_RT_LOG_THROTTLED(WARNING) << combined.ToString();
      return combined;
    }
    cycle_start_ = timeFromNSec(
        remote_clock.realtime_clock_update.GetValue().cycle_start_nanoseconds);
    cycle_end_ = cycle_start_ + cycle_period_;
  }
  return OkStatus();
}

RealtimeStatus MainLoop::CyclicUpdate() {
  INTRINSIC_TRACE_SCOPED("MainLoop::CyclicUpdate");
  Time before_update = Clock::now();
  update_duration_stats_.before_update_duration_us =
      static_cast<uint32_t>(ToInt64Microseconds(before_update - cycle_start_));

  INTRINSIC_RT_RETURN_IF_ERROR(BeginCycle());
  Time begin_cycle_done = Clock::now();

  // Send a request to all hardware modules to send their most current readings
  // for their process variables and wait for them to all respond.
  INTRINSIC_RT_RETURN_IF_ERROR(Read());
  Time read_done = Clock::now();

  // Run the Update method on the RealtimeControlManager allowing parts and
  // actions to determine the new control values based on the previously
  // collected process variables.
  INTRINSIC_RT_RETURN_IF_ERROR(realtime_control_manager_->Update(cycle_start_));
  Time update_done = Clock::now();

  INTRINSIC_RT_RETURN_IF_ERROR(Write(TimeSlice(update_done, cycle_end_)));
  Time write_done = Clock::now();

  // Calculate all the timing intervals.
  update_duration_stats_.begin_cycle_duration_us = static_cast<uint32_t>(
      ToInt64Microseconds(begin_cycle_done - before_update));
  update_duration_stats_.read_duration_us =
      static_cast<uint32_t>(ToInt64Microseconds(read_done - begin_cycle_done));
  update_duration_stats_.update_duration_us =
      static_cast<uint32_t>(ToInt64Microseconds(update_done - read_done));
  update_duration_stats_.write_duration_us =
      static_cast<uint32_t>(ToInt64Microseconds(write_done - update_done));

  // Finish the cycle. Shutdown if this fails.
  return FinishCycle(TimeSlice(write_done, cycle_end_));
}

RealtimeStatus MainLoop::FinishCycle(const TimeSlice& time_slice) {
  if (icon::GetThreadLocalMallocViolations().num_violations > 0) {
    INTRINSIC_RT_LOG_THROTTLED(WARNING)
        << "Detected "
        << icon::GetThreadLocalMallocViolations().num_violations.load()
        << " malloc calls for "
        << icon::GetThreadLocalMallocViolations().allocated_bytes.load()
        << " bytes in this cycle in the mainloop thread!";
    icon::ResetThreadLocalMallocViolations();
  }
  // Keep track of the headroom (smallest remaining duration observed).
  int32_t current_headroom_us =
      static_cast<int32_t>(ToInt64Microseconds(time_slice.GetDuration()));
  if (update_duration_stats_.min_headroom_us > current_headroom_us) {
    update_duration_stats_.min_headroom_us = current_headroom_us;
  }
  // Only logs cycle statistics if running in realtime mode.
  if (current_headroom_us < 0 &&
      context_.GetRuntimeOptions().mode == ServerRuntimeMode::kNormal) {
    n_overruns_++;
    INTRINSIC_TRACE_INSTANT(FixedStrCat<tracing::kMaxNameLength>(
        "exceeded timeslice by ", -1.0 * current_headroom_us, "usec"));
    INTRINSIC_RT_LOG_THROTTLED(ERROR)
        << "exceeded timeslice by " << -1.0 * current_headroom_us << "uSec."
        << "Before_update: " << update_duration_stats_.before_update_duration_us
        << " uSec."
        << "Begin_cycle: " << update_duration_stats_.begin_cycle_duration_us
        << " uSec." << "Read: " << update_duration_stats_.read_duration_us
        << " uSec." << "Update: " << update_duration_stats_.update_duration_us
        << " uSec." << "Write: " << update_duration_stats_.write_duration_us
        << " uSec.";
  }

  static size_t cycle_count = 0;
  INTRINSIC_TRACE_NAMED_COUNTER("CurrentCycle", cycle_count++);
  // Periodically print current headroom. This uses cycle_count to avoid
  // noisily logging every second.
  // Only logs cycle statistics if running in realtime mode.
  if (cycle_count % 10000 == 0 &&
      context_.GetRuntimeOptions().mode == ServerRuntimeMode::kNormal) {
    INTRINSIC_RT_LOG(INFO) << "cycle " << cycle_count
                           << ": The minimum headroom is "
                           << update_duration_stats_.min_headroom_us
                           << " us since start. The period is "
                           << static_cast<uint32_t>(
                                  ToInt64Microseconds(cycle_period_))
                           << " us";
    PagefaultInfo page_faults = GetPagefaultInfo();
    INTRINSIC_RT_LOG(INFO)
        << "PagefaultInfo [Abs Major | Abs Minor | Rel Major | Rel Minor ] ["
        << page_faults.major_faults << " | " << page_faults.minor_faults
        << " | " << page_faults.delta_major_faults << " | "
        << page_faults.delta_minor_faults << "]";
    if (n_overruns_ > 0) {
      INTRINSIC_RT_LOG(INFO)
          << "Observed " << n_overruns_ << " overruns since start";
    }
  }
  // Make sure that we are keeping up; if we have slipped and missed one
  // or more cycles, reset our next cycle time to give us a new full cycle.
  if (clock_mode() == ClockMode::kInternalClock) {
    if (time_slice.GetDuration().count() < 0) {
      // Not related to the hard_cycle_time_timeout_, which is checked by the
      // lockstep.
      if (std::get<InternalClockParams>(clock_data_).hard_deadline) {
        return AbortedError(RealtimeStatus::StrCat(
            "MainLoop is configured with a hard deadline and there was an "
            "overrun. Remaining duration was:",
            static_cast<uint32_t>(
                ToInt64Microseconds(time_slice.GetDuration())),
            "us."));
      }
      // Start the new cycle now.
      cycle_start_ = Clock::now();
      // Find the cycle end that keeps us in sync.
      // The next cycle duration may be really short and MainLoop may overrun
      // again. However, this is still a better solution than just skipping a
      // tick.
      cycle_end_ = FindNextCycleEnd(cycle_start_, cycle_end_, cycle_period_);
    } else {
      // Blocking behavior is intended here.
      [this]() INTRINSIC_SUPPRESS_REALTIME_CHECK {
        absl::SleepFor(
            absl::Nanoseconds(ToDoubleNanoseconds(cycle_end_ - Clock::now())));
      }();

      // Ideally, the next cycle should start one tick after the last cycle.
      cycle_start_ = cycle_end_;
      cycle_end_ += cycle_period_;
    }
  } else {
    RemoteClock& remote_clock =
        *std::get<std::unique_ptr<RemoteClock>>(clock_data_);
    // Give control back to clock owner.
    if (RealtimeStatus status = remote_clock.lockstep->EndOperationB();
        !status.ok()) {
      RealtimeStatus combined(
          status.code(),
          RealtimeStatus::StrCat(
              "FinishCycle: ", remote_clock.hardware_module_that_drives_clock,
              ": ", status.message()));
      INTRINSIC_RT_LOG_THROTTLED(WARNING) << combined.ToString();
      return combined;
    }
  }
  return OkStatus();
}

void MainLoop::RequestShutdown() {
  shutdown_notification_.store(true);
  user_requested_shutdown_.store(true);
}

}  // namespace icon
}  // namespace intrinsic
