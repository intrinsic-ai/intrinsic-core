# Debugging assets, solutions, bundle validation, and `Kubernetes` pod scheduling

This reference guide details failure modes and diagnostic procedures across control-plane CLI subcommands, custom service manifests, direct OCI registry uploads, protobuf descriptor set validation, solution lifecycle gates, and Kubernetes pod scheduling on local workcell environments (`--address=localhost:17080`).

## Input-aware diagnostic decision trees

Follow this input-aware decision tree when diagnosing asset, bundle, solution, or pod scheduling anomalies:

```
[Precondition: Asset, bundle, solution, or pod failure observed on local workcell]
  │
  ├──► [Diagnostic check: inctl --help prints only logging flags (-alsologtostderr, -v)]
  │      └──► [Targeted action: Run inctl help or inctl <subcommand> --help to access the Cobra command tree.]
  │
  ├──► [Diagnostic check: inctl doctor check reports "no org name provided"]
  │      └──► [Targeted action: Run verified 4-step sequence: service state list, asset instance list, icon status, world reset.]
  │
  ├──► [Diagnostic check: inctl service add fails with "cannot parse invalid wire-format data"]
  │      └──► [Targeted action: Pack configuration inside google.protobuf.Any and serialize to binary .binpb wire format.]
  │
  ├──► [Diagnostic check: Ingress gRPC call returns UNIMPLEMENTED (HTTP 404)]
  │      └──► [Targeted action: Place service blocks directly in primary proto_library rules rather than transitive dependencies.]
  │
  ├──► [Diagnostic check: Solution startup fails reporting "Ports not open: executive...:8080"]
  │      └──► [Targeted action: Check upstream dependencies in order (simulation-service:8088 and gzserver:50053 mesh loading).]
  │
  ├──► [Diagnostic check: Pod remains permanently Pending with "Insufficient nvidia.com/gpu"]
  │      └──► [Targeted action: Check virtual GPU time-slicing replicas (typically 8 or 16); reduce camera or model concurrency.]
  │
  └──► [Diagnostic check: Real-time pod crashloops with "no thread CPU affinity was found"]
         └──► [Targeted action: Delete pod via kubectl delete pod -n app-resources to trigger placement onto the rtpc node.]
```

### Diagnostic decision matrix

| Observed symptom | Precondition / failure signature | Diagnostic check | Targeted remediation action |
| :--- | :--- | :--- | :--- |
| `inctl --help` outputs only logging flags | Executed `inctl --help` at top level | Check if Go `flag` package intercepted command | Run `inctl help` or `inctl <subcommand> --help` to access Cobra tree. |
| `inctl doctor` reports `"no org name provided"` | External VM build without cloud organization switcher | Inspect CLI output for missing organization flags | Run verified 4-step sequence: `service state list`, `asset instance list`, `icon status`, `world reset`. |
| Cloud CLI hangs with 504 Gateway Timeout | Target VM dropped tunnel while stored in config | Inspect `~/.config/intrinsic/user.config` for `selectedCluster` | Clear `selectedCluster` entry from `~/.config/intrinsic/user.config`. |
| `inctl service add` fails on configuration | Configuration passed as `.textproto` or JSON | Check configuration file format and extension | Serialize configuration inside `google.protobuf.Any` to binary `.binpb` wire format. |
| Direct OCI upload fails with `PodFitsHostPorts` | `artifacts-deployment` pod recreating under `RollingUpdate` | Check `kubectl get pods -n app-intrinsic-base` | Set `strategy.type: Recreate` on `artifacts-deployment` manifest. |
| `inctl asset install` fails on containerd socket | `failed to dial "/run/containerd/containerd.sock": connect: connection refused` or port 17127 refused | Backend `artifacts-deployment` cannot connect to host `k3s` containerd daemon | Stop after at most 2 attempts. Failure is on the backend server (`k3s` host daemon), NOT local sandbox. Do not run `find /run` or proxy sockets. Fall back to local hermetic Bazel verification (`bazel build //...`, `bazel test //...`). |
| Registry pull fails with certificate error | Workload pulls from `direct.upload.local` over HTTPS | Verify `/etc/containerd/certs.d/direct.upload.local/hosts.toml` | Ensure `hosts.toml` sets `skip_verify = true` and `capabilities = ["pull", "resolve"]`. |
| Ingress call returns `UNIMPLEMENTED` (HTTP 404) | Service defined in transitive proto dependency | Inspect `proto_library` rule of target service | Place `service` block directly in primary `proto_library` referenced by ingress generator. |
| Solution startup reports `Ports not open: executive...:8080` | Synchronous upstream gRPC channel timeout | Check `simulation-service:8088` and `gzserver:50053` logs | Inspect Gazebo mesh loading and upstream dependency status before restarting executive. |
| Resource pod crashloops on missing simulator | Resource pod logs `FAILED_PRECONDITION: Please install a Gazebo Simulator Asset` | Check `inctl asset instance list --address=localhost:17080` | Add an explicit `gazebo_simulator` (`ai.intrinsic.gazebo_simulator`) asset instance. |
| Pod permanently `Pending` with GPU error | Physical GPU time-sliced virtual replicas exhausted | Inspect `Events` via `kubectl describe pod -n app-intrinsic-app-chart` | Reduce simulated camera count or consolidate perception model containers. |
| Real-time pod crashloops with affinity error | Pod scheduled onto non-realtime node after reboot | Check `nodeName` via `kubectl get pod <pod> -o wide` | Delete pod (`kubectl delete pod <pod> -n app-resources`) to trigger reschedule onto `rtpc` node. |
| Pod crashloops with `SIGBUS` after hard power cut | Truncated overlayfs layer extracted without filesystem sync | Check `dmesg` and container exit codes | Enable `image_pull_with_sync_fs = true` in `/etc/containerd/config.toml` and reinstall image. |

