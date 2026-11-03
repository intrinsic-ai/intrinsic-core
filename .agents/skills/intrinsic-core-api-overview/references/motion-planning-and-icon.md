# Motion planning and `ICON` real-time control reference

This reference covers `intrinsic_proto.motion_planning.v1.MotionPlannerService` trajectory planning, `intrinsic.icon.python.icon_api.Client` session lifecycles, `intrinsic_proto.icon.v1.IconApi` real-time control sessions, action signatures, part slot claiming, analog and digital I/O (ADIO) streaming, safety status aggregation dominance rules, two-tier hardware fault propagation, force-torque taring, Cartesian space limits, inverse kinematics options, low-level joint controllers, and streaming outputs.

## Core `gRPC` services and `RPC`s

| Fully qualified gRPC method | Request / response types | When and why to use |
| :--- | :--- | :--- |
| `intrinsic_proto.motion_planning.v1.MotionPlannerService/PlanTrajectory` | `intrinsic_proto.motion_planning.v1.MotionPlanningRequest` -> `intrinsic_proto.motion_planning.v1.TrajectoryPlanningResponse` | Compute collision-free, time-parameterized joint trajectories (`trajectory.discretized`) between joint or Cartesian waypoints against a specified world (`world_id="world"`). |
| `intrinsic_proto.motion_planning.v1.MotionPlannerService/ComputeIk` | `intrinsic_proto.motion_planning.v1.ComputeIkRequest` -> `intrinsic_proto.motion_planning.v1.ComputeIkResponse` | Solve inverse kinematics for one or more target poses with collision checking, solution limits (`max_num_solutions`), and branch consistency preferences (`prefer_same_branch`, `ensure_same_branch`). |
| `intrinsic_proto.motion_planning.v1.MotionPlannerService/ClearCache` | `google.protobuf.Empty` -> `google.protobuf.Empty` | Invalidate cached collision-free paths after modifying collision geometry or spawning new obstacles in `intrinsic_proto.world.ObjectWorldService`. Do not call concurrently with active planning. |
| `intrinsic_proto.icon.v1.IconApi/OpenSession` | `stream intrinsic_proto.icon.v1.OpenSessionRequest` <-> `stream intrinsic_proto.icon.v1.OpenSessionResponse` | Establish an exclusive real-time control session (at `ServerConfig.frequency_hz`, e.g., `500 Hz` / 2.0 ms on UR hardware modules or `1 kHz` on 1 kHz controllers) over one or more hardware parts (such as `"arm"`, `"adio"`, `"gripper"`, `"ft_sensor"`), or a zero-part monitoring session (`parts = []`) for non-locking freestanding reactions. Requires `--instance_name=icon` when calling via ingress (`localhost:17080`). |
| `intrinsic_proto.icon.v1.IconApi/WatchReactions` | `intrinsic_proto.icon.v1.WatchReactionsRequest` -> `stream intrinsic_proto.icon.v1.WatchReactionsResponse` | Stream real-time reaction events triggered by action state variables (such as `"intrinsic.outputs_set"`, `"intrinsic.is_settled"`, or `"intrinsic.is_done"`) or freestanding part state variable paths (`@{part_name}.ADIOPart.di.{block_name}[{index}]`) during an active `OpenSession` stream. |
| `intrinsic_proto.icon.v1.IconApi/GetStatus` | `intrinsic_proto.icon.v1.GetStatusRequest` -> `intrinsic_proto.icon.v1.GetStatusResponse` | Query point-in-time status for all parts managed by the ICON server (`PartStatus` map) as well as cell-level safety state (`intrinsic_proto.icon.SafetyStatus`). Call before opening a control session to verify `requested_behavior == REQUESTED_BEHAVIOR_NORMAL_OPERATION`. |
| `intrinsic_proto.icon.v1.IconApi/GetOperationalStatus` | `intrinsic_proto.icon.v1.GetOperationalStatusRequest` -> `intrinsic_proto.icon.v1.GetOperationalStatusResponse` | Query top-level operational state (`OperationalState`) and fault reason strings across the ICON server before inspecting detailed part statuses. |
| `intrinsic_proto.icon.v1.IconApi/GetConfig` | `intrinsic_proto.icon.v1.GetConfigRequest` -> `intrinsic_proto.icon.v1.GetConfigResponse` | Retrieve static part configurations (`GenericPartConfig`), including Cartesian limits (`cartesian_limits_config`), ADIO block definitions (`adio_config`), and force-torque deadband settings. |
| `intrinsic_proto.icon.v1.IconApi/ListActionSignatures` | `intrinsic_proto.icon.v1.ListActionSignaturesRequest` -> `intrinsic_proto.icon.v1.ListActionSignaturesResponse` | Introspect real-time action descriptors (`ActionSignature`), required part slots, parameter types, state variable names, and streaming input/output schemas (`streaming_input_infos`, `streaming_output_info`). Trajectory tracking is registered as `"intrinsic.trajectory_tracking"`. |
| `intrinsic_proto.icon.v1.IconApi/OpenWriteStream` | `stream intrinsic_proto.icon.v1.OpenWriteStreamRequest` <-> `stream intrinsic_proto.icon.v1.OpenWriteStreamResponse` | Stream high-frequency real-time inputs to an active action inside an open session. |
| `intrinsic_proto.icon.v1.IconApi/GetLatestStreamingOutput` | `intrinsic_proto.icon.v1.GetLatestStreamingOutputRequest` -> `intrinsic_proto.icon.v1.GetLatestStreamingOutputResponse` | Poll the most recent real-time streaming output (`intrinsic_proto.icon.StreamingOutput`) published by an active action. Blocks on first poll until output is emitted; subsequent calls return cached output. |
| `intrinsic_proto.icon.v1.IconApi/SetLoggingMode` | `intrinsic_proto.icon.v1.SetLoggingModeRequest` -> `intrinsic_proto.icon.v1.SetLoggingModeResponse` | Dynamically toggle ICON real-time control loop telemetry between `LOGGING_MODE_THROTTLED` and `LOGGING_MODE_FULL_RATE`. |
| `intrinsic_proto.icon.v1.IconApi/GetLoggingMode` | `intrinsic_proto.icon.v1.GetLoggingModeRequest` -> `intrinsic_proto.icon.v1.GetLoggingModeResponse` | Inspect the active `intrinsic_proto.icon.LoggingMode` on the ICON server before modifying telemetry rates. |
| `intrinsic_proto.icon.v1.JoggingService/GetAvailableParts` | `intrinsic_proto.icon.v1.AvailablePartsRequest` -> `intrinsic_proto.icon.v1.AvailablePartsResponse` | Discover jogging capabilities (`cartesian_jogging_available`), valid static/tool frames (`static_jogging_frames`, `tool_jogging_frames`), and watchdog timeout (`stop_timeout`). |
| `intrinsic_proto.icon.v1.JoggingService/JogRobot` | `stream intrinsic_proto.icon.v1.JoggingRequest` <-> `stream intrinsic_proto.icon.v1.JoggingResponse` | Open an exclusive bidirectional jogging session and stream normalized velocity keepalives (`[-1.0, 1.0]`) at an interval strictly less than `stop_timeout`. |

