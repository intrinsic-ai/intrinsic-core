# MoveIt Grasp Planning

The purpose of this tutorial is to showcase an integration of Intrinsic Core with *MoveIt* through [OMTS](../glossary/intrinsic_terms.md#open-machine-tending-solution-omts). We will first set up a ROS colcon workspace to build [*intrinsic-moveit*](https://github.com/intrinsic-ai/intrinsic-moveit). After deploying the OMTS [solution](../glossary/intrinsic_terms.md#solution), we will launch the *moveit_planning_service*. When the planning [scene](../glossary/intrinsic_terms.md#scene) on Rviz has been initialized, we can plan grasps for objects in the scene using *MoveIt*, first standalone, and then as the grasp planner of the full OMTS machine tending cycle.

> [!NOTE]
> Note that this integration is only showing cuboid grasp planning using MoveIt, verified by IK and collision checks, but the motion planning and execution will still be relying on Intrinsic Core capabilities.

![MoveIt Grasp Planning Overview](../../img/learn/tutorials/moveit_grasp_planning_overview.gif)

## 1. Prerequisites and Environment Setup

Users will need to have installed [ROS lyrical](https://docs.ros.org/en/lyrical/Get-Started/Installation/Ubuntu-Install-Debs.html).

Users will first need to build the colcon workspace for *intrinsic-moveit*.

```bash
# Set up the workspace and clone the repository
mkdir -p ~/ws_intrinsic_moveit/src && cd ~/ws_intrinsic_moveit/src
gh repo clone intrinsic-ai/intrinsic-moveit
# Prepare and install the dependencies
source /opt/ros/lyrical/setup.bash
vcs import . < intrinsic-moveit/lyrical.repos
cd ~/ws_intrinsic_moveit
rosdep install --from-paths . --ignore-src -r -y
sudo apt update && sudo apt install -y ros-lyrical-rmw-zenoh-cpp
# Build, this will take a while, time for another cup of coffee
colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  --symlink-install --packages-up-to moveit_planning_service
```

Users will then need to start the OMTS [simulation](../glossary/general_terms.md#simulation).

```bash
cd ~/intrinsic-omts
# The MoveIt integration is not part of the 20260922.0 release yet
git fetch origin && git checkout 48be90d563602d39d9f87f0c3a3074678dd28d49
bazel run //:omts_solution -c opt -- \
  --address localhost:17080 --operation_mode=sim
```

Once the solution is up and running, verify with Rviz that it should look like this.

![MoveIt Initial RViz](../../img/learn/tutorials/moveit_rviz_initial.png)

The deployed solution already includes everything this integration needs on the Intrinsic Core side, so there is nothing to install or reconfigure by hand:

- The *moveit_plan_grasp_skill* [skill](../glossary/intrinsic_terms.md#skill), which OMTS calls to obtain pre-grasps and grasps from the *moveit_planning_service*, is installed with every deployment.
- The *flowstate_ros_bridge* [service](../glossary/intrinsic_terms.md#service) runs with a default configuration that already streams the `/tf` and `/joint_states` topics MoveIt needs.

We can now start the *moveit_planning_service*.

```bash
# These commands are generally required for all terminals running ROS
source ~/ws_intrinsic_moveit/install/setup.bash
export RMW_IMPLEMENTATION=rmw_zenoh_cpp
export ZENOH_CONFIG_OVERRIDE='mode="client";connect/endpoints=["tcp/127.0.0.1:7447"]'
# Start the service
ros2 launch moveit_planning_service service.launch.py headless:=false \
  start_service_status_monitor:=false
```

Once the planning service launches, verify that the additional Rviz window should look like below. The [robot](../glossary/general_terms.md#robot) is represented by its meshes, while all other objects in the scene are propagated as collision objects and represented as green meshes.

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
# Updating the scene, see tutorial "Cell customization"
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
# Relocate Workpiece to the CNC Vice
bazel run //tools/world:apply_scene_updates -- \
  --address=localhost:17080 \
  --files configs/omts/raw_stock_in_vise.updates.pbtxt
# Plan again to grasp the workpiece that is in the vice now
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
| Planning scene is not synced with OMTS, e.g. arm [joint](../glossary/general_terms.md#joint) angles are wrong, collisions are missing, etc | Verify that the icon, ur_module and flowstate_ros_bridge service are running with `kubectl get pods --all-namespaces`, or check that joint states are being published, `ros2 topic echo /joint_states --once`. Redeploying the solution restarts *flowstate_ros_bridge* with its default configuration. |
| `Grasp skill 'ai.intrinsic.moveit_plan_grasp_skill' is not available` when running `omts_app --grasp_planner=moveit` | The deployed solution predates the MoveIt integration. Update `~/intrinsic-omts` to `main` and redeploy the solution. |
| Grasp plan not found | This generally occurs when the motion distance is large, or the object is quite occluded (e.g. in the CNC enclosure). Since the default planner uses a sampling based planner, we can increase the planning timeout with `--timeout_ms` on *moveit_plan_grasp_and_move*, or `moveit_timeout_ms` in `app_config.yaml` for the OMTS cycle. More configurations to choose planners or configure the planners will be in future releases. |
| Grasp plan not found for different objects | The gripper opening maxes out at 50mm, if the object has widths on all sides that are larger than 50mm, no valid grasp approach plans will be found. Try a smaller object, e.g. building_block. |
| After changing out the hardware (e.g. arm, enclosure, gripper) in Intrinsic Core, the MoveIt planning scene does not match the setup | Intrinsic-moveit currently only supports the hardware setup of OMTS. We will next work on abstracting the base hardware description layer such that users can more easily set up their own hardware description and MoveIt config. |
