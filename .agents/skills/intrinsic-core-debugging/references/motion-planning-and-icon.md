# Debugging motion planning, inverse kinematics, and `ICON` real-time control

## Input-aware decision tree for motion planning and inverse kinematics failures

| Observed symptom | Precondition / failure signature | Diagnostic check | Targeted remediation |
| :--- | :--- | :--- | :--- |
| **Empty `ComputeIk` result** | `result.solutions` is empty while `result.ik_debug_information.ik_solutions` has entries | Inspect rejected candidates in `ik_debug_information` to see whether joint limits, self-collision, or environment collision rejected candidates | Constrain initial seed pose toward viable joint limits, verify collision geometry visualization, or select numerical IK (`NLOptConstrainedIKSolver` via `ik_solver_key`). |
| **Linear segment 2*pi wrist flip** | `maximum_absolute_joint_configuration_error` near `6.283` rad on linear `Segment 1` | Check whether preceding approach `Segment 0` chose an IK branch near joint limit (e.g. wrist q_5 near -3*pi/2) | Set `ensure_same_branch=True` or `prefer_same_branch=True` in `IKOptions`, constrain approach target joint bounds away from limits, or apply a 180° tool symmetry rotation offset. |
| **Departure collision on retreat** | `Invalid initial joint configuration... collision(s) detected` at `Segment 0` of retreat | Compare commanded approach endpoint clearance against sensed joint state noise | Configure per-segment `collision_rules` (`is_excluded: true`) on the initial departure segment for expected contact pairs, and maintain >= 2 mm clearance on free-space poses. |
| **Multi-minute planning timeout** | `Large volume mesh detected, collision computation may be slow` in World Service logs | Inspect mesh volume; millimeter-imported CAD geometry inflates bounding volume by 10^9x (> 200 m^3) | Rescale the imported mesh to meters using a `0.001` scale factor or substitute simplified convex collision hulls. |
| **Behavior tree cancellation false lead** | Process hangs in `CANCELING` with final log `Calling GetFootprint for skill name: preplan_motion` | Trajectory planning runs in `GetFootprint()`; cancellation skips `Execute()` | Inspect sibling skill logs at the exact cancellation timestamp to identify the true failing condition or monitoring node. |
| **Parallel branch serialization** | `bt.Parallel` branches execute sequentially when running motion alongside monitoring | Inspect `SkillInterface.GetFootprint()` defaults (`lock_the_universe: true`) | Override `GetFootprint()` to return `footprint_pb2.Footprint(lock_the_universe=False)` and declare only the required robot part, avoiding whole-cell exclusive locking. |
| **Collinear pointing constraint divergence** | Numerical IK (`SLSQP`) diverges or times out when applying `PointAtConstraint` | Check if target point is collinear with the pointing axis at the initial guess (J*J^T condition number spike) | Rotate the initial seed configuration away from collinear alignment before invoking solver. |
| **Hardware vs. application limit mismatch** | World extraction or execution fails with `System acceleration limit is less than the application acceleration limit` | Payload was updated via `set_payload` without synchronizing world limits | Update application limits in the world via `UpdateWorld` on `world_id="world"` and `world_id="sim_world"`, ensuring `planning_limits` leave headroom below `system_limits`. |

## Input-aware decision tree for `ICON` real-time loop overruns and hardware faults

