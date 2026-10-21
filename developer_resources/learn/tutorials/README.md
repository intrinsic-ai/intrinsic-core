# Intrinsic Core Tutorials

Welcome to the **Intrinsic Core (IC)** tutorials. These hands-on guides walk you through configuring, operating, customizing, and extending your automation [solutions](../glossary/intrinsic_terms.md#solution) on Intrinsic Core.

The tutorials follow the **Open Machine Tending Solution ([OMTS](../glossary/intrinsic_terms.md#open-machine-tending-solution-omts))** baseline, progressively building from initial environment deployment and simulation visualization to custom asset development, camera perception, physical [robot](../glossary/general_terms.md#robot) bringup, and ROS integration.

---

> [!NOTE]
> Most tutorials run entirely in [simulation](../glossary/general_terms.md#simulation) and do not require physical hardware. Tutorials requiring physical hardware, cameras, or external controllers specify their prerequisites upfront.

---

## Core Tutorial Curriculum

The tutorials are designed to be followed in sequential order:

### 1. [Getting Started](getting_started.md)
* **Status**: Available
* **Summary**: Set up your development environment and workstation on Ubuntu 26.04. Configure container runtime prerequisites (`k3s`, `git-lfs`), install the Intrinsic Control CLI ([`inctl`](../glossary/intrinsic_terms.md#inctl-intrinsic-control-cli)), deploy Intrinsic Core, and load the baseline Open Machine Tending Solution (OMTS).

### 2. [Visualize the Robot](visualize_the_robot.md)
* **Status**: Available
* **Summary**: Inspect the execution state of your robot and workcell objects using **RViz** (to visualize the "belief world" state maintained by the Object World [Service](../glossary/intrinsic_terms.md#service) via the ROS Bridge) and **Gazebo** (to observe rigid-body dynamics, gravity, and contact interactions in the physics simulation).

### 3. [Jog the Robot](jog_the_robot.md)
* **Status**: Available
* **Summary**: Manually jog real or simulated robot [joints](../glossary/general_terms.md#joint) using direct keyboard teleoperation via the `jog_interactive` CLI. Verify runtime connectivity to the [Intrinsic Real-Time Control Framework (ICON)](../glossary/intrinsic_terms.md#intrinsic-real-time-control-framework-icon), manage hardware fault states, and inspect joint limits.

### 4. [Visualize the Solution](visualize_the_solution.md)
* **Status**: Available
* **Summary**: Observe the Open Machine Tending Solution as it loads and unloads a simulated CNC machine.

### 5. [Cell Customization](cell_customization.md)
* **Status**: Available
* **Summary**: Learn how `ObjectWorldUpdates` configure workcell geometry. Apply dynamic in-memory coordinate [frames](../glossary/general_terms.md#frame) using `apply_scene_updates`, relocate the robot base pose within the cell, and persist updates across deployments by adding configuration targets to the Bazel `BUILD` file.

### 6. [Swap Assets](swap_assets.md)
* **Status**: Available
* **Summary**: Swap software and hardware [Assets](../glossary/intrinsic_terms.md#asset) in a Solution. Replace software [Skills](../glossary/intrinsic_terms.md#skill) on a running cluster using `inctl`, introspect Kubernetes microservice logs using `k9s`, and swap the standard Universal Robots manipulator with a KUKA KR10-R1100-2 robot by updating Bazel dependencies and hardware driver configurations.

### 7. [Import New Part](import_new_part.md)
* **Status**: Available
* **Summary**: Import custom workpiece geometries (such as raw stock for CNC machining) into Intrinsic Core. Define physical properties (mass, inertia, collision geometries) using SDFormat (`.sdf`), configure asset manifests, package bundles with Bazel, and deploy the new [Scene Object](../glossary/intrinsic_terms.md#scene-object) into the running workcell.

### 8. [Custom Asset Creation (Software)](custom_asset_creation_software.md)
* **Status**: Available
* **Summary**: Develop, package, deploy, and execute custom Python skills. Build a minimal cancellation-aware skill (`say_skill`), construct a spatial [digital twin](../glossary/intrinsic_terms.md#digital-twin) validation skill (`validate_pose`) querying the [ObjectWorld](../glossary/intrinsic_terms.md#world) transform graph, and compose skills into automated sequences using the [Solution Building Library (SBL)](../glossary/intrinsic_terms.md#solution-building-library-sbl) and [Behavior Tree](../glossary/intrinsic_terms.md#behavior-tree) [Executive](../glossary/intrinsic_terms.md#executive).

### 9. [Custom Asset Creation (Hardware)](custom_asset_creation_hardware.md)
* **Status**: Available
* **Summary**: Build an actuated hardware asset from mechanical CAD models and manufacturer datasheets. Follow a step-by-step walkthrough to import and configure a Weiss Robotics GRIPKIT-EASY (WSG 32 parallel gripper) using digital I/O lines and custom driver services.

### 10. Camera Setup
* **Status**: Coming Soon
* **Summary**: Connect and configure a Power-over-Ethernet 3D depth sensor (such as the Orbbec Gemini 335LE PoE). Configure network routing, verify camera streaming pipelines, and prepare sensor inputs for pose estimation and visual inspection.

### 11. ROS Connectivity
* **Status**: Coming Soon
* **Summary**: Leverage "The Aquarium" encapsulation tooling to package existing ROS 2 packages, nodes, and topics into containerized Skills that seamlessly install and run on Intrinsic Core solutions.

---

## Specialized & Ecosystem Extensions

These standalone modules cover advanced integrations with robotics community frameworks:

### 12. Adding Robot via ROS 2 Control
* **Status**: Coming Soon
* **Summary**: Integrate third-party and custom robot hardware mechanisms using the industry-standard `ros2_control` hardware abstraction layer and controller managers inside Intrinsic Core.

### 13. Camera & Workcell Calibration
* **Status**: Coming Soon
* **Summary**: Perform high-precision intrinsic and hand-eye extrinsic camera calibrations using ChArUco calibration targets. Calibrate both eye-in-hand (robot-mounted) and eye-to-hand (statically mounted) sensor configurations.

### 14. [MoveIt Grasp Planning](moveit_grasp_planning.md)
* **Status**: Available
* **Summary**: Integrate the MoveIt motion and grasp planning ecosystem with Intrinsic Core. Stream workcell collision geometry into the MoveIt planning scene, generate collision-free grasp proposals for objects in the scene, and use them as the grasp planner of the OMTS machine tending cycle.