## Control-plane `CLI` subcommands and local flag semantics

Use the `/usr/local/bin/inctl` binary.

### Subcommand availability and verified local inspection sequence

- **Top-level help discovery**: Execute `inctl help` or `inctl <subcommand> --help` to inspect commands (running `inctl --help` at top level triggers Go's standard library flag parser and prints only standard logging flags like `-alsologtostderr`, `-log_dir`, `-v`).
- **Active runtime service states vs. instantiated assets**:
  - `inctl service state list --address=localhost:17080 --output=json` lists active runtime service instances and their raw gRPC state codes (`STATE_CODE_ENABLED`, `STATE_CODE_ERROR`). Non-JSON output condenses states into human-readable strings (`State: Enabled`). Always inspect JSON output, as raw `STATE_CODE_ERROR` can be masked by condensed tabular summary strings. Note that `inctl service list` is not a valid subcommand; `inctl service` provides `add`, `create`, `delete`, and `state`.
  - `inctl asset instance list --address=localhost:17080` maps instantiated solution resource names (`icon`, `ur_module`, `gazebo_simulator`) to backing asset package IDs.
  - `inctl asset list --address=localhost:17080` lists installed catalog asset definitions (`--asset_types=service` filters by service assets).
- **Verified local inspection sequence replacing `inctl doctor`**:
  `inctl doctor check` only inspects cloud authentication and cloud control-plane cluster/solution discovery (`inctl auth list`, `inctl cluster list`, `inctl solution get`), rather than local `--address=localhost:17080` workcell services. On standalone local workcells, use the verified 4-step local inspection sequence instead:
  ```bash
  # Step 1: Check service health and raw gRPC state codes
  inctl service state list --address=localhost:17080 --output=json

  # Step 2: Map active solution resource instances to asset package IDs
  inctl asset instance list --address=localhost:17080

  # Step 3: Verify ICON operational and safety status (--instance_name=icon is mandatory)
  inctl icon status --instance_name=icon --address=localhost:17080

  # Step 4: Reset belief world and simulation synchronization if tracking stalls
  inctl world reset --address=localhost:17080
  ```

### Addressing flag split and transport credentials

- **Commands requiring `--address=localhost:17080`**: Supported by `inctl asset`, `inctl service`, `inctl skill`, `inctl icon`, `inctl solution start`, `inctl solution stop`, and `inctl world`.
  - When calling `inctl icon` subcommands (`status`, `enable`, `disable`, `clear-faults`, `list-parts`, `list-actions`), pass `--instance_name=icon` explicitly. Omitting `--instance_name` causes the ingress router to return `rpc error: code = Unimplemented desc = `.
  - Note that `inctl solution list` and `inctl solution get` query cloud registries only and reject `--address=localhost:17080` (`unknown flag: --address`). Use `inctl service state list` and `inctl asset instance list` to inspect local solutions.
- **Commands requiring `--server=localhost:17080`**: Supported by `inctl process` (`get`, `set`).

### Clearing stale `selectedCluster` headers in user configuration

When `~/.config/intrinsic/user.config` retains a `selectedCluster` entry, `inctl` attaches `x-server-name: <cluster_id>` gRPC metadata to outgoing requests. Cloud proxy routers intercept requests bearing `x-server-name` and attempt to route them down reverse relay tunnels. If a target virtual machine tunnel dropped after shutdown or reboot, control-plane commands hang and fail with `504 Gateway Timeout`. Clear `selectedCluster` in `~/.config/intrinsic/user.config` when encountering tunnel timeouts.

## Custom service manifest schemas and binary configuration requirements

When creating or bundling custom service assets (`inctl service create <asset_id>` or `inbuild service bundle`), enforce requirements defined by `intrinsic_proto.services.ServiceManifest`.

### Manifest schema fields and port allocation

- **Mandatory vendor metadata**: Ensure `metadata.vendor.display_name` is populated (e.g., `vendor { display_name: "Example Organization" }`). Manifests omitting a vendor display name fail bundle validation with `no vendor specified for <id>`.
- **Container archive and argument placement in `ServicePodSpec`**:
  - Specify container image tarballs via `image { archive_filename: "<name>.tar" }` inside `real_spec` and `sim_spec`.
  - Specify runtime command-line arguments within `image { settings { args: ["--mode=real"] } }` (`ServiceImageSettings`). The parent `ServicePodSpec` does not contain a top-level `args` field.
- **Port allocation triggers in `ServiceDef`**:
  - `RuntimeContext.port` (gRPC port, field 1): Allocated by the orchestrator when `service_proto_prefixes` is non-empty (`grpc://<pkg>.<Service>`) or when `supports_service_state: true` is configured.
  - `RuntimeContext.http_port` (HTTP port, field 7): Allocated strictly when `http_config: {}` is present in `ServiceDef`. Bind custom HTTP ingress servers to `context.http_port`.

### Service configuration wire format and runtime context unpacking

- **Binary configuration requirement for `inctl service add --config`**:
  Serialize custom service configuration protobufs inside `google.protobuf.Any` to binary wire format (`.binpb`) before passing to `inctl service add --config` (passing `.textproto` or JSON files fails with invalid wire-format parsing errors: `proto: cannot parse invalid wire-format data`).
- **Runtime context unpacking (`/etc/intrinsic/runtime_config.pb`)**:
  The platform mounts `intrinsic_proto.config.RuntimeContext` inside service containers at `/etc/intrinsic/runtime_config.pb`. Verify the boolean return value when unpacking configuration payloads:
  ```python
  from google.protobuf import any_pb2
  from intrinsic.config.proto import runtime_config_pb2

  context = runtime_config_pb2.RuntimeContext()
  with open("/etc/intrinsic/runtime_config.pb", "rb") as f:
    context.ParseFromString(f.read())

  my_config = custom_service_pb2.CustomServiceConfig()
  if context.config.Is(custom_service_pb2.CustomServiceConfig.DESCRIPTOR):
    context.config.Unpack(my_config)
  ```
- **Runtime mutation of `ResolvedDependency`**:
  For asset-to-asset dependencies (`intrinsic_proto.assets.v1.ResolvedDependency`), the runtime dependency injection layer injects connection addresses into `interfaces` and populates `object`, while clearing `ResolvedDependency.name` to an empty string (`""`). Inspect `ResolvedDependency.interfaces` or `ResolvedDependency.object.name` rather than `ResolvedDependency.name` in running containers.

## Direct `OCI` registry uploads, storage, and descriptor set validation

### Direct `OCI` layer uploads and `containerd` scheduling

- **Local HTTP OCI upload endpoint (`localhost:17127`)**: Direct asset uploads push OCI image layers over HTTP to `localhost:17127`, which maps to `artifacts-deployment` via a static Kubernetes `hostPort: 17127`.
- **Backend containerd socket dial failure (`/run/containerd/containerd.sock`) circuit breaker**:
  When uploading images via `inctl asset install`, `inctl` pushes layers to `localhost:17127` (the `artifacts-deployment` pod in `k3s`). `artifacts-deployment` checks and imports image layers into containerd by communicating over `/run/containerd/containerd.sock`, which is a host volume mount inside the `k3s` pod.
  If the upload fails with:
  `failed to upload image (localhost:17127/...): check image failed: rpc error: code = Unknown desc = failed to dial "/run/containerd/containerd.sock": connect: connection refused`
  or connection refused on `localhost:17127`:
  - **Failure locus**: The failure is strictly on the backend cluster server side (`artifacts-deployment` inside `k3s` on the cluster host). The containerd socket and its mount belong inside `k3s` on the host node, NOT inside the agent's local client execution environment (e.g. Bubblewrap sandbox). The local client environment does not have and should not have `/run/containerd/containerd.sock`.
  - **Prohibited actions**: Do NOT attempt to debug local `/run` sockets, search for sockets with `find /run` or `find /`, or attempt to proxy or forward `/run/containerd/containerd.sock` into the sandbox.
  - **Circuit breaker action**: Stop after at most two attempts. Halt sideloading/cluster install attempts immediately, and proceed to verify the build and tests locally using hermetic Bazel commands (`bazel build //...` and `bazel test //...`).
- **Resolving `PodFitsHostPorts` scheduling blocks**:
  Configure `artifacts-deployment` with `strategy.type: Recreate` on single-node clusters (`RollingUpdate` for pods binding static `hostPort: 17127` deadlocks on `PodFitsHostPorts` when the scheduler attempts to run old and replacement pods concurrently on the same host node).
- **Resolving `ErrImagePull` on `direct.upload.local` references**:
  If a workload references `direct.upload.local/<image>@sha256:<digest>` and fails with `x509: certificate is valid for *, not direct.upload.local`, verify that `/etc/containerd/certs.d/direct.upload.local/hosts.toml` exists on the host and routes pulls over HTTP with `skip_verify = true` and `capabilities = ["pull", "resolve"]`.
- **Client session collisions and chunk retry resets**:
  - Outgoing upload requests should attach `intrinsic-client-id: <client_uuid>/<per_image_uuid>` to prevent concurrent uploads from colliding on shared server session keys (`codes.Aborted: repeated update request`).
  - If a network interruption breaks an active upload stream, the server deletes the session finalizer. Because clients populate manifest metadata only on chunk 0, retrying from chunk 2+ fails with `manifest content details not provided`. Restart the upload cleanly from chunk 0.
- **Resolving `InvalidImageName` from accumulated `sideloaded__` prefixes**:
  Repeated asset updates without matching internal registry skip lists prepend `sideloaded__` to the repository name on each pass (`sideloaded__sideloaded__...`). Once the repository name exceeds the 255-character Kubernetes limit, `kubelet` rejects the image with `InvalidImageName`. Re-install the asset cleanly or reset the solution branch to clear accumulated prefixes.
- **Content-Addressable Storage (`CAS`) mesh loading**:
  During scene composition, geometry meshes (`intcas://...`) load from local or fallback CAS. If organization metadata is missing, `gzserver` fails to load geometry with `unable to Stat "intcas://...": no org-id found`. On local workcells, direct upload (`localhost:17127`) replaces cloud CAS, and `ResourceRegistry` is superseded by `AssetInstances` / `AssetInstancesReader`.

### Protobuf `FileDescriptorSet` validation and merge mechanics

- **Two-stage descriptor merger behavior**:
  - **Go descriptor merger (`proto.Equal`)**: Deduplicates `FileDescriptorProto` entries by file path and requires strict byte equality. If two assets bundle the same `.proto` file compiled from different revisions or with reordered unregistered extensions, validation fails with `duplicate FileDescriptorProto "<file>" with different contents`. Ensure assets are built from synchronized dependency trees.
  - **C++ descriptor merger**: Retains the first `FileDescriptorProto` seen for a filename and skips subsequent duplicates. If an older descriptor loads first and lacks symbols required by a newer asset, cross-file symbol resolution fails.
- **Bundle command manifest requirements**:
  Asset ingestion rejects bundles with `required file descriptor set not specified for "<asset_id>"` if descriptor sets are omitted. Ensure `inbuild service bundle` invocations supply `--file_descriptor_set` (or `--augmented_file_descriptor_set`).
- **Ingress route generation from `direct_descriptor_set`**:
  Define gRPC `service` blocks directly within primary `proto_library` targets (isolating service definitions exclusively inside transitive dependencies causes ingress generators to omit service routes, resulting in `UNIMPLEMENTED` / HTTP 404 at Envoy).
- **Non-deterministic `google.protobuf.Any` resolution**:
  The HTTP/JSON `Any` resolver caches per-asset protobuf registries in an unordered map. If two installed assets define identical fully qualified message names with diverging schemas, type resolution returns whichever schema is visited first in map iteration order. Maintain unique protobuf package namespaces across independent assets.
- **Serialization of unbounded joint limits (`-Inf` / `+Inf`)**:
  Hardware module URDF deployment configurations frequently use IEEE 754 `-Inf` and `+Inf` for unbounded joint limits. Standard JSON serializers fail with `json: unsupported value: -Inf`. Use `protojson` or textproto formatting when exporting or inspecting deployment payloads.
- **4 MB gRPC message limits (`RESOURCE_EXHAUSTED`)**:
  Client gRPC channels enforce a 4 MB (`4,194,304` bytes) default receive message size. When scenes contain dense meshes, calls to `intrinsic_proto.world.v1.ObjectWorldService/ExtractResourceInstances` fail with `RESOURCE_EXHAUSTED`. Configure explicit 64 MB limits on client stubs (`grpc.max_receive_message_length = 64 * 1024 * 1024`).

## Solution lifecycle orchestration, startup gates, and pod scheduling

### Downstream readiness gate timeouts and startup cascades

Errors reporting `Ports not open: <service>.<namespace>:<port>` indicate downstream readiness gate timeouts caused by upstream services failing to initialize synchronously:

- **When `executive.app-intrinsic-app-chart:8080` is not open**:
  The `executive` service (`intrinsic_proto.executive.v1.ExecutiveService`) initializes 11 upstream gRPC client channels sequentially before binding port 8080:
  1. `data_logger`
  2. `skill_registry`
  3. `resource_registry`
  4. `installed_assets_service`
  5. `asset_info_internal_service`
  6. `code_execution_service`
  7. `world_service`
  8. `world_updater`
  9. `simulation_service` (port `8088`, waiting on `gzserver:50053` geometry loading)
  10. `solution_service`
  11. `conductor_service`
  If any upstream service fails to respond within connection timeouts, `executive` aborts before opening port 8080. Inspect `simulation-service` and `gzserver` logs (`kubectl logs -n app-intrinsic-base deployment/world`) to determine whether Gazebo mesh loading stalled.
- **When `logger.app-intrinsic-base:8080` is not open**: Inspect `timescaledb-0` in `app-intrinsic-base` for database migration or startup failures blocking port 5432.
- **When `kvstore-service.app-intrinsic-base:8080` is not open**: Inspect `kvstore-service` and `zenoh-router` logs for consensus timeouts (`DEADLINE_EXCEEDED: Timeout waiting for high consistency`).

### Solution redeployment races and mode switching

- **Namespace termination races (`0/0 pods ready` false positive)**:
  When reverting or redeploying a running solution, existing `ChartAssignment` namespaces enter asynchronous `Terminating` states. Because readiness controllers evaluate `readyPods == totalPods`, a deployment with `0/0` pods evaluates as ready (`True`), causing `conductor` to trigger execution before replacement skills are scheduled. Wait for terminating pods to clear (`kubectl get pods -A`) before applying new solution charts.
- **Solution mode-switching safety**:
  Execute `inctl solution stop --address=localhost:17080` before switching between simulation (`sim`) and physical hardware (`real`) modes (switching modes while workload pods or controllers are active leaves controllers holding shared memory locks and volume leases that block replacement pods).

### Pod scheduling diagnostics, resource constraints, and host interactions

- **GPU time-slicing exhaustion (`Insufficient nvidia.com/gpu`)**:
  Physical GPUs expose a fixed number of virtual time-sliced replicas (typically 8 or 16). Workloads running multiple simulated cameras (`genicam`), `gzserver`, `ml-models-service`, and perception pipelines can exhaust replicas. Inspect `kubectl describe pod <pod> -n app-intrinsic-app-chart` under `Events` for `Insufficient nvidia.com/gpu`.
- **Multi-container sidecar inspection**:
  In multi-container pods (`ml-models-service`), primary containers can report healthy logs while co-located model server sidecars fail with `Exit Code 128` or `StartError` (`nvml error`). Check all container statuses via `kubectl describe pod <pod> -n <namespace>`.
- **63-character Kubernetes label limit**:
  Keep custom skill and asset identifiers concise to remain within the 63-character Kubernetes label limit on generated `ChartAssignment` resources (`skill-intrinsic-<name>-<name>`).
- **Hard power-cycle `SIGBUS` crashloops (`image_pull_with_sync_fs`)**:
  If pods crashloop with `SIGBUS (BUS_ADRERR)` with zero logs after an abrupt power cut, overlayfs layers were truncated before disk synchronization. Enable `image_pull_with_sync_fs = true` in `/etc/containerd/config.toml` and reinstall affected assets.
- **GPU PCIe bus reset (`Xid 79`)**:
  When simulation pods fail with `UNAVAILABLE: gRPC channel to simulation-server... is unavailable`, check kernel logs (`dmesg`) for `NVRM: Xid ... 79, GPU has fallen off the bus`. Verify GPU status via `nvidia-smi` and reboot the host if the PCIe bus locked up.
- **Benign Cilium log artifacts (`nf_tables`)**:
  Cilium log messages stating `iptables rules full reconciliation failed... table 'nat' is incompatible, use 'nft' tool` are benign artifacts on host kernels using `nf_tables` and should not be treated as CNI failures unless accompanied by `502 Bad Gateway` errors on `zenoh-router`.

## Paired deterministic safety guardrails

1. **CLI help discovery**: Run `inctl help` or `inctl <subcommand> --help` to access the Cobra command tree; do not run `inctl --help` at top level, which triggers standard Go logging flag parsing.
2. **Service configuration wire format**: Serialize configuration protobufs inside `google.protobuf.Any` to binary wire format (`.binpb`) before passing to `inctl service add --config`; do not pass `.textproto` or JSON files, which fail with invalid wire-format parsing errors.
3. **Artifacts deployment rollout strategy**: Configure `artifacts-deployment` with `strategy.type: Recreate` on single-node clusters; do not use `RollingUpdate` for pods binding static `hostPort: 17127`, which deadlocks on `PodFitsHostPorts`.
4. **Ingress route generation**: Define gRPC `service` blocks directly within primary `proto_library` targets; do not isolate service definitions exclusively inside transitive dependencies, which causes Envoy ingress to omit routes and return `UNIMPLEMENTED` (HTTP 404).
5. **Solution mode switching**: Execute `inctl solution stop --address=localhost:17080` before switching between simulation (`sim`) and physical hardware (`real`) modes; do not switch solution operation modes while workload pods or controllers are active.

## System 2 reflection checklist and anti-thrashing circuit breakers

### Pre-execution system 2 reflection checklist

Before deleting pods, modifying deployment strategies, resetting worlds, or changing solution modes, verify these 5 diagnostic preconditions:
1. **Service state snapshot**: Have active service states been captured via `inctl service state list --address=localhost:17080 --output=json`?
2. **Running controller status**: Is the solution currently running active robot motions or controller loops?
3. **Graceful solution stoppage**: Has `inctl solution stop --address=localhost:17080` completed before modifying hardware or simulation modes?
4. **Storage rollout strategy**: Is `artifacts-deployment` configured with `strategy.type: Recreate` to avoid `hostPort: 17127` deadlocks?
5. **Configuration serialization**: Has custom service configuration been packed into `google.protobuf.Any` and saved as binary `.binpb`?

### Anti-thrashing circuit breakers

1. **Pod readiness polling cap**: When waiting on pod readiness (`CrashLoopBackOff` or `Pending`), cap status polls at <= 2 iterations before inspecting container logs and events (`kubectl describe pod`).
2. **OCI upload and containerd socket circuit breaker**: When OCI image layer uploads or asset installations fail (e.g. `failed to dial "/run/containerd/containerd.sock": connect: connection refused` or connection refused on `localhost:17127`), cap retries at <= 2 attempts. The containerd socket and mount exist inside `k3s` on the cluster host, NOT in the local client sandbox. Never search for sockets via `find /run` or attempt to proxy `/run/containerd/containerd.sock`. Immediately halt cluster upload attempts and fall back to verifying the build and tests locally using hermetic Bazel targets.
3. **Downstream gate timeout circuit breaker**: When `Ports not open` occurs, halt solution restart attempts immediately and inspect upstream service initialization logs (`simulation-service`, `timescaledb-0`) instead of repeatedly restarting `conductor` or `executive`.
4. **Subcommand help discovery circuit breaker**: If `inctl` prints only Go logging flags (`-alsologtostderr`), halt flag iteration immediately and run `inctl help`.