## Deterministic safety guardrails

To prevent hardware collisions, control loop stalls, and unrecoverable session deadlocks, enforce these paired deterministic guardrails:

1. **Payload configuration outside active sessions**:
   - Configure robot payload mass, center of gravity, and inertia tensor (`icon_client.set_payload(...)`) strictly before opening an ICON control session or after terminating it.
   - Do not call `set_payload` while an active session holds the target arm part slot (`FAILED_PRECONDITION`).
2. **Non-locking ADIO inspection**:
   - Inspect and wait on digital inputs using zero-part monitoring sessions (`parts = []`) with freestanding reactions (`@{part_name}.ADIOPart.di.{block_name}[{index}]`).
   - Do not claim exclusive `"adio"` part slots for passive condition polling, as claiming the slot blocks all concurrent services and skills from reading or writing ADIO lines.
3. **Physical settling verification**:
   - Verify mechanical manipulator settling by attaching reaction triggers to `"intrinsic.is_settled"` on motion actions (`intrinsic.point_to_point_move`, `intrinsic.trajectory_tracking`, `intrinsic.stop`).
   - Do not rely on `is_done()` or deleted legacy settling actions (`intrinsic.wait_for_settling_action`) for physical convergence before taring force-torque sensors or capturing camera images.
4. **Fault remediation via lifecycle interfaces**:
   - Clear operational hardware faults through `inctl icon clear-faults --instance_name=icon --address=localhost:17080` and resolve physical E-stop conditions before retrying motion commands.
   - Do not restart hardware module pods (`rs-ur-module`) or delete containers directly while `rs-robot-controller` is running, as deleting shared-memory peripherals leaves stale futexes and deadlocks the real-time controller.

## System 2 reflection and anti-thrashing circuit breakers

### Pre-execution `System 2` reflection checklist

Before executing state-mutating commands (such as opening exclusive control sessions, commanding trajectory tracking, setting controller gains, or configuring Cartesian limits), pause and perform this 5-point self-critique:

1. **Controller state check**: Has `inctl icon status --instance_name=icon --address=localhost:17080` verified `Operational Status: ENABLED` and `safety_status.requested_behavior == REQUESTED_BEHAVIOR_NORMAL_OPERATION`?
2. **Kinematic and derivative bound check**: Are translational position, velocity, and acceleration limits strictly compliant with `CartesianLimits` sign invariants (all min velocity/acceleration bounds `<= 0.0`, all max bounds `>= 0.0`), and is initial joint deviation below `0.1 rad`?
3. **Part slot isolation check**: Does the session request only the strictly necessary part slots, and are concurrent sessions partitioned to prevent overlapping part ownership?
4. **Cleanup and rollback guarantee**: Is the control session wrapped in a Python context manager (`with icon_client.start_session()`), and is physical motion wrapped in a `try...finally` block that guarantees rollback to a safe home pose upon failure?
5. **Schema and type validation**: Are array dimensions explicitly verified before proto serialization (e.g., `matrix.shape == (6, 6)` for `Matrix6d`, exactly 7 elements for `CartState.pose`, native Python `float` sequences)?

### Anti-thrashing circuit breakers

- **Motion planning & IK solver circuit breaker**:
  - Cap consecutive planning or inverse kinematics retries at **2 attempts**.
  - If `ComputeIk` or `PlanTrajectory` fails twice consecutively, halt waypoint generation immediately.
  - Branch to diagnostic inspection: inspect `ComputeIkResult.ik_debug_information.ik_solutions` to identify rejected collision or joint limit constraints, verify `world_id="world"`, inspect `Large volume mesh detected` warnings in world service logs, check collision visualization (`Alt + G`), and verify application joint limits. Do not churn random waypoints.
- **ICON session and controller fault circuit breaker**:
  - Cap session opening retries at **2 attempts**.
  - If `AlreadyExistsError` or `State: FAULTED` recurs, halt session creation immediately.
  - Branch to diagnostic commands: check `kubectl logs -n app-resources deployment/rs-icon`, verify whether pending safety actions are delaying startup (`Delaying start of session because of a safety action`), inspect `/tmp/intrinsic_icon/ur_module.lock` on the host, and execute `inctl icon clear-faults`. Do not blindly reopen sessions in a loop.

## Motion planning and trajectory execution via `MotionPlannerClient` and `icon_api.Client`

### `API` usage hints for `MotionPlannerClient` and `icon_api.Client`

- **Instantiate `MotionPlannerClient` directly with `world_id` and `MotionPlannerServiceStub`**:
  - The factory `MotionPlannerClient.for_solution(solution)` returns a base class wrapper that lacks `compute_ik` and `plan_trajectory`.
  - Always instantiate `intrinsic.motion_planning.motion_planner_client.MotionPlannerClient` directly by passing `world_id="world"` (or a cloned world ID) and a `MotionPlannerServiceStub` connected to the workcell ingress channel:
    ```python
    from intrinsic.motion_planning import motion_planner_client
    from intrinsic.motion_planning.proto.v1 import motion_planner_service_pb2_grpc

    mp_stub = motion_planner_service_pb2_grpc.MotionPlannerServiceStub(channel)
    mp_client = motion_planner_client.MotionPlannerClient(
        world_id="world", stub=mp_stub
    )
    ```
- **Unwrap both `.trajectory` and `.discretized` from `plan_trajectory` results**:
  - `mp_client.plan_trajectory(...)` returns a `TrajectoryPlanningResponse` wrapper whose planned trajectory is nested under `.trajectory.discretized` (`intrinsic_proto.icon.JointTrajectoryPVA`).
  - Pass `plan_response.trajectory.discretized` (not `plan_response` or `plan_response.trajectory`) to `create_action_utils.create_trajectory_tracking_action(...)`.
- **Configure payloads (`icon_client.set_payload(...)`) outside active ICON sessions**:
  - Calling `icon_client.set_payload(...)` while an `OpenSession` stream is holding the target arm part fails with `FAILED_PRECONDITION`. Always configure or update robot payload mass, center of gravity, and inertia tensor before opening a control session (or after closing the active session).
- **Precede trajectory tracking with a point-to-point move (`< 0.1 rad` initial deviation)**:
  - `create_trajectory_tracking_action` requires the physical robot's current joint positions to match the first waypoint of `trajectory.discretized` within `< 0.1 rad` (`kMaxInitialJointPositionDeviation`). If the robot's initial configuration deviates from `trajectory.discretized.waypoints[0]` by more than `0.1 rad`, starting the trajectory tracking action aborts immediately. Always execute a point-to-point move (`create_action_utils.create_point_to_point_move_action`) to the initial waypoint before starting trajectory tracking.