| Observed symptom | Precondition / failure signature | Diagnostic check | Targeted remediation |
| :--- | :--- | :--- | :--- |
| **Cycle overrun with late wakeup** | `Long duration between read_status_calls: >2ms` with `exec` > 95% of cycle time | Inspect four-phase timing breakdown (`rs`, `proc`, `ac`, `exec`) in `kubectl logs -n app-resources deployment/rs-icon` | High `exec` proves host core preemption or futex wakeup delay. Verify `PagefaultInfo` shows `Rel Major/Minor == 0`, isolate RT CPU cores from non-RT threads, and ensure point-to-point network cabling. |
| **EtherCAT frame response error** | `EC_NOTIFY_FRAME_RESPONSE_ERROR (0x1000a)` preceded by ultra-short catch-up cycle (~95 µs) | Inspect the `Long duration between read_status_calls` immediately preceding the underrun log | Address the preceding long `exec` stall that triggered the catch-up cycle; eliminate host core preemption before attempting fieldbus parameter adjustments. |
| **Communication timeout with zero overruns** | Robot controller faults with `MOTN-603` or RSI/EGM timeout while ICON cycle times are nominal | Check for non-RT auxiliary TCP threads (e.g. OPC-UA) sharing the real-time NIC | Priority inversion in kernel transmit path. Elevate auxiliary TCP threads to low real-time priority (`SCHED_FIFO` 20) or disable them during active motion. |
| **Session startup failure** | `AlreadyExistsError: Channel is already in use by Session <old_id>` | Check whether a previous skill leaked its session context or is blocked in cleanup | Ensure skills manage sessions via context managers (`with icon_client.start_session()`), and inspect `rs-icon` logs for delayed safety actions. |
| **Fault clearing failure** | `Robot is safety stopped. ModeOfSafeOperation: UNKNOWN` when running `inctl icon clear-faults` | Query `inctl icon status --instance_name=icon --address=localhost:17080` for physical safety status | Software commands cannot override hardware safety stops. Disengage physical E-stops (`BUTTON_STATUS_DISENGAGED`) and reset teach pendant faults before retrying. |
| **Robot controller sequence lag** | `MOTN-600 ST: Sequence No error. PC <N>, Rob <N-2>` | Compare planned Cartesian speed against robot controller collaborative limits | Ensure planned trajectories respect collaborative Cartesian speed limits (typically <= 250 mm/s) to prevent controller CPU packet buffer overflow. |
| **Shared memory lockfile restart deadlock** | Container crash enters `CrashLoopBackOff` with `Resource temporarily unavailable` acquiring shared memory | Check for stale `/tmp/intrinsic_icon/ur_module.lock` on host filesystem volume surviving container exit | Remove `/tmp/intrinsic_icon/ur_module.lock` before container restart, then clear faults via `inctl icon clear-faults --instance_name=icon --address=localhost:17080`. |
| **Simulation clock synchronization timeout** | `ai.intrinsic.move_robot` fails with `DEADLINE_EXCEEDED` in simulation | Check `rs-icon` logs: `Timed out waiting for first message` (loop unstarted since boot) vs `Timed out reading from Part status queue` (loop stalled mid-run) | For first-message timeout, verify Gazebo clock ticking thread; for mid-run timeout, inspect secondary sensor hardware modules (e.g. F/T sensor) blocking the main cycle. |

## Motion planner client initialization, inverse kinematics diagnostics, and collision geometry

- **Direct `MotionPlannerClient` initialization**:
  - Calling `MotionPlannerClient.for_solution(solution)` returns a `MotionPlannerClientBase` instance that exposes `clear_cache` but lacks `plan_trajectory` and `compute_ik`.
  - To invoke planning or inverse kinematics from Python, instantiate `MotionPlannerClient(world_id="world", stub=MotionPlannerServiceStub(channel))` directly.
- **Interpreting `ComputeIkResult` debug candidates**:
  - When `compute_ik` returns an empty `result.solutions` list, `result.ik_debug_information.ik_solutions` contains all candidate configurations evaluated during search, including configurations rejected due to collisions or joint limits.
  - Inspect `result.solutions` to determine planning success; inspect `ik_debug_information` exclusively to diagnose which specific constraint caused candidate rejection.
- **Multi-segment 2*pi wrist flips on linear segments**:
  - When a Cartesian linear segment (`Segment 1`) fails with `maximum_absolute_joint_configuration_error` near `6.283` radians (2*pi), the preceding point-to-point approach segment (`Segment 0`) chose an IK branch near a joint position limit (e.g. wrist joint q_5 near -3*pi/2).
  - Set `ensure_same_branch=True` or `prefer_same_branch=True` in `IKOptions`, constrain joint bounds on the approach target away from envelope limits, or apply a 180° tool symmetry rotation offset.
- **Commanded vs. sensed joint noise on retreat motions**:
  - When an approach motion terminates near contact, slight sensor noise on the live sensed joint state can shift forward kinematics into collision at the start of the retreat motion (`Invalid initial joint configuration... collision(s) detected`).
  - Configure per-segment `collision_rules` (`is_excluded: true`) for expected contact pairs during the initial departure segment of the retreat motion, and ensure non-zero collision margins (>= 2 mm) for free-space targets.
