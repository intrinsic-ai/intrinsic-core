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

#ifndef INTRINSIC_ICON_SERVER_MAIN_LOOP_H_
#define INTRINSIC_ICON_SERVER_MAIN_LOOP_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/hardware/gpio/v1/gpio_service.grpc.pb.h"
#include "intrinsic/icon/control/context.h"
#include "intrinsic/icon/control/logging_mode.h"
#include "intrinsic/icon/control/realtime_control_manager.h"
#include "intrinsic/icon/control/services/service_collection.h"
#include "intrinsic/icon/hal/hardware_module_manager.h"
#include "intrinsic/icon/hal/hardware_module_proxy.h"  // IWYU pragma: keep
#include "intrinsic/icon/hal/realtime_clock.h"
#include "intrinsic/icon/interprocess/shared_memory_lockstep/shared_memory_lockstep.h"
#include "intrinsic/icon/interprocess/shared_memory_manager/memory_segment.h"
#include "intrinsic/icon/proto/v1/jogging_service.grpc.pb.h"
#include "intrinsic/icon/server/config/realtime_control_config.pb.h"
#include "intrinsic/icon/server/icon_api_service.h"
#include "intrinsic/icon/server/time_slice.h"
#include "intrinsic/icon/testing/realtime_annotations.h"
#include "intrinsic/icon/utils/clock.h"
#include "intrinsic/icon/utils/duration.h"
#include "intrinsic/icon/utils/realtime_guard.h"
#include "intrinsic/icon/utils/realtime_status.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/rt_thread.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/util/thread/thread_options.h"

namespace intrinsic::icon {

// This class runs the ICON main control loop. This calls:
// 1) `Read` on all hardware modules
// 2) `Update` on RealtimeControlManager
//      - RealtimePart::ReadStatus
//      - rtcl_session_manager::RunSense
//      - rtcl_session_manager::RunControl
//      - RealtimePart::ApplyCommand
// 3) `Write` on all hardware modules
//
// The MainLoop supports internal timing or timing dictated by a connected
// Hardware module.
class MainLoop {
 public:
  // Defines what component drives the clock, who steps the MainLoop.
  enum class ClockMode {
    // MainLoop manages its own clock. Call `Start()` to kick off a
    // thread that runs to completion.
    kInternalClock,

    // One of the configured hardware modules is responsible for stepping
    // MainLoop.
    // `Start()` kicks off a thread that initializes MainLoop up to the
    // point that it enters the "Running" state. The hardware module must call
    // `module_config.GetRealtimeClock()` to obtain a RealtimeClockInterface and
    // then call `clock->TickBlockingWithTimeout()` repeatedly to perform cyclic
    // update.
    kHardwareModuleDrivenClock,
  };

  // Create a MainLoop instance, using the configuration objects passed in
  // explicitly in the form of `name`, `control_frequency_hz`,
  // `internal_clock_hard_deadline`, `context`, `realtime_control_manager`,
  // `hardware_module_manager`, `read_write_timeout`, `clock_mode`,
  // `hardware_module_that_drives_clock`, and
  //   `main_loop_thread_options`
  // - `realtime_control_manager` is expected to be uninitialized.
  // - If `internal_clock_hard_deadline` is true, MainLoop will shut down after
  // missing a cycle deadline when running with an internal clock.
  // - `read_write_timeout` determines the permissible time for read and write
  // operations to the hardware modules before a timeout error is triggered.
  // - The clock_mode dictates who is responsible for stepping MainLoop once
  // realtime operation begins. See ClockModeEnum for options. If clock_mode is
  // kHardwareModuleDrivenClock, then hardware_module_that_drives_clock must be
  // set to the name of the hardware module.
  // - If malloc_guarded is true, the main loop observes for mallocs. At the end
  // of each cycle, it reports all mallocs from that cycle as a log message.
  // - The `default_logging_mode` is active after creation and defines how ICON
  // logs the robot status. It can be changed at runtime.
  //   Returns:
  // - InvalidArgumentError if clock_mode is kHardwareModuleDrivenClock but
  //   hardware_module_that_drives_clock is empty, or if clock_mode is not
  //   kHardwareModuleDrivenClock but hardware_module_that_drives_clock is set.
  // - A fully initialized MainLoop instance otherwise.
  static absl::StatusOr<std::unique_ptr<MainLoop>> Create(
      double control_frequency_hz, bool internal_clock_hard_deadline,
      absl::Duration read_write_timeout, Context context,
      const intrinsic_proto::icon::RealtimeControlConfig&
          realtime_control_manager_config,
      std::unique_ptr<ServiceCollection> service_collection,
      std::unique_ptr<HardwareModuleManager> hardware_module_manager,
      LoggingMode default_logging_mode,
      ClockMode clock_mode = ClockMode::kInternalClock,
      absl::string_view memory_namespace = "",
      absl::string_view hardware_module_that_drives_clock = "",
      ThreadOptions main_loop_thread_options = {}, bool malloc_guarded = true);