- **Wait on `"intrinsic.is_settled"` rather than `is_done()` for physical convergence**:
  - Action completion (`is_done()`) fires as soon as the final trajectory setpoint is emitted to the controller buffer, while the physical manipulator may still have residual velocity or tracking lag. Note that standalone `"intrinsic.wait_for_settling_action"` is removed; settling detection is built into `intrinsic.point_to_point_move`, `intrinsic.trajectory_tracking`, and `intrinsic.stop` via `"intrinsic.is_settled"`. Attach reactions or completion checks directly to `"intrinsic.is_settled"` before triggering camera captures, force taring, or precision insertion.
- **Bypass fuzzy cache hits on dynamic coordinate frames**:
  - When executing motions with path constraints tied to coordinate frames that move between calls, set `skip_fuzzy_cache_check=True` in `MotionPlanningOptions` to prevent the planner from returning a cached trajectory computed for the previous frame pose.
  - Do not invoke cache clearing (`clear_cache`) concurrently while a motion planning request is actively executing.
  - Note that RSS memory in `motion-planner-service` pods grows from `~0.4 GB` to `~2.1-2.7 GB` during normal operation because `PlanTrajectoryCache` stores up to 2,000 cached trajectory results before LRU eviction begins. Do not restart planner pods for RSS growth under `2.7 GB` unless an out-of-memory error occurs.
- **Commanded vs. sensed joint noise on back-to-back retreat motions**:
  - When an approach motion succeeds at near-contact, but an immediate retreat motion fails at `Segment 0` with `Invalid initial joint configuration... collision(s) detected`, sensor settling or measurement noise shifted the live sensed configuration into collision.
  - Configure per-segment `collision_rules` to exclude expected contact pairs (`is_excluded: true`) during the initial departure segment of the retreat motion, and maintain non-zero collision margins (`>= 2 mm`) for free-space targets.
- **Dynamic collision exclusions across object pick-and-place cycles**:
  - Attaching an object adds a collision exclusion between the gripper and the attached entity; detaching removes that exclusion. If a scene exclusion existed for that pair prior to attachment, configure the detach operation to preserve existing scene exclusions.
- **CAD mesh volume scaling and collision check latency**:
  - If `move_robot` times out or planning stalls for minutes, inspect World Service logs (`kubectl logs -n app-intrinsic-base deployment/world`) for `Large volume mesh detected, collision computation may be slow`. Importing CAD geometry in millimeters instead of meters inflates bounding volumes by `1e9x` (`> 200 m^3`) and forces the collision checker into expensive octree traversal. Rescale the mesh to meters (`0.001` scale factor) or substitute simplified convex collision hulls.
- **Superposition safety margins (`system_limits` vs. `planning_limits`)**:
  - When using superimposed tracking (`intrinsic.trajectory_tracking` with online perturbations), configure `planning_limits` strictly below `system_limits`. The margin `limits_margin = system_limits - planning_limits` accommodates online trajectory generator residuals without triggering hardware limit aborts.

## Real-time control sessions, action signatures, and part slot claiming

### Hardware-module-dependent control frequency

Real-time control loop frequency (`ServerConfig.frequency_hz`) is configured per hardware module rather than hardcoded:
- On Universal Robots controllers (`ur_module`), the control frequency is typically **`500 Hz`** (a 2.0 ms cycle period).
- On high-speed controllers, the control frequency can reach **`1 kHz`** (a 1.0 ms cycle period).
- Inspect the active loop frequency via:
  ```bash
  inctl icon config --instance_name=icon --address=localhost:17080
  ```
- All real-time timing deadlines, derivative filter cutoffs, and action execution rates are evaluated relative to this hardware-module-dependent cycle time.

### Slot allocation and multi-part session lifecycle

When interacting with `intrinsic_proto.icon.v1.IconApi/OpenSession`:

- **Exclusive part slot allocation**:
  - The initial `OpenSessionRequest` specifies `initial_session_data.allocate_parts`, claiming exclusive control of named hardware parts (such as `"arm"`, `"adio"`, `"gripper"`, `"ft_sensor"`).
  - Part-controlling sessions are assigned deterministically to the slot matching their lowest requested part index (`min_index`). Monitoring sessions (`parts = []`) scan forward through trailing monitoring slots.
  - Wrap sessions in Python context managers (`with icon_client.start_session() as session:`) to guarantee channel release and prevent `AlreadyExistsError`.
- **Concurrent multi-part overlap and uncontrolled part safety triggers**:
  - If two concurrent sessions request overlapping sets of parts with different minimum part indices (e.g., Session A requests `{0, 2}` and Session B requests `{1, 2}`), both sessions can start. However, when either session terminates, its cleanup marks all of its parts as uncontrolled, triggering a safety stop on the overlapping part and halting the surviving session. Always partition controlled parts cleanly across concurrent sessions.
- **Stale streaming I/O buffers when halting active actions**:
  - Updating session data with `stop_active_actions` enabled clears active action indices, but action instances and streaming channels persist. When designing cyclic pause/resume reactions, re-instantiate the action or flush streaming channels before resuming motion to prevent jumps toward stale setpoints.
- **Session startup delayed by unfinished safety actions**:
  - If a requested part is still executing a safety action or stop trajectory from a prior fault, session startup remains pending and logs: `Delaying start of session because of a safety action: ...`. Inspect `rs-icon` logs (`kubectl logs -n app-resources deployment/rs-icon`) when `OpenSession` blocks without an immediate error.
- **Two-step non-RT to RT bridge handshake**:
  - Opening a session and installing actions requires a two-step handshake across the non-real-time to real-time bridge (session creation followed by session data installation). If a hardware fault occurs in the millisecond window between session creation and action installation, the real-time loop aborts the session and closes pending bridge channels with `AbortedError`.
  - In Python skills waiting on an action completion flag (`done.wait()`), ensure reaction stream error handlers immediately signal waiting flags so the skill exits cleanly rather than blocking until executive timeout.
- **Diagnostic note on repeated session cancellation logs (`faulted_connected`)**:
  - When a session is cancelled or deactivated, its communication channels are closed (`IsActive()` becomes `false`), but the session slot retains its prior requested part index list until overwritten by a new session in that slot.
  - If ICON logs repeated messages of the form `Cancelling session due to operational hardware state 'faulted_connected'` during a hardware fault, recognize that this is a log artifact of the status loop iterating over inactive session slots that still hold old part index references, rather than new sessions actively attempting to connect.
- **`WatchReactions` stream lifecycle**:
  - `intrinsic_proto.icon.v1.IconApi/WatchReactions` must only be called while an `OpenSession` stream with the matching `session_id` is active.
  - Upon stream initialization, the server immediately emits a single empty `WatchReactionsResponse` to confirm watcher readiness.
  - Only one `WatchReactions` stream is permitted per session; opening additional concurrent streams fails with `UNAVAILABLE`.