- **Planner debug traces for low-level collision checks**:
  - Direct calls to `CheckCollisions` fail with `MotionPlanningError/collision_error:validation` without generating a world snapshot.
  - Copy the failing joint configuration vector and call `ai.intrinsic.move_robot` with that configuration to route through `MotionPlannerService/PlanTrajectory`, creating a full scene debug snapshot.
- **Quaternion normalization and antipodal equality**:
  - Ensure all orientation quaternions in `CartesianMotionTarget` or constraint messages are normalized (`norm == 1.0`) to avoid solver failures during constraint intersection.
  - Compare poses and orientations using `Pose3.__eq__` or `Rotation3.__eq__`, which correctly treat antipodal quaternions (+q and -q) as equal rotations, unlike raw `Quaternion.__eq__`.
- **Motion planner cache memory footprint vs. dynamic frames**:
  - Normal trajectory cache warm-up (`PlanTrajectoryCache`, storing up to 2,000 trajectories at ~2 MB each) causes `motion-planner-service` RSS memory to grow from ~0.4 GB to ~2.1–2.7 GB before LRU eviction begins. Maintain running planner pods when RSS is below 2.7 GB.
  - Set `skip_fuzzy_cache_check=True` in `MotionPlanningOptions` when relative or path constraints reference frames that move between calls.
- **Scene object density and `compute_ik` latency**:
  - Search latency for `compute_ik` scales linearly with scene object count (from ~20 ms with 4 objects to >200 ms with 180+ objects). In dense scenes, pass explicit `collision_settings` in `IKOptions` to exclude static background objects.
- **Parallel branch footprints and "lock_the_universe"**:
  - Motion planning and trajectory tracking skills require `lock_the_universe: true` in their footprint to guarantee collision-free execution and prevent concurrent mutation of scene geometry. Prohibit parallel behavior tree branches from executing concurrent motion tasks.
  - The base class `skill_interface.Skill.get_footprint()` returns a `Footprint` with `lock_the_universe=True` by default. Retain universe locking for motion skills. Only set `lock_the_universe=False` when actions are purely read-only or independent, declaring explicit equipment leases.

## `ICON` real-time cycle timing, session management, and hardware fault propagation

- **Mandatory ingress instance name flag**:
  - Always pass `--instance_name=icon` when querying ICON over Envoy ingress (`localhost:17080`):
    ```bash
    inctl icon status --instance_name=icon --address=localhost:17080
    ```
  - Omitting `--instance_name=icon` fails with `rpc error: code = Unimplemented` because the ingress gateway requires the `x-resource-instance-name: icon` header.
- **Four-phase cycle timing breakdown (`rs`, `proc`, `ac`, `exec`)**:
  - When an EtherCAT bus or robot controller faults with cycle time violations, inspect the four-phase timing signature in `kubectl logs -n app-resources deployment/rs-icon`:
    ```text
    WARNING ... Long duration between read_status_calls: 3.836198ms expected: 2ms [previous: rs=1.062us, proc=51.455us, ac=8.97us, exec=3.774711ms]
    ```
  - `rs` (`ReadStatus`): Hardware module read stall; the module blocked reading from fieldbus or network sockets.
  - `proc` (Processing): Controller / action computation overrun; kinematics, collision checks, or active callbacks exceeded budget.
  - `ac` (`ApplyCommand`): Hardware module write stall; the module blocked writing outputs or shared buffers.
  - `exec` (Futex wait): Host core preemption or wakeup delay; ICON and hardware modules finished in microseconds and yielded on the futex, but the host OS failed to wake the real-time thread on time (`exec` > 95% of cycle).
- **Pagefault verification (`PagefaultInfo`)**:
  - Inspect periodic `PagefaultInfo [Abs Major | Abs Minor | Rel Major | Rel Minor]` logs in `rs-robot-controller`.
  - When `Rel Major == 0` and `Rel Minor == 0`, memory locking (`mlockall`) is active and page faults did not cause the stall.
