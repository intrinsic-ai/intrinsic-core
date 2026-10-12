# Getting Started

This guide walks through setting up your first Intrinsic Core environment and loading the Open Machine Tending Solution ([OMTS](../glossary/intrinsic_terms.md#open-machine-tending-solution-omts)).

> [!NOTE]
> In Intrinsic Core, a **[Solution](../glossary/intrinsic_terms.md#solution)** refers to the combination of software and configuration needed to realize an automation task. In the case of OMTS, the task is loading and unloading a CNC machine.

## Prerequisites

Intrinsic Core requires Ubuntu 26.04. To visualize your solution, we recommend installing [Ubuntu 26.04 Desktop](https://ubuntu.com/desktop/docs/en/latest/tutorial/install-ubuntu-desktop/).

## Recommended computer specs

* **CPU**: x86-64 Architecture (6-core / 12-thread @ 4.9 GHz or higher, e.g.,Intel i7-13700 or better). Note: For realtime control of [robot](../glossary/general_terms.md#robot) hardware, you'll need an Intel CPU, although for [simulation](../glossary/general_terms.md#simulation), you can also use AMD CPUs. ARM architectures are strictly unsupported at this time.
* **GPU**: Integrated Graphics sufficient for simulation but dedicated NVIDIA RTX 3060/4060+ required for ML/Vision workloads. Running full OMTS in SIM with Perception REQUIRES dedicated GPU.
* **RAM**: 32 GiB DDR4/DDR5 minimum (64 GiB recommended). Note: Small solutions may even work on smaller systems (16 GiB), but you'll need to take care to run builds with a limit on the number of jobs and to stop the solution before running builds. Alternatively, running builds and visualization on another PC takes the load off the real-time PC's memory. 
* **Storage**: 1 TB NVMe SSD (minimum 100 GB dedicated free space).
* **Networking**: 2-3 Gigabit Ethernet ports (Recommended setup: Port 1: LAN/Internet; Port 2: Real-time Robot [Controller](../glossary/general_terms.md#controller), Optional port 3: PoE Camera switch).

## Step 1: Download and deploy Intrinsic Core

Intrinsic Core uses [Kubernetes](../glossary/general_terms.md#kubernetes-k8s) to run containerized automation software on the PC. Specifically, it uses [k3s](https://k3s.io/), which is designed for resource-constrained environments. We'll set that up and deploy Intrinsic Core to it:

1. Install git-lfs, which manages large files like geometric models in Git repositories:

   ```bash
   sudo apt update
   sudo apt install -y git git-lfs
   git lfs install
   ```

2. Install and log in to the GitHub CLI, which will be used to fetch sources and release artifacts from GitHub:

   ```bash
   sudo apt install gh
   gh auth login
   ```

   When prompted by the GitHub CLI:
   * Press Enter to accept defaults (Github.com, HTTPS, Yes, Login with a web browser).
   * If necessary, open https://github.com/login/device in your browser window.
   * Copy the one-time code from the terminal output to the browser window.
   * Click through the rest of the auth flow. Once you see "Congratulations, you're all set!" in the browser, return to the terminal.
   * You should see "Logged in as ..." in the terminal.

3. Next, fetch the source code. These tutorials assume you fetch the sources in your home directory:

   ```bash
   cd ~
   export CORE_TAG=20261012.0
   export OMTS_TAG=`gh release view --repo intrinsic-ai/intrinsic-omts --json tagName --jq '.tagName'`

   gh repo clone intrinsic-ai/intrinsic-core -- --revision="${CORE_TAG:?}"
   gh repo clone intrinsic-ai/intrinsic-omts -- --revision="${OMTS_TAG:?}"
   ```

4. Install k3s:

   ```bash
   sudo apt install -y curl
   ~/intrinsic-core/intrinsic_runtime/setup_k3s.sh
   ```

5. Make sure that your shell has the access it needs:

   ```bash
   sudo apt install -y util-linux-extra
   newgrp containerd
   ```

6. Download and deploy Intrinsic Core:

   ```bash
   gh release download --repo intrinsic-ai/intrinsic-core "${CORE_TAG:?}" \
     --pattern intrinsic-base-linux-amd64.tar \
     --output /tmp/intrinsic-base-linux-amd64.tar --clobber
   tar -C /tmp -xvf /tmp/intrinsic-base-linux-amd64.tar
   /tmp/intrinsic-base
   ```

   You should see:

   ```text
   [... lots of output ...]
   App status at HH:MM:DD:
     ChartAssignment app-intrinsic-base All pods are running; maybe they're working, maybe they're not!


   Timing: XX.XX seconds to install Workcell Spec.
   ```

7. Install the [inctl](../glossary/intrinsic_terms.md#inctl-intrinsic-control-cli) ("in control") CLI, which you'll use to control Intrinsic Core:

   ```bash
   gh release download --repo intrinsic-ai/intrinsic-core "${CORE_TAG:?}" \
     --pattern inctl-linux-amd64 \
     --output /tmp/inctl-linux-amd64 --clobber
   sudo mv /tmp/inctl-linux-amd64 /usr/local/bin/inctl
   sudo chmod +x /usr/local/bin/inctl
   ```

8. If you need to run supplied perception packages, it is required to run the following script to configure the K3s setup for GPU support.

   ```bash
   sudo ~/intrinsic-core/intrinsic_runtime/setup_nvidia.sh
   ```

## Step 2: Build and deploy OMTS

Now Intrinsic Core is running, we can prepare to build and deploy the Open Machine Tending Solution (OMTS):

1. Before building a Solution, you'll need to install the [Bazel](https://bazel.build/) build system. We'll use Bazelisk, which manages Bazel versions:

   ```bash
   curl -L "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64" -o /tmp/bazelisk
   sudo mv /tmp/bazelisk /usr/bin/bazelisk
   sudo chmod +x /usr/bin/bazelisk
   sudo ln -sf /usr/bin/bazelisk /usr/bin/bazel
   ```

2. We also need this symlink for compatibility between the version of LLVM used by OMTS and Ubuntu 26.04:

   ```bash
   sudo ln -s /usr/lib/x86_64-linux-gnu/libxml2.so.16 /usr/lib/x86_64-linux-gnu/libxml2.so.2
   ```

3. Compile and run the Open Machine Tending Solution using Bazel:

   <details>
     <summary>Note: This takes ~50 minutes to build. Expand this section to learn more about what's going on.</summary>

     This command uses the [Bazel build](https://bazel.build/) system to build OMTS and its dependencies from source. It takes a long time because it builds components like the Gazebo simulator, the computation geometry libraries used to track the positions of the objects around the robot, and many more. This is time-consuming, but has the advantage that you can easily patch and rebuild these dependencies.

     This command tells Bazel to build, and then run, the binary identified by the [label](https://bazel.build/concepts/labels) `//:omts_solution`. Bazel reads [BUILD files](https://bazel.build/concepts/build-files) to know what to build: [intrinsic-omts/BUILD](https://github.com/intrinsic-ai/intrinsic-omts/blob/main/BUILD) lists the dependencies that Bazel needs to build as part of this Solution. The `--` separates the build instructions from the runtime flags that will be used when deploying. For example, if you wanted a debug build, you could add `-c dbg` before the `--`, but not after.

     When the Solution has been built, it will be sent to Intrinsic Core over port 17080. If you want to use different PCs for building and running Intrinsic Core, you can use this to identify the PC where the solution will be deployed.

     The Solution is deployed in simulation (`operation_mode=sim`): This means that when you command the robot to move, it will move a simulated robot, and when you capture images with a camera, your Solution will use 3D renders of the simulated scene: This lets you test your Solution before you deploy it to a real robot.
   </details>

   ```bash
   cd ~/intrinsic-omts
   bazel run //:omts_solution --config=lab_bb_01 -- \
     --address localhost:17080 --operation_mode=sim
   ```

   After a lot of build warnings and status output, you should see:

   ```text
   Timing: XX.XX seconds to process assets.
   Timing: XX.XX seconds to deploy application.
   Timing: XX.XX seconds to wait for ready
   ```

## Troubleshooting

| Symptom | Cause | Resolution |
| :--- | :--- | :--- |
| Check failed: ::intrinsic::scene_object::MainImpl() is OK (INTERNAL: Failed to load mesh /path/to/file.glb. Error is No suitable reader found for the file format of file "/path/to/file.glb".; Please validate mesh file '/path/to/file.sdf'; while parsing 0th visual geometry 'base_visual' of link 'base_link'; While parsing links in SDF Model block_50x50x75 to convert to an Intrinsic Scene Object. | The git repo contains large files, but these haven't been fetched to the local clone, and the parser fails to parse the placeholder file. | `sudo apt install git-lfs && git lfs install && git lfs pull` |
| Error: failed to process Asset ai.intrinsic.omts_enclosure: failed to process SceneObject bundle: failed to process bundle from ...: failed to walk tar file to process assets: error processing file "omts_enclosure_scene_object.gzf": failed to process object: could not upload "3b31bd16a12c40bf" to CAS: failed to upload to CAS: closing stream: rpc error: code = Unimplemented desc = | A required Runtime service is not running. | Retry the step "Download and deploy Intrinsic Core". Because Intrinsic Core runs inside Kubernetes, we can use k9s, a terminal UI for managing Kubernetes, to check the logs.<br>In the terminal, run: `k9s -n app-intrinsic-base -c pods`.<br>The Runtime of Intrinsic Core runs as Kubernetes "[pods](../glossary/general_terms.md#pod)" in a namespace called "app-intrinsic-base": Check that these are healthy. |
| Error: failed to process Asset ai.intrinsic.attach_object_to_robot: failed to process Skill bundle: failed to process bundle from ...: failed to walk tar file to process assets: error processing file "attach_object_to_robot_skill_image.tar": failed to process image: image write failed: check image failed: rpc error: code = Unknown desc = failed to dial "/run/containerd/containerd.sock": connection error: desc = "transport: error while dialing: dial unix /run/containerd/containerd.sock: connect: connection refused" | The service that manages Asset container images cannot connect to containerd, the daemon that runs containers. | Restart the service: In the terminal, run: `k9s -n app-intrinsic-base -c pods`.<br>Select the line "artifacts-deployment-...", press Ctrl+D to delete the pod, and select Enter, then retry. |
| Error: failed to process Asset ai.intrinsic.inference_service: failed to process Service bundle: failed to process bundle from...: failed to walk tar file <br>to process assets: error processing file "triton_inference_server_image.tar": failed to process image: could not process tar file "triton_inference_server_image.tar": failed to write reader data to temp file: write /tmp/read-opener-120377528: disk quota exceeded<br>(or "no space left on device") | Your /tmp is either too small, or has a restrictive quota, and the large container images in the OTMS solution fill it up. | `mkdir -p ~/tmp`<br>`TMPDIR=~/tmp bazel run //:omts_solution -- --address localhost:17080 --operation_mode=sim` |
| `failed to get solution information: rpc error: code = Unavailable desc = connection error: desc = "transport: Error while dialing: dial tcp 10.43.123.92:9777: connect: connection refused"` or `Error: failed to process Asset ai.intrinsic.ioc_pose_estimation.pose_estimator.foundationpose: failed to process Data Asset bundle: failed to read Data bundle: failed to process in-tar reference "data_files/foundationpose_refine.onnx": failed to start upload: rpc error: code = Internal desc = failed to create CAS stream: rpc error: code = Unavailable desc = connection error: desc = "transport: Error while dialing: dial tcp 10.43.248.104:9747: connect: connection refused"` | The IP address of your PC may have changed since it was initially set up. | Reconfigure your PC to use a fixed DHCP lease or static IP. Remove k3s, then follow [Getting started](getting_started.md) to set it up fresh: `sudo /usr/local/bin/k3s-uninstall.sh` then `sudo rm -rf /var/lib/rancher /etc/rancher/ /var/lib/longhorn/ /etc/cni/` |

## Next steps

Continue to [Visualize the robot](visualize_the_robot.md) to see the solution you've deployed.