### Built-in action signatures, slots, and state variables

- **`"intrinsic.trajectory_tracking"`**, **`"intrinsic.point_to_point_move"`**, and **`"intrinsic.stop"`**:
  - Claims part slot `"arm"` (requires `FEATURE_INTERFACE_JOINT_POSITION`, `FEATURE_INTERFACE_JOINT_POSITION_SENSOR`, `FEATURE_INTERFACE_JOINT_LIMITS`, and `FEATURE_INTERFACE_JOINT_VELOCITY_ESTIMATOR`).
  - The trajectory tracking action signature is registered as `"intrinsic.trajectory_tracking"`.
  - Exposes state variables `"intrinsic.is_done"`, `"intrinsic.is_settled"`, `"intrinsic.is_stopped"`, `"elapsed_time_seconds"`, and `"maximum_joint_velocity_magnitude"`.
  - Note: standalone `"intrinsic.wait_for_settling_action"` is removed; settling detection is built directly into `intrinsic.point_to_point_move`, `intrinsic.trajectory_tracking`, and `intrinsic.stop` via `"intrinsic.is_settled"`.
- **`"intrinsic.adio"`** (`intrinsic_proto.icon.actions.proto.ADIOFixedParams`):
  - Claims part slot `"adio"` (requires `FEATURE_INTERFACE_ADIO` and a configured `GenericAdioConfig`).
  - Exposes state variables `"intrinsic.all_inputs_match"` (`TYPE_BOOL`, true when all analog comparisons and digital bit expectations match live inputs) and `"intrinsic.outputs_set"` (`TYPE_BOOL`, true on the `Sense()` cycle immediately following the `Control()` cycle in which output values were written).
  - The server registers `"intrinsic.all_inputs_match"` and `"intrinsic.outputs_set"`. Registering a reaction on `"intrinsic.any_inputs_match"` fails with `NOT_FOUND`.
  - At least one of `expectations` or `outputs` must be populated in `ADIOFixedParams`, or action creation fails with `INVALID_ARGUMENT`. If `expectations` is empty, `"intrinsic.all_inputs_match"` always evaluates to `false`.
- **`"intrinsic.simple_gripper"`** (`intrinsic_proto.icon.actions.proto.SimpleGripperFixedParams`):
  - Claims part slot `"gripper"` (requires `FEATURE_INTERFACE_SIMPLE_GRIPPER`).
  - Accepts `command = GRASP` (`1`) or `RELEASE` (`2`); `UNKNOWN` (`0`) is rejected during action creation.
  - Exposes state variables `"intrinsic.is_done"`, `"intrinsic.simple_gripper.sent_command"` (true on the cycle after `SetGripperCommand` succeeds), `"intrinsic.grasped"`, and `"intrinsic.released"`.
  - For open-loop pneumatic grippers without reed switches, attach reactions to `IsTrue("intrinsic.simple_gripper.sent_command")`. Use `IsTrue("intrinsic.is_done")` only when the underlying gripper part provides closed-loop feedback.
- **`"intrinsic.tare_force_torque_sensor"`** (`intrinsic_proto.icon.actions.proto.TareForceTorqueSensorParams`):
  - Claims part slot `"ft_sensor"` (requires `FEATURE_INTERFACE_FORCE_TORQUE_SENSOR`).
  - Exposes state variable `"intrinsic.is_done"` (true when averaging finishes across `num_taring_cycles` and `TareIsDone()` reports completion).
  - Set `num_taring_cycles: 100` in production admittance workflows to reduce sample noise.

## Non-locking `ADIO` input waits, safety status hierarchy, and fault propagation

### Non-locking `ADIO` input waits and write-only output command buffers

#### Why waiting on digital inputs via `ai.intrinsic.dio_wait_for_input` uses a zero-part monitoring session

Starting the `"intrinsic.adio"` action with `ADIOFixedParams.expectations` requires claiming the `"adio"` part slot in `intrinsic_proto.icon.v1.IconApi/OpenSession`, which holds an exclusive lock on the ADIO part and blocks any concurrent skill or service from reading or writing ADIO signals on that part.

To wait on digital inputs without locking the `"adio"` part slot:
- `ai.intrinsic.dio_wait_for_input` opens a **zero-part monitoring session** (`icon::Session::Start(icon_channel, {})` / `parts = []`).
- It registers a **freestanding reaction** directly on the part state variable path:
  `@{part_name}.ADIOPart.di.{block_name}[{index}]`
- It polls `intrinsic_proto.icon.v1.IconApi/WatchReactions` in 1-second intervals (`step_timeout = min(remaining_time, 1s)`) to allow immediate cancellation without holding an exclusive lock on the `"adio"` part.

#### Why digital output blocks are write-only command buffers and how to read back output status

In `HalADIOPartConfig`, `digital_outputs` blocks (such as `export_name: "standard_out"`) are **write-only command buffers** that do not expose readback state.
- To read back current digital output states via `ai.intrinsic.dio_read_input` or `intrinsic_proto.icon.v1.IconApi/GetStatus` (`PartStatus.adio_state`), map the hardware module's output status interfaces (such as `"standard_digital_output_status"`, `"configurable_digital_output_status"`, and `"tool_digital_output_status"`) as `digital_inputs` entries inside `HalADIOPartConfig` (for example, `export_name: "standard_out_status"`).
- Query that mapped input block name (`"standard_out_status"`) to inspect the live hardware output status.

#### Additional `ADIO` configuration hints

- **Hard 32-signal real-time limit per block (`kMaxValuesPerBlock = 32`)**:
  - In real-time ADIO blocks (`intrinsic_proto.icon.DigitalBlock` and `AnalogBlock`), the server enforces a hard limit of at most 32 signals per block (`kMaxValuesPerBlock = 32`).
  - Specifying a block size > 32 returns `INVALID_ARGUMENT: The parameter size(...) exceeds kMaxValuesPerBlock(32)`. Always partition EtherCAT or fieldbus I/O mappings into blocks of at most 32 signals (bit indices 0–31).
- **Sparse index bitmasking in `ADIOFixedParams`**:
  - Maps inside `DigitalBlock`, `AnalogOutputBlock`, and `AnalogInputBlock` (`values_by_index` and `comparisons_by_index`) are sparse: unset indices are masked out and left unchanged on the hardware interface. Passing any index >= block_size (as defined in `GenericAdioConfig`) causes action creation to fail with `OUT_OF_RANGE`.
- **Chaining multi-part output writes**:
  - When setting outputs across multiple ADIO parts simultaneously, multi-part helper methods create one `"intrinsic.adio"` action descriptor per part, chain them sequentially using `.WithReaction(ReactionDescriptor(IsTrue("intrinsic.outputs_set")).WithRealtimeActionOnCondition(next_action_id))`, and attach a watcher handle to the final action with a `5.0s` timeout (`kIconSetOutputTimeout`).