- **Overrun-to-underrun EtherCAT cascade (`EC_NOTIFY_FRAME_RESPONSE_ERROR`)**:
  - After a long `exec` wakeup delay (~3.8 ms), the real-time thread executes a catch-up cycle with an ultra-short sleep (~95 µs).
  - Distributed-clock EtherCAT slaves synchronize at ~2/3 of the nominal 2.0 ms cycle (~1333 µs), so sending the next frame ~95 µs later arrives before slaves processed the previous frame, triggering `EC_NOTIFY_FRAME_RESPONSE_ERROR (0x1000a)`.
  - Always inspect the `Long duration between read_status_calls` log *preceding* `EC_NOTIFY_FRAME_RESPONSE_ERROR` to locate the true root cause.
- **Shared memory lockfile deadlock remediation**:
  - When `rs-ur-module` or `rs-icon` crashes abruptly, a stale lockfile persists at `/tmp/intrinsic_icon/ur_module.lock` on the host filesystem volume.
  - Subsequent pod restarts fail to acquire the shared memory segment, remaining in `CrashLoopBackOff` with `Resource temporarily unavailable`.
  - Delete `/tmp/intrinsic_icon/ur_module.lock` on the host before restarting the container, then clear faults via `inctl icon clear-faults --instance_name=icon --address=localhost:17080`.
- **Simulation clock synchronization and queue timeout signatures**:
  - In simulation, Gazebo drives ICON's real-time clock. Distinguish between two status queue timeout signatures:
    - `Timed out waiting for first message from Part status queue`: The real-time loop remained unstarted since startup without publishing status (verify the Gazebo clock ticking thread).
    - `Timed out reading from Part status queue`: The real-time loop was previously active but stalled mid-motion (typically caused by a secondary sensor hardware module blocking in `ReadStatus`).
- **Network switch IRQ storms and non-RT TCP priority inversion**:
  - An unmanaged switch between the real-time NIC and robot controller floods the isolated CPU core with broadcast IRQ context switches. Use a direct point-to-point cable on validated real-time NICs (`Intel I219-LM` or `I225-V`).
  - When a robot controller faults with `MOTN-603` while ICON shows zero cycle overruns, a non-RT (`SCHED_OTHER`) TCP thread sharing the NIC transmit lock blocked high-priority UDP packets from `ApplyCommand`.
- **Session slot allocation mechanics and `AlreadyExistsError`**:
  - Part-controlling sessions are deterministically assigned to the slot matching their lowest requested part index (`min_index`).
  - Monitoring sessions (requesting zero parts) scan forward through trailing monitoring slots for the first inactive entry.
  - When `AlreadyExistsError` occurs, verify that prior skills released their session context managers.
  - Inactive session slots retain old part references; repeated `Cancelling session due to operational hardware state 'faulted_connected'` logs during hardware faults are status loop inspection artifacts, not active connection attempts.
- **Stale streaming I/O buffers on pause and resume**:
  - When updating session data with `stop_active_actions` enabled, active action indices are cleared but streaming channels persist if not explicitly reset.
  - When resuming motion, re-instantiate the action or flush streaming I/O channels to prevent the robot from jumping toward stale queued pose targets.
- **Operational vs. cell-control hardware fault propagation**:
  - If an **operational** hardware module (robot arm, force-torque sensor) faults, ICON disables operational modules but keeps **cell-control** modules (safety PLCs, grippers) enabled.
  - Clear faults via `inctl icon clear-faults --instance_name=icon --address=localhost:17080` after physical E-stops are disengaged.
  - Distinguish ICON hardware module resets from independent asset device services; if an upstream sensor service pod fails, restart that specific service via `inctl service state restart <name> --address=localhost:17080`.
- **Verified ICON action signatures and 6x6 matrix validation**:
  - Trajectory tracking is registered as `"intrinsic.trajectory_tracking"` (not `"intrinsic.trajectory_tracking_action"`).
  - Standalone `"intrinsic.wait_for_settling_action"` is absent on the server; wait on the `"intrinsic.is_settled"` state variable integrated into `"intrinsic.point_to_point_move"`, `"intrinsic.trajectory_tracking"`, and `"intrinsic.stop"`.
  - In `intrinsic.icon.proto.matrix_conversions.from_ndarray`, explicitly verify `matrix.shape == (6, 6)` before passing NumPy arrays, as internal chained inequalities (`matrix.shape[0] != matrix.shape[1] != 6`) evaluate `False` for square 3x3 or 4x4 matrices.
  - In `intrinsic_proto.icon.HalForceTorqueSensorPartConfig`, center-of-gravity `ft_t_cog` requires 3 entries (`[x, y, z]`), and `sensed_wrench_deadband` is nested inside `force_control_settings`.
  - On Universal Robots (`ur_module`), ignore false-positive `Failed to tare within the specified number of cycles` logs after protective stops unless accompanied by `Failed to send 'retare' command to ur_robot.`.
  - In `GripperClient`, pass `turn_on=True` explicitly when calling `blow_off()` to activate air blow-off.