  ClockMode clock_mode() const { return clock_mode_; }

  absl::Status Start() {
    INTR_ASSIGN_OR_RETURN(
        const auto missing,
        hardware_module_manager_->GetUnusedRequiredInterfaceNames());
    if (!missing.empty()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Please configure the following required, but currently unused "
          "interfaces: ",
          absl::StrJoin(
              missing, ", ",
              absl::PairFormatter(
                  absl::AlphaNumFormatter(), ": ",
                  [](std::string* out,
                     const std::vector<std::string>& unused_interfaces) {
                    absl::StrAppend(
                        out, "[", absl::StrJoin(unused_interfaces, ", "), "]");
                  }))));
    } else {
      LOG(INFO) << "All required interfaces are used.";
    }

    INTR_ASSIGN_OR_RETURN(
        main_loop_thread_,
        CreateRealtimeCapableThread(main_loop_thread_options_, [this]() {
          auto status = this->Run();
          // Since the `intrinsic::Thread` thread
          // function is required to be
          // `void()` we at least print the
          // error if present.
          if (!status.ok()) {
            LOG(ERROR) << "MainLoop::Run failed: " << status;
          }
        }));

    return absl::OkStatus();
  }

  // Returns 'nullopt' if MainLoop is running.
  std::optional<RealtimeStatus> GetExitStatus() const {
    if (shutdown_notification_) {
      return exit_status_;
    }
    return std::nullopt;
  }

  void Join() {
    if (main_loop_thread_.joinable()) {
      main_loop_thread_.join();
    }
  }

  // Request shutdown. Call Join() after this to cleanly exit.
  //
  // Shutdown of user-provided objects will happen in the following order:
  // 1) RealtimeControlManager is destructed;
  // 2) Services are destructed.
  void RequestShutdown();

  IconApiService* absl_nullable IconService() {
    return realtime_control_manager_->IconService();
  }
  intrinsic_proto::gpio::v1::GPIOService::Service* absl_nullable GpioService() {
    return realtime_control_manager_->GpioService();
  }
  intrinsic_proto::icon::v1::JoggingService::Service* absl_nullable
  JoggingService() {
    return realtime_control_manager_->JoggingService();
  }

 private:
  // Bundles objects needed for interprocess real-time clock.
  struct RemoteClock {
    // Payload from the remote clock, updated each time it calls
    // TickBlockingWithTimeout.
    ReadOnlyMemorySegment<RealtimeClockUpdate> realtime_clock_update;
    // Shared memory version of Lockstep used with hardware-module-driven
    // clocks.
    SharedMemoryLockstep lockstep;
    const std::string hardware_module_that_drives_clock;
  };

  // Bundles objects needed for interprocess real-time clock.
  struct InternalClockParams {
    // If this is true then when in ClockMode::kInternalClock the loop will
    // fault if a cycle overruns.
    bool hard_deadline;
  };

  struct UpdateDurationStats {
    uint32_t before_update_duration_us = 0;
    uint32_t begin_cycle_duration_us = 0;
    uint32_t read_duration_us = 0;
    uint32_t update_duration_us = 0;
    uint32_t write_duration_us = 0;
    // This is the minimum of the headroom observed since start, ie the time
    // remaining in the cycle when FinishCycle() is called. It indicates how
    // much additional time could be consumed by actions. If we have observed
    // overruns it will be negative.
    int32_t min_headroom_us = INT32_MAX;
  };

  MainLoop(
      double control_frequency_hz, absl::Duration hard_cycle_time_timeout,
      absl::Duration read_write_timeout, const Context& context,
      std::unique_ptr<icon::RealtimeControlManager> realtime_control_manager,
      std::unique_ptr<ServiceCollection> service_collection,
      std::unique_ptr<icon::HardwareModuleManager> hardware_module_manager,
      std::variant<InternalClockParams, std::unique_ptr<RemoteClock>>
          clock_data,
      MainLoop::ClockMode clock_mode,
      ::intrinsic::ThreadOptions main_loop_thread_options, bool malloc_guarded);

  // Phases of the main processing loop.
  //
  // Trigger Hardware module manager to perform Read on each hardware module.
  RealtimeStatus Read() INTRINSIC_CHECK_REALTIME_SAFE;
  // Trigger Hardware module manager to process transition requests and perform
  // Write on each hardware module.
  RealtimeStatus Write(const TimeSlice& time_slice)
      INTRINSIC_CHECK_REALTIME_SAFE;

  // Calls Prepare() on all hardware modules.
  absl::Status StartUp();
  // Calls Activate() on all hardware modules.
  RealtimeStatus ActivateHardwareModules() INTRINSIC_CHECK_REALTIME_SAFE;
  absl::Status Run();
  // Enters RT mode, activates all hardware modules and runs the RT loop.
  RealtimeStatus RunRt() INTRINSIC_CHECK_REALTIME_SAFE;

  RealtimeStatus CyclicUpdate() INTRINSIC_CHECK_REALTIME_SAFE;
  absl::Status Shutdown();
  // Synchronizes the timeslice update loop with the clock owner.
  RealtimeStatus BeginCycle() INTRINSIC_CHECK_REALTIME_SAFE;

  // Performs final verification and logging of timing.
  RealtimeStatus FinishCycle(const TimeSlice& time_slice)
      INTRINSIC_CHECK_REALTIME_SAFE;

  void CancelClockIfNeeded();

  std::unique_ptr<HardwareModuleManager> hardware_module_manager_;
  std::unique_ptr<ServiceCollection> service_collection_;

  // Holds pointers to hardware_module_manager_ and service_collection_, so
  // must go after that.
  Context context_;

  // RealtimeControlManager a Context that references global_context_, so it
  // needs to go after that (so that it's destroyed first).
  std::unique_ptr<RealtimeControlManager> realtime_control_manager_;

  // This holds all the variables and parameters necessary for running either an
  // ICON internal clock or running the mainloop using a HardwareModule driven
  // clock.
  std::variant<InternalClockParams, std::unique_ptr<RemoteClock>> clock_data_;

  const Duration cycle_period_;
  // Fail if any hardware module takes longer (except in sim or test
  // mode).
  const absl::Duration hard_cycle_time_timeout_;
  // Read and Write calls to the `hardware_module_manager_` will timeout and
  // return an error after `hwm_read_write_timeout_`.
  const absl::Duration hwm_read_write_timeout_;

  Time cycle_start_;
  Time cycle_end_;

  std::atomic_bool shutdown_notification_ = false;
  // Thread safety: Locked by 'thread_' until shutdown_notification_.
  RealtimeStatus exit_status_ = OkStatus();
  std::atomic_bool user_requested_shutdown_ = false;

  UpdateDurationStats update_duration_stats_;

  // Count of cycle time overruns since start.
  size_t n_overruns_ = 0;

  const ThreadOptions main_loop_thread_options_;

  Thread main_loop_thread_;

  ClockMode clock_mode_;
  const bool malloc_guarded_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_SERVER_MAIN_LOOP_H_