- **Simulation interface overrides in `DigitalIO`**:
  - On Fanuc and KUKA simulated hardware modules, default FlatBuffer interface naming (`<device_name>_digital_{input,output}_{status,command}`) may not match the controller simulator's exposed buffers. Set `command_interface_name_sim_only: "output"` and `status_interface_name_sim_only: "input"` in `intrinsic_proto.icon.DigitalIO` when configuring simulated robot controller DIO blocks.

### Safety status aggregation dominance hierarchy

Cell-level and part-level safety states are exposed in `intrinsic_proto.icon.v1.GetStatusResponse.safety_status` and `intrinsic_proto.icon.PartStatus.safety_status` (`intrinsic_proto.icon.SafetyStatus`).

- **Always check `requested_behavior` instead of relying solely on `estop_button_status`**:
  - Do not rely solely on `estop_button_status == BUTTON_STATUS_ENGAGED` to determine whether the robot cell is in a safety stop.
  - External safety interlocks (light curtains, safety doors, fieldbus `SS1_T` low signals) trigger `SAFE_STOP_1_TIME_MONITORED` or `SAFE_STOP_2_TIME_MONITORED` even while `estop_button_status` reports `BUTTON_STATUS_DISENGAGED`.
  - Always check `safety_status.requested_behavior != REQUESTED_BEHAVIOR_NORMAL_OPERATION` to determine if motion is inhibited by safety hardware.
- **Multi-interface dominance rules (`SafetyMessageHandler`)**:
  - When ICON aggregates multiple `safety_hardware_interfaces`:
    1. **`RequestedBehavior` severity dominance**:
       `SAFE_STOP_0 > SAFE_STOP_1_TIME_MONITORED > SAFE_STOP_2_TIME_MONITORED > PAUSE > NORMAL_OPERATION > UNKNOWN`
    2. **`ModeOfSafeOperation` restrictiveness dominance**:
       `UPDATING > CONFIGURATION > TEACHING_2 > TEACHING_1 > AUTOMATIC > UNKNOWN`
    3. **`estop_button_status` logical-OR dominance**:
       `ENGAGED > DISENGAGED` (any engaged E-stop triggers cell E-stop).
    4. **`enable_button_status` logical-AND dominance**:
       `DISENGAGED > ENGAGED` (releasing any three-position enabling switch in a multi-pendant workspace immediately halts motion).
    5. **Digital input override patterns (`behavior_override_request_pause_signal`)**:
       Can inject `REQUESTED_BEHAVIOR_PAUSE`, which overrides `NORMAL_OPERATION` but is dominated by any hardware safe stop (`SAFE_STOP_2_TIME_MONITORED`, `SAFE_STOP_1_TIME_MONITORED`, `SAFE_STOP_0`).
- **Physical safety stop vs. software fault clearing (`ModeOfSafeOperation: UNKNOWN`)**:
  - If `inctl icon clear-faults --instance_name=icon --address=localhost:17080` returns `Robot is safety stopped. ModeOfSafeOperation: UNKNOWN`, software commands cannot override a physical safety stop. Resolve physical E-stop buttons and teach pendant locks before attempting software clearing.

### Two-tier hardware fault propagation and service restart boundaries

When diagnosing partial cell shutdowns or unexpected session cancellations, understand how ICON classifies hardware modules:
- **Operational hardware**: Motion-producing hardware (robot arms, force-torque sensors).
- **Cell control hardware**: Cell infrastructure (EtherCAT I/O buses, safety controllers, grippers).
- **Fault propagation rules**:
  - If an **operational** hardware module faults while motion is enabled, ICON disables all operational hardware modules but **keeps cell control hardware modules enabled**. You can also manually trigger this state via:
    ```bash
    inctl icon disable --operational-only --instance_name=icon --address=localhost:17080
    ```
  - If a **cell control** hardware module faults (or any module suffers a fatal initialization fault), ICON disables **all** hardware modules (both operational and cell control).
  - Active sessions are selectively cancelled based on part dependencies: sessions controlling only cell control parts remain alive when an operational hardware module faults, and read-only monitoring sessions (`parts = []`) remain alive across non-fatal operational and cell control faults.
- **Hardware module pods vs. upstream asset device services**:
  - Calling `inctl icon clear-faults` only resets ICON hardware modules; it does not reset independent upstream asset `DeviceService` pods (e.g., an assetized EtherCAT force-torque sensor pod).
  - If an independent `DeviceService` fails during cluster startup, inspect its service state via:
    ```bash
    inctl service state list --address=localhost:17080 --output=json
    ```
    and restart the specific device service via:
    ```bash
    inctl service state restart <service_name> --address=localhost:17080
    ```

### Force-torque taring and contact deadband constraints

Before executing compliant insertion, surface following (`CartesianAdmittanceAction`), or force-controlled trajectories (`MakeContact`):

- **Target contact force must exceed 2.0 * the sensed wrench deadband norm**:
  - Target contact forces must strictly exceed:
    `F_target > 2.0 * ||force_control_settings.sensed_wrench_deadband||`
    as configured in `HalForceTorqueSensorPartConfig`. Otherwise, the contact force is masked by the sensor deadband and contact detection will never trigger.
- **Built-in settling detection via `"intrinsic.is_settled"`**:
  - Note that standalone `"intrinsic.wait_for_settling_action"` is removed; settling detection is built directly into `intrinsic.point_to_point_move`, `intrinsic.trajectory_tracking`, and `intrinsic.stop` via the `"intrinsic.is_settled"` state variable.
  - Always wait for `IsTrue("intrinsic.is_settled")` on the motion action so the physical manipulator has decelerated and settled before taring the force-torque sensor.
- **Selecting `num_taring_cycles` in `TareForceTorqueSensorParams`**:
  - Explicitly setting `num_taring_cycles: 0` fails action creation with `INVALID_ARGUMENT: num_taring_cycles must be > 0`.
  - While omitting the field defaults to `1` cycle, a single real-time cycle is susceptible to high-frequency sensor noise; production admittance workflows set `num_taring_cycles: 100` to reduce sample mean standard deviation (standard error = sigma / sqrt(N)).
  - Execute `"intrinsic.tare_force_torque_sensor"` only while the robot arm is stationary and free of external contact forces other than gravity acting on the attached tool.
- **False-positive UR force-torque taring log after protective stops**:
  - On Universal Robots (`ur_module`), recovering from a protective stop may emit `Failed to tare within the specified number of cycles`. This message is a known false-positive artifact of the driver's single-cycle status countdown; the F/T sensor tare actually succeeds.

## Cartesian space, `IK` options, low-level controllers, and streaming outputs

### Cartesian space representation and limits