- **Superposition safety margins**:
  - When using superimposed tracking with online perturbations, enforce `system_limits > planning_limits` (`limits_margin = system_limits - planning_limits`). Update application limits in the world if payload-dependent limits change.

## Paired deterministic safety guardrails

1. **Initial joint deviation verification**: Verify that initial joint configuration error is < 0.1 rad before commanding trajectory tracking; do not force trajectory execution from unaligned configurations without an approach move.
2. **Fault clearing through lifecycle commands**: Clear faults via `inctl icon clear-faults --instance_name=icon --address=localhost:17080` after physical safety stops are disengaged; do not restart hardware module pods (`rs-ur-module`, `rs-icon`) directly while `rs-robot-controller` is attached.
3. **Concurrent session part isolation**: Partition controlled parts cleanly across concurrent sessions or use zero-part monitoring sessions (`parts = []`); do not launch concurrent control sessions requesting overlapping part sets.
4. **Safe cache bypass on dynamic frames**: Set `skip_fuzzy_cache_check=True` in `MotionPlanningOptions` when constraints reference moving coordinate frames; do not invoke `clear_cache()` concurrently while planning requests are actively executing.

## `System 2` reflection checklist and anti-thrashing circuit breakers

### `System 2` pre-execution reflection checklist

Before dispatching high-cost motion planning requests or state-mutating ICON operations, verify these 5 diagnostic preconditions:
1. **Pose and orientation normalization**: Have all target quaternions been normalized (`norm == 1.0`) and orientation comparisons evaluated via `Pose3.__eq__` or `Rotation3.__eq__` rather than raw quaternion equality?
2. **Matrix dimension assertion**: Has `matrix.shape == (6, 6)` been asserted before passing Cartesian stiffness or damping arrays to `from_ndarray`?
3. **Branch continuity on linear segments**: Has `ensure_same_branch=True` been configured for multi-segment motions to prevent 2*pi wrist flips?
4. **Hardware safety interlock status**: Has `inctl icon status --instance_name=icon --address=localhost:17080` confirmed `BUTTON_STATUS_DISENGAGED` before calling `inctl icon clear-faults`?
5. **Resource footprint scoping**: Has `SkillInterface.GetFootprint()` been verified to specify `lock_the_universe: false` via `footprint_pb2.Footprint(lock_the_universe=False)` with targeted part reservations for parallel execution?

### Anti-thrashing circuit breakers

1. **IK candidate inspection limit**: If `compute_ik` fails 2 consecutive times for a target pose, halt search; inspect rejected configurations in `result.ik_debug_information.ik_solutions` to identify the limiting joint or collision pair rather than blindly modifying waypoints.
2. **Hardware safety stop circuit breaker**: If `inctl icon clear-faults` fails with `ModeOfSafeOperation: UNKNOWN`, immediately halt automated retries; inspect physical E-stop switches and teach pendant interlocks.
3. **Real-time cycle overrun circuit breaker**: If `Long duration between read_status_calls` logs show `exec` > 95% across 2 consecutive cycles, halt application-level code modifications; inspect host CPU core isolation (`isolcpus`), network switch broadcast traffic, and NIC priority inversions.
4. **Planning timeout and lockfile deadlock circuit breaker**: If `plan_trajectory` or `move_robot` times out (> 60 seconds), halt retries; inspect World Service logs for `Large volume mesh detected` to verify CAD mesh scale factors. If a controller pod remains in `CrashLoopBackOff`, inspect `/tmp/intrinsic_icon/ur_module.lock` on the host rather than restarting the pod repeatedly.
