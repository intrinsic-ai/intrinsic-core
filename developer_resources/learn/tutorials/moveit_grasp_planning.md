# MoveIt Grasp Planning

The purpose of this tutorial is to showcase an integration of Intrinsic Core with *MoveIt* through [OMTS](../glossary/intrinsic_terms.md#open-machine-tending-solution-omts). Deploying the OMTS [solution](../glossary/intrinsic_terms.md#solution) automatically starts the *moveit_planning_service* and installs *moveit_plan_grasp_skill* inside the cluster. We will connect a local RViz window to inspect the MoveIt planning [scene](../glossary/intrinsic_terms.md#scene) and candidate grasps, and then plan grasps for objects in the scene using *MoveIt*—first standalone, and then as the grasp planner of the full OMTS machine tending cycle.

> [!NOTE]
> Note that this integration is only showing cuboid grasp planning using MoveIt, verified by IK and collision checks, but the motion planning and execution will still be relying on Intrinsic Core capabilities.

![MoveIt Grasp Planning Overview](../../img/learn/tutorials/moveit_grasp_planning_overview.gif)

## 1. Prerequisites and Environment Setup

First, start the OMTS [simulation](../glossary/general_terms.md#simulation) (both the default `omts` UR5e cell shown in the screenshots below and `--config=lab_bb_01` for the UR3e cell are supported):

```bash
cd ~/intrinsic-omts
# Check out main if you cloned an earlier release tag that predates intrinsic-moveit v0.1.0
git fetch origin && git checkout origin/main
bazel run //:omts_solution -c opt -- \
  --address localhost:17080 --operation_mode=sim
# Or, for the Lab BB-01 (UR3e) setup:
# bazel run //:omts_solution -c opt --config=lab_bb_01 -- \
#   --address localhost:17080 --operation_mode=sim
```

The deployed solution automatically installs and starts everything this integration needs on the Intrinsic Core side, so there is nothing to sideload or reconfigure by hand:

- The *moveit_planning_service* [service](../glossary/intrinsic_terms.md#service) (`rs-moveit-planning-service-0` in `app-resources`) runs in the cluster and automatically loads the `omts` or `lab_bb_01` MoveIt configuration matching the deployed cell.
- The *moveit_plan_grasp_skill* [skill](../glossary/intrinsic_terms.md#skill), which OMTS calls to obtain pre-grasps and grasps from *moveit_planning_service*, is installed with every deployment.
- The *flowstate_ros_bridge* [service](../glossary/intrinsic_terms.md#service) runs with a default configuration that streams the `/tf` and `/joint_states` topics MoveIt needs.

Verify that `rs-moveit-planning-service-0` and `rs-flowstate-ros-bridge-0` are `Running`:

```bash
kubectl get pods -n app-resources
```

If you have already set up Intrinsic Core's standard RViz view (from [Visualize the robot](visualize_the_robot.md)), the initial belief world looks like this:

![MoveIt Initial RViz](../../img/learn/tutorials/moveit_rviz_initial.png)

### Visualizing the MoveIt planning scene in RViz

Because *moveit_planning_service* is already running inside the cluster, you can run Sections 3 and 4 directly without ROS 2 installed locally. To inspect the MoveIt planning scene and MoveIt Task Constructor (MTC) grasp candidates visually in RViz, install [ROS lyrical](https://docs.ros.org/en/lyrical/Get-Started/Installation/Ubuntu-Install-Debs.html) and build only the lightweight hardware description and MoveIt config packages from [*intrinsic-moveit*](https://github.com/intrinsic-ai/intrinsic-moveit):

```bash
# Set up a lightweight colcon workspace for the robot meshes and MoveIt RViz config
mkdir -p ~/ws_intrinsic_moveit/src && cd ~/ws_intrinsic_moveit/src
gh repo clone intrinsic-ai/intrinsic-moveit -- --revision=v0.1.0
source /opt/ros/lyrical/setup.bash
cd ~/ws_intrinsic_moveit
sudo apt update && sudo apt install -y \
  ros-lyrical-rmw-zenoh-cpp \
  ros-lyrical-moveit-ros-visualization \
  ros-lyrical-moveit-task-constructor-visualization
rosdep install --from-paths \
  src/intrinsic-moveit/robot_hardware_descriptions \
  src/intrinsic-moveit/third_party \
  --ignore-src -r -y
colcon build --symlink-install \
  --packages-up-to omts_moveit_config lab_bb_01_moveit_config
```

Now connect standalone RViz to the cluster's Zenoh Router exposed on port `7447`:

```bash
# These commands are required for all terminals running ROS against Intrinsic Core
source ~/ws_intrinsic_moveit/install/setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
export ZENOH_CONFIG_OVERRIDE='mode="client";connect/endpoints=["tcp/127.0.0.1:7447"]'

# Launch RViz for the default OMTS cell (UR5e):
ros2 launch omts_moveit_config moveit_rviz.launch.py \
  rviz_config:=$HOME/ws_intrinsic_moveit/src/intrinsic-moveit/moveit_planning_service/rviz/motion_planning_service.rviz

# Or, if //:omts_solution was deployed with --config=lab_bb_01 (UR3e):
# ros2 launch lab_bb_01_moveit_config moveit_rviz.launch.py \
#   rviz_config:=$HOME/ws_intrinsic_moveit/src/intrinsic-moveit/moveit_planning_service/rviz/motion_planning_service.rviz
```

<details>
  <summary>Optional: Build and run <code>moveit_planning_service</code> from source (for development)</summary>

  If you are modifying *intrinsic-moveit* itself and want to run *moveit_planning_service* locally from source instead of using the in-cluster pod:

  1. Import `lyrical.repos` and build `moveit_planning_service` (this compiles the C++ SDK dependencies and takes several minutes):

     ```bash
     cd ~/ws_intrinsic_moveit/src
     source /opt/ros/lyrical/setup.bash
     vcs import . < intrinsic-moveit/lyrical.repos --recursive
     cd ~/ws_intrinsic_moveit
     rosdep install --from-paths . --ignore-src -r -y
     colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
       --symlink-install --packages-up-to moveit_planning_service
     ```

  2. Stop the in-cluster `moveit_planning_service` instance first so two planning nodes do not compete on `/grasp_planning/plan_grasps` (or comment out `":moveit_planning_service",` in `omts_solution.instances` in `~/intrinsic-omts/BUILD` before deploying):

     ```bash
     inctl service delete --address=localhost:17080 moveit_planning_service
     ```

  3. Launch the local service connected to the cluster Zenoh Router (`7447`), passing `robot_config:=lab_bb_01` if running against `--config=lab_bb_01`:

     ```bash
     source ~/ws_intrinsic_moveit/install/setup.bash
     export RMW_IMPLEMENTATION=rmw_zenoh_cpp
     export ZENOH_CONFIG_OVERRIDE='mode="client";connect/endpoints=["tcp/127.0.0.1:7447"]'
     ros2 launch moveit_planning_service service.launch.py \
       robot_config:=omts \
       headless:=false \
       start_service_status_monitor:=false
     ```
</details>

Once MoveIt RViz opens, verify that the window looks like below. The [robot](../glossary/general_terms.md#robot) is represented by its meshes, while all other objects in the scene are propagated as collision objects and represented as green meshes.

![MoveIt RViz Planning Service](../../img/learn/tutorials/moveit_rviz_planning_service.png)

## 2. OMTS state synced to MoveIt planning scene

The state of the robot and objects are synchronized with the MoveIt planning scene. This can be verified with

```bash
cd ~/intrinsic-omts
# Jogging the robot, see tutorial "Jog the robot"
bazel run //tools/jogging:jog_interactive -- \
  --host=localhost \
  --port=17080 \
  --instance=icon
# Updating the scene (default OMTS cell only; skip on --config=lab_bb_01, which has no CNC vise), see tutorial "Cell customization"
bazel run //tools/world:apply_scene_updates -- \
  --address localhost:17080 \
  --files configs/omts/raw_stock_in_vise.updates.pbtxt
# Resetting the scene, see tutorial "Cell customization"
inctl world reset --address localhost:17080
```

![MoveIt State Synchronization](../../img/learn/tutorials/moveit_state_sync.gif)

This gif is sped up 2x.

## 3. Grasp planning on *raw_stock_2x3x5*

![MoveIt Grasp Planning 1](../../img/learn/tutorials/moveit_grasp_planning_1.png)
![MoveIt Grasp Planning 2](../../img/learn/tutorials/moveit_grasp_planning_2.png)

We can run *moveit_plan_grasp_and_move* to plan for each object and optionally move the robot to the pre-grasp frame. The tool stops at the pre-grasp and never commands the gripper, so it is safe to run repeatedly while checking that a part is plannable and reachable.

```bash
cd ~/intrinsic-omts
# Dry run: plan a grasp on raw_stock_2x3x5 without moving the arm
bazel run //third_party/intrinsic_moveit/tools:moveit_plan_grasp_and_move -- \
  --address=localhost:17080 \
  --target_object=raw_stock_2x3x5 \
  --plan_only \
  --surfaces=0,1,4,5
# Plan and approach the pre-grasp on the table surface
bazel run //third_party/intrinsic_moveit/tools:moveit_plan_grasp_and_move -- \
  --address=localhost:17080 \
  --target_object=raw_stock_2x3x5 \
  --surfaces=0,1,4,5
# Optionally, reset the scene such that the trajectory to the CNC Vice is shorter
# inctl world reset --address localhost:17080
# Relocate Workpiece to the CNC Vice (default OMTS cell only; skip on --config=lab_bb_01)
bazel run //tools/world:apply_scene_updates -- \
  --address=localhost:17080 \
  --files configs/omts/raw_stock_in_vise.updates.pbtxt
# Plan again to grasp the workpiece that is in the vice now (default OMTS cell only)
# This planning step may take longer due to the length of the trajectory if the
# world has not been reset
bazel run //third_party/intrinsic_moveit/tools:moveit_plan_grasp_and_move -- \
  --address=localhost:17080 \
  --target_object=raw_stock_2x3x5 \
  --surfaces=0,1,4,5
```

![MoveIt Plan and Move](../../img/learn/tutorials/moveit_grasp_planning_overview.gif)

This gif is sped up 2x.

The various motions in the MoveIt planning scene represent the trajectories that MoveIt generated, that can be used to reach the planned pre-grasp frames, ranked by cost. By default, only the best set of grasp and pre-grasp is returned, which will be used to change the positions of the pre-grasp and grasp frames in OMTS.

`--surfaces=0,1,4,5` restricts the candidates to the four long faces of the workpiece, so that it is never gripped by its end caps. See [Why `--surfaces=0,1,4,5`](https://github.com/intrinsic-ai/intrinsic-omts/tree/main/third_party/intrinsic_moveit#why-surfaces-0145) for details.

## 4. Grasp planning in the OMTS cycle

By default, the OMTS machine tending cycle computes the grasp and pre-grasp from the center of the detected workpiece (the `cuboid_center` grasp planner). With the *moveit_planning_service* still running, we can hand that step over to MoveIt instead.

If you have not run the OMTS cycle before, register the pose estimator first, as described in [Visualize the solution](visualize_the_solution.md#start-the-process). Then reset the scene and run the cycle with the `moveit` grasp planner.

```bash
cd ~/intrinsic-omts
# Reset the scene to its initial state
inctl world reset --address localhost:17080
# Run the machine tending cycle, with MoveIt planning the grasp
# (use --config=configs/lab_bb_01/app_config.yaml if deployed with --config=lab_bb_01)
bazel run //src:omts_app -- \
  --address=localhost:17080 \
  --config=configs/omts/app_config.yaml \
  --grasp_planner=moveit
```

Perception still localizes the workpiece, but instead of calculating the grasp frames itself, the cycle settles briefly so that the MoveIt planning scene catches up with the detected pose, and then calls *moveit_plan_grasp_skill* to write the `grasp` and `pre_grasp` frames. The rest of the cycle is unchanged. You can tell the two planners apart by the pre-grasp: `cuboid_center` places it 8 cm straight above the grasp, while MoveIt places it 5 cm back along the planned approach.

To make MoveIt the default for a cell instead of passing the `--grasp_planner` flag every time, set the planner in the `grasp` section of the cell's `app_config.yaml` to `moveit`. The `--grasp_planner` flag still overrides it for a single run.

```yaml
grasp:
  planner: "moveit"
```

The same section holds the MoveIt tuning parameters, such as `moveit_surfaces`, `moveit_retract_dist_m`, `moveit_timeout_ms` and `moveit_settle_seconds`. See [Using MoveIt in the Production Cycle](https://github.com/intrinsic-ai/intrinsic-omts/tree/main/third_party/intrinsic_moveit#using-moveit-in-the-production-cycle) for the full list.

## 5. Things to Try Next (Next Steps)

- Adding more objects to the scene, e.g. *building_block*, and perform grasp planning on them
- Tune the planner parameters either from the skill execution’s side, or from the values used by the MoveIt Task Constructor (MTC) within *moveit_planning_service*. This should enable more robust grasp planning and IK solving across various scenarios.
- Set up custom hardware descriptions and MoveIt configs.

## 6. Architecture

The architecture diagram can be found [here](https://github.com/intrinsic-ai/intrinsic-omts/tree/main/third_party/intrinsic_moveit#architecture).

## 7. Troubleshooting

| Issue | Potential fix |
| --- | --- |
| Grasp planning fails to connect / times out, or the MoveIt RViz planning scene is empty or not synced with OMTS (e.g. arm [joint](../glossary/general_terms.md#joint) angles are wrong, collisions are missing) | Verify that `rs-moveit-planning-service-0`, `rs-flowstate-ros-bridge-0`, `rs-icon-0`, and `rs-ur-module-0` are `Running` with `kubectl get pods -n app-resources` (or `kubectl get pods --all-namespaces`), and inspect the planning service logs with `kubectl logs -n app-resources rs-moveit-planning-service-0`. In your ROS terminal, ensure `RMW_IMPLEMENTATION=rmw_zenoh_cpp` and `ZENOH_CONFIG_OVERRIDE='mode="client";connect/endpoints=["tcp/127.0.0.1:7447"]'` are set, and check that joint states are streaming with `ros2 topic echo /joint_states --once`. |
| `Grasp skill 'ai.intrinsic.moveit_plan_grasp_skill' is not available` or `rs-moveit-planning-service-0` is absent from `kubectl get pods -n app-resources` | The deployed solution predates `intrinsic-moveit` `v0.1.0`. Update `~/intrinsic-omts` to `main` and redeploy `//:omts_solution`. |
| Grasp plan not found | This generally occurs when the motion distance is large, or the object is quite occluded (e.g. in the CNC enclosure). Since the default planner uses a sampling based planner, we can increase the planning timeout with `--timeout_ms` on *moveit_plan_grasp_and_move*, or `moveit_timeout_ms` in `app_config.yaml` for the OMTS cycle. More configurations to choose planners or configure the planners will be in future releases. |
| Grasp plan not found for different objects | The gripper opening maxes out at 50mm, if the object has widths on all sides that are larger than 50mm, no valid grasp approach plans will be found. Try a smaller object, e.g. building_block. |
| MoveIt RViz shows the wrong robot model (e.g. UR5e on a `lab_bb_01` deployment), or custom hardware does not match the MoveIt planning scene | Both `omts` (UR5e, default) and `lab_bb_01` (UR3e, `--config=lab_bb_01`) are supported out of the box. Verify `RuntimeContext configured robot_config` in `kubectl logs -n app-resources rs-moveit-planning-service-0`, ensure `moveit_rviz.launch.py` is launched from the matching `<cell>_moveit_config` package (`omts_moveit_config` vs `lab_bb_01_moveit_config`), or pass `robot_config:=lab_bb_01` when launching `service.launch.py` from source. Custom robot cells require adding a hardware description and MoveIt config package to *intrinsic-moveit*. |