- **Three distinct sensed wrench frames in `PartStatus`**:
  - `wrench_at_ft_uncompensated`: Raw force (`x, y, z` in N) and torque (`rx, ry, rz` in Nm) measured in the force-torque sensor frame without gravity or payload compensation.
  - `wrench_at_ft`: Low-pass filtered, tared, and payload/gravity-compensated wrench expressed in the force-torque sensor frame.
  - `wrench_at_tip`: Filtered, tared, and compensated wrench (`wrench_at_ft`) transformed into the robot tip frame.
  - Always inspect `wrench_at_tip` (or `wrench_at_ft`) rather than `wrench_at_ft_uncompensated` for contact-detection, insertion, and force-control logic.
- **Differing quaternion norm checks between `Transform` and `CartState`**:
  - `intrinsic_proto.icon.PartStatus.base_t_tip_sensed` populates `intrinsic_proto.icon.Transform`, whereas `PartStatus.cartesian_position_state.sensed_pose` uses `intrinsic_proto.Pose`.
  - `intrinsic_proto.icon.Transform`: Deserialization enforces strict unit quaternion normalization (`|q_w^2 + q_x^2 + q_y^2 + q_z^2 - 1.0| < 1e-12`) without auto-normalizing. Unnormalized quaternions fail with `INVALID_ARGUMENT: Cannot deserialize control::Transform: quaternion is not normalized`.
  - `intrinsic_proto.icon.CartState`: The `pose` repeated field must contain **exactly 7 elements** ordered as `[x, y, z, qx, qy, qz, qw]` (with scalar `q_w` at index 6). Deserialization enforces a looser norm check (`0.9 <= ||q||^2 <= 1.1`) and automatically normalizes the quaternion. Both `velocity` and `acceleration` repeated fields, when present, must contain **exactly 6 elements** (`[x, y, z, RX, RY, RZ]`).
  - Always compare orientations using `intrinsic.math.python.data_types.Pose3` or `Rotation3` (which treat antipodal quaternions `+q` and `-q` as equal) rather than raw component-wise quaternion equality (`Quaternion.__eq__`).
- **Asymmetric translational vs. rotational fields and strict sign invariants in `CartesianLimits`**:
  - All translational limit fields (`min_translational_position`, `max_translational_position`, `min_translational_velocity`, `max_translational_velocity`, `min_translational_acceleration`, `max_translational_acceleration`, `min_translational_jerk`, `max_translational_jerk`) are `repeated double` fields that **must contain exactly 3 elements** (`[X, Y, Z]`).
  - Rotational limits (`max_rotational_velocity`, `max_rotational_acceleration`, `max_rotational_jerk`) are scalar `double` fields representing symmetric upper bounds (rad/s, rad/s², rad/s³).
  - **Strict derivative sign invariants**: In addition to requiring `min <= max`, validation enforces that every element of `min_translational_velocity`, `min_translational_acceleration`, and `min_translational_jerk` **must be <= 0.0**, every element of `max_translational_velocity`, `max_translational_acceleration`, and `max_translational_jerk` **must be >= 0.0**, and all rotational limits **must be >= 0.0**. Passing positive numbers for minimum velocity/acceleration/jerk bounds fails with `INVALID_ARGUMENT: Cartesian limits are invalid`.
  - When supplying `CartesianLimits` inside `CartesianJoggingFixedParams`, translational position bounds (`min_translational_position`, `max_translational_position`) are ignored by the controller; only velocity, acceleration, and jerk limits are enforced relative to `frames.jog_in_frame` (`JOG_IN_FRAME_BASE` or `JOG_IN_FRAME_TIP`).
- **Stability bounds on `CartVec6` gains in `AgentBridgeCommand`**:
  - When streaming `MotionUpdate` to `AgentBridgeCommand`, all components of the 6-DOF force feedback gain vector `wrench_feedback_gains_at_tip` (`intrinsic_proto.icon.CartVec6`) must lie within `[0.0, 0.95]` to prevent closed-loop force control instability, and all components of `pose_error_integrator_gain_at_tip` must be `>= 0.0`.

### Inverse kinematics options

- **`intrinsic_proto.icon.IKOptions` vs. real-time nullspace targets and `intrinsic_proto.motion_planning.v1.MotionPlannerService/ComputeIk`**:
  - `intrinsic_proto.icon.IKOptions` defines a quadratic redundancy-resolution cost term (`preferred_joint_positions` and scalar weight `preferred_joint_positions_weight`) penalizing squared Euclidean distance from a preferred joint vector (`J(q) = w * ||q - q_pref||_2^2`).
  - `intrinsic_proto.icon.IKOptions` is defined in the SDK schema but is omitted from Intrinsic Control (ICON) and is not referenced by active `intrinsic_proto.icon.v1.IconApi` RPCs or built-in ICON action signatures.
  - For **real-time nullspace redundancy resolution** during Cartesian control, specify `intrinsic_proto.icon.actions.proto.NullspaceTarget` (`nullspace_stiffness`, `nullspace_damping`, `nullspace_reference`) inside `CartesianImpedanceParameters`, or populate `nullspace_target_configuration` (`intrinsic_proto.icon.JointVec`) inside execution settings (such as `AgentBridgeCommand.execution_settings`).
  - For **offline/skill inverse kinematics queries** (controlling solution counts, branch consistency, or collision settings), always call `intrinsic_proto.motion_planning.v1.MotionPlannerService/ComputeIk` (`intrinsic_proto.motion_planning.v1.ComputeIkRequest` or Python `MotionPlannerClient.compute_ik` with `max_num_solutions`, `collision_settings`, `prefer_same_branch`, `ensure_same_branch`).
- **Interpreting `ComputeIkResult` candidates vs. valid solutions**:
  - Check `result.solutions` to determine whether IK succeeded. The field `result.ik_debug_information.ik_solutions` contains all candidates generated during solver exploration, including candidates rejected due to joint limits, collisions, or constraints. Use `ik_debug_information` strictly for diagnosing rejected constraints.
- **Diagnosing 2*pi wrist flips on linear segments**:
  - When a multi-segment motion fails on a linear segment with `maximum_absolute_joint_configuration_error` near `6.283 rad` (`2*pi`), the preceding approach segment selected an IK branch near a joint limit (e.g. wrist `q_5` near `-3*pi/2`). A subsequent linear move crosses the limit and forces a jump to `+2*pi`. Set `ensure_same_branch=True` or `prefer_same_branch=True` in `IKOptions`, or constrain joint position bounds on the approach target.
- **Latency scaling in high-density workcells**:
  - Because `compute_ik` constructs a full `ObjectWorldView` on each call, execution time scales linearly with total scene objects (growing from `~20 ms` with 4 objects to `>200 ms` with 180+ objects). In high-density workcells, pass explicit `collision_settings` in `IKOptions` to exclude static or unreachable background objects.
