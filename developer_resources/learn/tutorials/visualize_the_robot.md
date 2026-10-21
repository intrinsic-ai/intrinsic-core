# Visualize the robot

In this tutorial, you can choose one or both of these UIs to see the state of the [robot](../glossary/general_terms.md#robot):

* RViz shows the state that the software *believes* the robot and any objects around it are in (the "belief world"). It's useful for visualizing both real and simulated [solutions](../glossary/intrinsic_terms.md#solution).
* Gazebo shows the state of the simulated robot and any objects around it (the "simulation world"). It's useful for visualizing the physics [simulation](../glossary/general_terms.md#simulation), because it shows you how objects move under the force of gravity or contact with other objects, which is not visible in RViz without additional setup.

Since Gazebo is simulating the physical world, when using a real robot, you'd just look at the robot instead of using Gazebo to visualize the state of the simulation.

These two states are often the same, although they can differ: for example if the robot is disconnected from the PC and jogged to another position, or if a [pose](../glossary/general_terms.md#pose) estimator returns a bad estimate, leading to an incorrect position in the Object World [Service](../glossary/intrinsic_terms.md#service) and RViz. To learn more, see the "Belief world" section of [World concepts](../platform_introduction/world_concepts.md).

## Prerequisites

First, follow [Getting started](getting_started.md) to start an Intrinsic Core solution.

## Option A: Use RViz to visualize the execution state

RViz is part of the ROS project, so we'll begin by installing that. Note that RViz relies on a "ROS Bridge" component in the [Open Machine Tending Solution (OMTS)](../glossary/intrinsic_terms.md#open-machine-tending-solution-omts) solution.

1. Install "ros-lyrical-desktop" by following the [Installing on Ubuntu — how-to](https://docs.ros.org/en/lyrical/Get-Started/Installation/Ubuntu-Install-Debs.html), then return to this tutorial.
2. Run this command to ensure you have the necessary [packages](../glossary/intrinsic_terms.md#package):

   ```bash
   sudo apt install ros-lyrical-desktop ros-lyrical-rmw-zenoh-cpp
   ```

   *If this fails, double-check you've [installed ROS correctly](https://docs.ros.org/en/lyrical/Get-Started/Installation/Ubuntu-Install-Debs.html) before retrying.*

3. Start RViz:

   ```bash
   # Set up the environment for ROS 2.
   source /opt/ros/lyrical/setup.sh
   # Tell RViz to use the Zenoh communication middleware to talk to the ROS Bridge.
   export RMW_IMPLEMENTATION=rmw_zenoh_cpp
   # Tell Zenoh to connect to the Zenoh Router exposed on port 17447.
   export ZENOH_CONFIG_OVERRIDE='mode="client";connect/endpoints=["tcp/127.0.0.1:17447"]'
   # Start RViz.
   choom -n 1000 ros2 run rviz2 rviz2
   ```

   _This uses `choom` to ensure that if RViz uses too much RAM, the kernel stops it before interfering with other processes. It's necessary because of how Kubernetes handles "best effort" processes._

> [!NOTE]
> If you see "could not connect to display", make sure you're running this command from a graphical environment where the DISPLAY environment variable is set. If you're using the Server edition of Ubuntu, you'll either need to install a graphical environment, or use an SSH tunnel or similar to run these components in a graphical environment and tunnel ports 17447 and 17080 to the PC running Intrinsic Core.

4. You should see an empty 3D view:

   ![RViz empty 3D view](../../img/learn/tutorials/rviz_empty_3d_view.png)

5. In the **Displays** panel on the left, select **Global Options** > **Fixed Frame** > **root**.
6. Click **Add**, select **MarkerArray**, then **OK**.
7. Below **MarkerArray**, select **Topic** > **/workcell_markers**.
8. Below **Topic**, select **Durability Policy** > **Transient Local**.
9. You should see a 3D view including a robot:

   ![RViz robot 3D view](../../img/learn/tutorials/rviz_robot_3d_view.png)

### Troubleshooting

* If you see: **Fixed Frame**: **No tf data. Actual error: Frame [root] does not exist**, or if some or all of the **meshes are missing** from the display, then you may need to restart the bridge:
  * Because the bridge runs inside [Kubernetes](../glossary/general_terms.md#kubernetes-k8s), we can use k9s, a terminal UI for managing Kubernetes, to restart the bridge.
  * In the terminal, run:

    ```bash
    k9s -n app-resources -c pods
    ```

    *The bridge runs as a Kubernetes "[pod](../glossary/general_terms.md#pod)" in a namespace called "app-resources", alongside the robot [controller](../glossary/general_terms.md#controller) and other long-running services.*
  * Select "rs-flowstate-ros-bridge" and press Ctrl+D to delete the old pod, then select OK.
  * This will terminate the running instance so it gets restarted by Kubernetes in a new pod.

## Option B: Use Gazebo to visualize the simulation

Gazebo requires additional steps to be able to load the 3D meshes from the solution:

1. Install the latest version of Gazebo (including the UI) by following [Binary Installation on Ubuntu](https://gazebosim.org/docs/latest/install_ubuntu/).
2. Apply a workaround for older Intrinsic Core builds: (you'll need to repeat this any time you see errors from Gazebo about missing meshes)

   ```bash
   if [[ ! -d /tmp/service_volumes/intrinsic/gzserver-meshes ]] ; then
     mkdir -p /tmp/service_volumes/intrinsic
     sudo cp -r /proc/$(pgrep asset_sim)/root/mnt/gzserver-meshes/ /tmp/service_volumes/intrinsic/
   fi
   ```

3. Ensure the meshes are available under the expected path:

   ```bash
   sudo ln -s /tmp/service_volumes/intrinsic/gzserver-meshes /mnt/
   ```

4. Start Gazebo:

   ```bash
   GZ_PARTITION=intrinsic_sim gz sim -g
   ```

You should see a 3D view including the robot:

![Gazebo robot 3D view](../../img/learn/tutorials/gazebo_robot_3d_view.png)

### Troubleshooting

* If the Gazebo GUI (Graphical User Interface) doesn't load (empty window, no output on terminal), it means it can't connect to the Gazebo server. Double-check that you started the Solution with `--operation_mode=sim` as described in [Getting Started](getting_started.md).
  * Because the Gazebo server runs inside Kubernetes, we can use k9s, a terminal UI for managing Kubernetes, to check the logs or restart it.
  * In the terminal, run:

    ```bash
    k9s -n app-resources -c pods
    ```

    *The Gazebo server runs as a Kubernetes "pod" in a namespace called "app-resources", alongside the robot controller and other long-running services.*
  * If you don't see "rs-gazebo-simulator-0", it indicates no solution is running. Start the solution as described in [Getting Started](getting_started.md).
  * Select "rs-gazebo-simulator-0" and press `l` to see the logs. If you see `noop.go:... Server is now listening [...]`, it indicates that this is a "no-operation" simulator because the Solution was started in the "real" operation mode. Stop the solution before restarting as described in [Getting Started](getting_started.md):

    ```bash
    inctl solution stop --address localhost:17080
    ```

To learn more, see the [Gazebo documentation](https://gazebosim.org/docs/latest/gui/).

## What's next

Go to [Jog the robot](jog_the_robot.md) to move the simulated robot.