- **Real-time IK solver safety in `ServicesConfig`**:
  - In `intrinsic_proto.icon.ServicesConfig`, `allow_non_real_time_inverse_kinematics` defaults to `false`, causing `KinematicsFromWorldService` to ignore any IK solver where `IsRealtimeSafe()` returns false. Set `allow_non_real_time_inverse_kinematics: true` when testing generic kinematic chains that rely on iterative numerical IK solvers.
  - Enabling `kinematics_from_world_service: true` or `assembly_from_world_service: true` without configuring `world_service_from_grpc` fails ICON startup (`create_icon_main_loop`) with `FAILED_PRECONDITION`. Configuring `dynamics_from_kinematics_service_config` without `kinematics_from_world_service: true` also fails with `FAILED_PRECONDITION`.

### Real-time `PID` controllers, linear acceleration filters, and 6x6 cartesian matrices

- **Hardware interface preconditions in `HalArmPartConfig`**:
  - Enabling `arm_position_pid_torque_controller_config` (`ArmPositionPidTorqueControllerConfig` / `JointPositionPidTorqueControllerConfig`) requires `joint_torque_command`, `joint_position_state`, and `joint_velocity_state` hardware interfaces to be present, and requires `joint_position_command` to be **absent**. Furthermore, feedback setpoint calculation returns `false` (producing zero torque) if `input_command.velocity_feedforward()` is not populated on each cycle. In `JointPositionPidTorqueControllerConfig`, `integration_window_seconds` must be `>= timestep` (`timestep > 0`), and `alpha` must be in `[0.0, 1.0]`.
  - Enabling `linear_joint_acceleration_filter_config` (`LinearJointAccelerationFilterConfig`) requires `joint_position_state` and `joint_velocity_state` to be present, and requires `joint_acceleration_state` to be **absent**.
- **Strict parameter validation and hidden zero-clamping in `JointPositionPidVelocityControllerConfig`**:
  - All repeated gain vectors (`k_p`, `k_i`, `k_d`, `k_ff`, `max_velocity_command`, and non-empty `max_integral_control`) must have identical lengths equal to the number of joints.
  - For any degree of freedom `i` where `k_i[i] > 0`, `k_p[i]` must be strictly positive (`k_p[i] > 0`); otherwise controller initialization fails with `INVALID_ARGUMENT: All values in k_p should be > 0 for degrees of freedom where k_i > 0`.
  - **Mandatory `max_integral_control` when `k_i > 0`**: If `max_integral_control` is left empty (`size == 0`), the controller defaults `max_integral_control` to a zero vector for all joints and clamps the integral state to `[-0.0, 0.0] = 0.0` every cycle. Setting `k_i > 0` without explicitly populating positive `max_integral_control` bounds results in zero integral action.
  - **Anti-windup conditional integration**: Integral accumulation automatically pauses on any joint `i` during cycles where the previous velocity command reached saturation (`|previous_velocity_command[i]| >= max_velocity_command[i]`).
  - The Butterworth filter cutoff frequency fields (`position_filter_cuttoff_frequency_hz` and `velocity_filter_cuttoff_frequency_hz`: note the double `t` spelling `cuttoff` in the schema) must satisfy the Nyquist criterion (`0 < f_c < 0.5 / cycle_time_seconds`).
- **DARE solver defaults in `LinearJointAccelerationFilterConfig`**:
  - `DiscreteAlgebraicRicattiEquationSolutionConfig` (note the single `c`, double `t` spelling `Ricatti` in the schema) defaults to `DARE_ITERATION_STRATEGY_ROBUST` with `max_iterations = 250`, `accuracy = 1e-4`, and `regularizer_epsilon = 1e-5`.
- **6x6 Cartesian matrices (`intrinsic_proto.icon.Matrix6d`)**:
  - `Matrix6d.data` is a packed `repeated double` field that must contain **exactly 36 elements** stored in **row-major** order (`data[i * 6 + j] = M(i, j)`). Any other length is rejected with `INVALID_ARGUMENT: Cannot read Matrix6d from proto: expected data size of 36`.
  - In Python `intrinsic.icon.proto.matrix_conversions.from_ndarray`, the dimension guard is written using a chained inequality (`if matrix.shape[0] != matrix.shape[1] != 6:`), which evaluates to `False` for any square `N x N` array where `N != 6` (such as a `3 x 3` rotation matrix) and emits a malformed proto. Always explicitly assert `matrix.shape == (6, 6)` before calling `from_ndarray`.

### Real-time action streaming

- **First-poll blocking behavior vs. subsequent cached returns (`GetLatestStreamingOutput`)**:
  - Calling `intrinsic_proto.icon.v1.IconApi/GetLatestStreamingOutput` (`GetLatestStreamingOutputRequest{session_id, action_id}`) blocks until the target action writes its **first** streaming output value into the real-time output buffer, or until the gRPC request deadline expires (`DEADLINE_EXCEEDED`).
  - Once at least one output has been written, subsequent `GetLatestStreamingOutput` calls return the most recent cached `StreamingOutput` immediately without blocking for a new control cycle. Always attach a finite gRPC deadline when calling `GetLatestStreamingOutput`.
  - Returns `FAILED_PRECONDITION` if `session_id` does not exist, or `NOT_FOUND` if `action_id` does not exist or its `ActionSignature` does not declare a `streaming_output_info`.
- **Clock domain separation (`timestamp_ns` vs. `wall_clock_timestamp_ns`)**:
  - `timestamp_ns`: Monotonic control time in nanoseconds since the ICON server started (ticks once per real-time control cycle and may advance faster or slower than real time in simulation).
  - `wall_clock_timestamp_ns`: Local machine wall clock time in nanoseconds when the output was generated.
  - Never subtract or compare `timestamp_ns` against `wall_clock_timestamp_ns`. Use `timestamp_ns` to correlate streaming outputs with `PartStatus.timestamp_ns` and `RobotStatus.timestamp_ns`, and use `wall_clock_timestamp_ns` only to align ICON events with external cluster logs.
- **Live gRPC `StreamingOutput` vs. logged `StreamingOutputWithMetadata` two-stage unpacking**:
  - Live calls to `GetLatestStreamingOutput` return a bare `intrinsic_proto.icon.StreamingOutput`.
  - Platform logs recorded on topic `/icon/{robot_name}/output_streams/action_{action_id}` wrap every streaming output in `intrinsic_proto.icon.StreamingOutputWithMetadata` (`output`, `action_type_name`, `action_instance_id`). Unpacking a logged output requires a two-stage `google.protobuf.Any` unpack: first unpack `LogItem.payload.any` into `StreamingOutputWithMetadata`, then unpack `streaming_output_with_meta.output.payload` into the concrete action output message.

## Input-aware diagnostic decision trees

### Motion planning and inverse kinematics decision tree

- **`ComputeIk` returns empty `solutions`**: Inspect `result.ik_debug_information.ik_solutions` for collision violations (check `Alt + G` and `collision_settings`), joint limits, or quaternion errors (`|q|^2 == 1.0`). Re-run with `ensure_same_branch=True`, or constrain approach pose.
- **Multi-segment linear move fails with wrist flip (~6.28 rad error)**: Check preceding approach segment (Segment 0) joint angles near `+/-3*pi/2`. Set `prefer_same_branch=True` in `IKOptions` or apply 180° tool symmetry offset.
- **Immediate retreat motion fails at Segment 0 with collision**: Sensor settling or noise shifted live sensed configuration into collision. Add `collision_rules` with `is_excluded: true` on departure segment; set margin `>= 2 mm`.
- **Dynamic coordinate frame moves between calls (stale trajectory returned)**: Trajectory matches previous frame pose (fuzzy cache hit). Set `skip_fuzzy_cache_check=True` in `MotionPlanningOptions`.
- **Motion planning fails with "Unable to determine collection type for entity"**: Inspect URDF/SDF for componentless adapter links or mismatched `eid_<N>` vs `ofid_<N>`. Define valid coordinate frame or geometry components on adapter links.
- **`move_robot` times out or planner memory > 2.7 GB**: Inspect world logs for "Large volume mesh detected" (`> 200 m^3`). Rescale CAD meshes from mm to m (0.001 scale factor); replace with convex hulls.

### `ICON` session and real-time control loop decision tree

- **`OpenSession` returns `AlreadyExistsError`**: Previous session context leaked or is trapped in cleanup. Wrap sessions in Python context managers; terminate stale session ID.
- **`inctl icon clear-faults` fails with "ModeOfSafeOperation: UNKNOWN"**: Physical E-stop button status or teach pendant enabling switch is active. Disengage physical E-stops and clear teach pendant errors before re-running `clear-faults`.
- **`OpenSession` hangs without error ("Delaying start of session because of a safety action")**: Inspect `rs-icon` logs for unfinished safety action on requested part. Wait for safety stop trajectory to complete; run `inctl icon clear-faults`.
- **`inctl icon clear-faults` succeeds, but external device remains offline**: Inspect `inctl service state list --output=json` for `STATE_CODE_*`. Run `inctl service state restart <device_service_name>`.
- **Overrun warning "Long duration between read_status_calls: >2ms"**: Check phase breakdown (`rs > 1 ms`: hardware module read stall; `proc > 1 ms`: controller computation overrun; `ac > 1 ms`: hardware module write stall; `exec > 90%`: host CPU preemption / late futex wakeup). Isolate RT core and prevent NIC IRQ flood.

## Multi-`RPC` workflows and sequences

### Sequence 1: pulsed pneumatic and `PLC` actuation sequence (door and vise pattern)

Actuating double-acting solenoids or PLC digital inputs (such as CNC doors and pneumatic vises) uses a 4-stage pulsed sequence rather than leaving digital outputs latched high:

1. **Pre-check input state (`ai.intrinsic.dio_read_input`)**:
   - Calls `intrinsic_proto.icon.v1.IconApi/GetConfig` followed by `intrinsic_proto.icon.v1.IconApi/GetStatus` to inspect `PartStatus.adio_state.digital_inputs` on the sensor input block (such as `"ethercat_cell_control_channel_2_input"`) and short-circuits if the mechanism is already in the target state.
2. **Assert solenoid output (`ai.intrinsic.dio_set_output`)**:
   - Opens an exclusive session (`intrinsic_proto.icon.v1.IconApi/OpenSession`) claiming the ADIO part, adds an `"intrinsic.adio"` action with `ADIOFixedParams.outputs.digital_outputs` setting `values_by_index[bit] = true`, starts the action, and watches via `intrinsic_proto.icon.v1.IconApi/WatchReactions` until `IsTrue("intrinsic.outputs_set")` fires (with a `5.0s` timeout).
3. **Wait for limit-switch feedback without locking the ADIO part (`ai.intrinsic.dio_wait_for_input`)**:
   - Opens a zero-part monitoring session (`parts = []`) and registers a freestanding reaction on `@{part_name}.ADIOPart.di.{block_name}[{index}]`. Polls `WatchReactions` in 1-second intervals (`step_timeout = min(remaining_time, 1s)`) until the limit switch triggers.
4. **De-assert solenoid output (`ai.intrinsic.dio_set_output`)**:
   - Immediately resets `values_by_index[bit] = false` on the output block so the solenoid coil is de-energized and opposing valves can operate later.

### Sequence 2: move, settle, and tare sequence for force-controlled operations

Before starting a compliant insertion or surface-following action (`CartesianAdmittanceAction` or `MakeContact`):

1. **Pre-validate force deadband constraints**:
   - Verify that the target contact force strictly exceeds `2.0 * ||force_control_settings.sensed_wrench_deadband||`.
2. **Approach trajectory and built-in settling**:
   - Execute a point-to-point move (`intrinsic.point_to_point_move` or `intrinsic.trajectory_tracking`) to the pre-contact pose.
3. **Wait for physical arm settling**:
   - Guard transition on reaction condition `IsTrue("intrinsic.is_settled")` exposed directly by `intrinsic.point_to_point_move`, `intrinsic.trajectory_tracking`, or `intrinsic.stop` (standalone `"intrinsic.wait_for_settling_action"` is removed).
4. **Tare force-torque sensor bias**:
   - Transition to `"intrinsic.tare_force_torque_sensor"` on the `"ft_sensor"` slot with `TareForceTorqueSensorParams{num_taring_cycles: 100}`, waiting for reaction condition `IsTrue("intrinsic.is_done")`.
5. **Execute compliance action**:
   - Start the admittance/force action using the freshly tared sensor offset, guarding the transition with `Condition.all_of([is_greater_than(ELAPSED_TIME_SECONDS, MIN_APPROACH_TIME_S), is_greater_than(SENSED_FORCE, stop_switching_force)])` to ignore transient startup force spikes.

### Sequence 3: streaming real-time inputs and polling streaming outputs

1. **Introspect action streaming descriptors**:
   - Call `intrinsic_proto.icon.v1.IconApi/ListActionSignatures` (or `GetActionSignatureByName`) and inspect `ActionSignature.streaming_input_infos` and `ActionSignature.streaming_output_info`.
2. **Open session and start action**:
   - Open `intrinsic_proto.icon.v1.IconApi/OpenSession` with `allocate_parts`, `add_actions_and_reactions`, and `start_actions_request`, reading `initial_session_data.session_id` from the response.
3. **Stream real-time inputs (`OpenWriteStream`)**:
   - Open `intrinsic_proto.icon.v1.IconApi/OpenWriteStream`. Send the first message with `session_id` and `add_write_stream` (`action_id`, `field_name`) without `write_value`. Send subsequent messages with `write_value.value` (`google.protobuf.Any`) without `add_write_stream`.
4. **Poll real-time outputs (`GetLatestStreamingOutput`)**:
   - Call `intrinsic_proto.icon.v1.IconApi/GetLatestStreamingOutput` with `session_id`, `action_id`, and an explicit gRPC timeout, unpacking `response.output.payload` (`google.protobuf.Any`).
