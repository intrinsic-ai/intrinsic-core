# Assets, catalog, solution deployment, storage, and build tooling reference

This document provides technical reference documentation, multi-RPC sequences, and API usage hints for communicating with Intrinsic assets versus platform services, managing asset catalogs and solution deployments, resolving dependencies, uploading storage artifacts, and using build and diagnostic tooling.

## Communicating with an `Intrinsic` asset versus an `Intrinsic Core` platform service

All external and inter-service gRPC traffic enters the workcell through the Envoy ingress gateway (`localhost:17080` externally or `istio-ingressgateway.app-ingress.svc.cluster.local:80` inside Kubernetes pods). Envoy routes requests using two distinct mechanisms depending on whether the target is an **Intrinsic asset instance** or an **Intrinsic Core platform service**:

| Dimension | Intrinsic asset (authored or sideloaded service/hardware instance) | Intrinsic Core platform service (singleton workcell service) |
| :--- | :--- | :--- |
| **Examples** | Custom gRPC microservices, camera drivers (`basler_camera`), gripper services, hardware modules (`ur_module`, `icon`). | `intrinsic_proto.world.ObjectWorldService`, `intrinsic_proto.motion_planning.v1.MotionPlannerService`, `intrinsic_proto.executive.ExecutiveService`, `intrinsic_proto.services.v1.SystemServiceState`, `intrinsic_proto.assets.v1.AssetInstances`, `intrinsic_proto.assets.v1.InstalledAssets`, `google.longrunning.Operations`. |
| **Multiplicity** | Multiple named instances can run concurrently from the same asset class (for example, `left_camera` and `right_camera`). | Exactly one singleton instance per workcell cluster. |
| **Envoy ingress routing key** | Requires the **`x-resource-instance-name: <instance_name>`** gRPC metadata header on every RPC call. | Routed directly by **gRPC URI path prefix** (`/intrinsic_proto.<pkg>.<Service>/<Method>`). Ingress `VirtualService` rules match when `x-resource-instance-name` is omitted (`withoutHeaders: { x-resource-instance-name: {} }`) or set to `exact: intrinsic_runtime`. |
| **Header requirement** | **Mandatory**: Without `x-resource-instance-name`, Envoy cannot identify which pod instance to route to and returns `UNAVAILABLE` or `UNIMPLEMENTED` (code 12, empty description `desc = ""`). | **Omit or use `intrinsic_runtime`**: Omit `x-resource-instance-name` headers (or set `exact: intrinsic_runtime`) when calling singleton platform services or polling long-running operations, as attaching a non-matching instance header causes Istio to reject the RPC with `UNIMPLEMENTED`. |
| **Standalone client connection** | Use `connection.ConnectionParams(address=..., instance_name="<name>", header="x-resource-instance-name")` with `HeaderAdderInterceptor` (or `intrinsic.assets.instances.connect.connect(grpc_connection)`). | Use a plain `grpc.insecure_channel(address)` and pass directly to the generated gRPC stub. |
| **Inside `Skill.execute()`** | Access pre-configured `ResourceHandle` via `context.resource_handles["<slot>"]` (or `intrinsic.assets.dependencies.utils.connect(resolved_dep, "grpc://<Service>")`) and extract `handle.connection_info.grpc`. | Access `context.object_world` (`ObjectWorldClient`) and `context.motion_planner` (`MotionPlannerClient`) directly. |
| **Inside `Skill.preview()`** | `PreviewContext` lacks `resource_handles` (hardware/asset instances are not leased during dry-run previews). Use `context.get_object_for_equipment("<slot>")` or `context.get_kinematic_object_for_equipment("<slot>")`. | `PreviewContext` treats `context.object_world` as read-only; record speculative changes via `context.record_world_update(update, elapsed, duration)`. |

### `API` usage hints for asset versus platform routing

- **Mandatory metadata injection for `GrpcConnection`**:
  `intrinsic_proto.assets.v1.GrpcConnection` encapsulates both `string address` (`"istio-ingressgateway.app-ingress.svc.cluster.local:80"`) and `repeated Metadata metadata` (`key: "x-resource-instance-name"`, `value: "<instance_name>"`). Dialing `GrpcConnection.address` with a raw `grpc.insecure_channel(connection.address)` without attaching `GrpcConnection.metadata` fails to route to the target instance.
- **SDK connection helpers**:
  - **Python**: Call `intrinsic.assets.instances.connect.connect(connection)` or `intrinsic.assets.dependencies.utils.connect(resolved_dep, iface_uri)`. These wrap the gRPC channel with a `HeaderAdderInterceptor` that injects all `(m.key, m.value)` pairs from `GrpcConnection.metadata` into every outgoing RPC. In standalone Python scripts outside Bazel, pass `metadata=[('x-resource-instance-name', '<instance_name>')]` directly to gRPC stub invocations.
  - **Go**: Call `connect.Connect(ctx, connection)` or `utils.Connect(ctx, resolved_dep, iface_uri)`, which returns both the `*grpc.ClientConn` and an updated `context.Context` with `metadata.AppendToOutgoingContext` applied.
- **Avoiding header leakage on long-running operations**:
  When polling an operation returned by an asset service call via `google.longrunning.Operations/GetOperation` or `WaitOperation`, ensure the client does not forward the service's `x-resource-instance-name` header to the operations proxy. The operations service is a platform singleton that expects `withoutHeaders: { x-resource-instance-name: {} }` or `x-resource-instance-name: intrinsic_runtime`. Forwarding an asset-specific instance name causes Istio to return status code 12 (`UNIMPLEMENTED`).
- **In-cluster HTTP REST gateway path rewriting (`/api/http-gateway/`)**:
  When calling HTTP REST endpoints (such as KVStore) through `istio-ingressgateway.app-ingress.svc.cluster.local:80` or `localhost:17080`, requesting the raw path `/api/kvstore/stores/<store>/keys/<key>` returns HTTP `404 Not Found` (empty body) from Envoy. The Istio `VirtualService` mounts the HTTP gateway under `/api/http-gateway/` and rewrites the URI to `/`. Always prefix HTTP REST gateway requests with `/api/http-gateway/` (for example, `http://istio-ingressgateway.app-ingress.svc.cluster.local:80/api/http-gateway/api/kvstore/stores/kv_store/keys/<key>`).

---

## Asset catalog and resource lifecycle (`InstalledAssets` and `AssetDeploymentService`)

A foundational architectural distinction on the platform is separating **installing an asset type** into a solution from **instantiating a runtime resource** within that solution:

1. **Asset installation (`intrinsic_proto.assets.v1.InstalledAssets`)**: Registers an asset definition (a versioned type or sideloaded bundle) in the solution's asset table (`intrinsic_proto.assets.v1.Solution.assets`). Skills (`ASSET_TYPE_SKILL`) and data assets (`ASSET_TYPE_DATA`) are used directly after installation and do not have runtime instances.
2. **Resource instantiation (`intrinsic_proto.assets.AssetDeploymentService`)**: Creates a named runtime instance from an installed asset type in `intrinsic_proto.assets.v1.Solution.instances`. Services (`ASSET_TYPE_SERVICE`), hardware devices (`ASSET_TYPE_HARDWARE_DEVICE`), and scene objects (`ASSET_TYPE_SCENE_OBJECT`) require explicit instantiation with an instance-specific `intrinsic_proto.assets.ResourceInstanceConfiguration`. Multiple instances can be spawned from a single installed asset type.

### `intrinsic_proto.assets.v1.InstalledAssets` and `intrinsic_proto.assets.v1.InstalledAssetsReader`

- `intrinsic_proto.assets.v1.InstalledAssets/CreateInstalledAsset`: Installs a single asset into the active solution. Returns a `google.longrunning.Operation` whose `response` unpacks to `intrinsic_proto.assets.v1.InstalledAsset` and whose `metadata` unpacks to `intrinsic_proto.assets.v1.CreateInstalledAssetMetadata`.
- `intrinsic_proto.assets.v1.InstalledAssets/CreateInstalledAssets`: Batch installs multiple assets in a single long-running operation (`intrinsic_proto.assets.v1.CreateInstalledAssetsResponse`).
- `intrinsic_proto.assets.v1.InstalledAssets/ListInstalledAssets`: Queries installed assets with pagination, filtering (`strict_filter.asset_types`, `strict_filter.provides`, `strict_filter.asset_tag`), sorting (`intrinsic_proto.assets.v1.OrderBy`), and view selection (`intrinsic_proto.catalog.AssetViewType`).
- `intrinsic_proto.assets.v1.InstalledAssets/GetInstalledAsset`: Retrieves a specific installed asset by `intrinsic_proto.assets.Id` (`package` and `name`).
- `intrinsic_proto.assets.v1.InstalledAssets/BatchGetInstalledAssets`: Retrieves multiple installed assets by their `intrinsic_proto.assets.Id` values.
- `intrinsic_proto.assets.v1.InstalledAssets/DeleteInstalledAsset` and `DeleteInstalledAssets`: Uninstalls one or more assets from the active solution.

#### `API` usage hints for `InstalledAssets`

- **Selecting the appropriate `AssetViewType`**:
  - `ASSET_VIEW_TYPE_BASIC`: Returns only `asset_type` and `id_version`.
  - `ASSET_VIEW_TYPE_DETAIL`: Adds `display_name`, `documentation`, `vendor`, `asset_tag`, `provides`, and asset-specific metadata (`ServiceMetadata`, `SkillMetadata`, `HardwareDeviceMetadata`, `DataMetadata`).
  - `ASSET_VIEW_TYPE_VERSIONS`: Returns metadata distinguishing asset versions (`asset_type`, `id_version`, `release_notes`, `update_time`, `vendor`, and release metadata).
  - `ASSET_VIEW_TYPE_ALL_METADATA`: Returns all metadata fields, including `file_descriptor_set` (`google.protobuf.FileDescriptorSet`). Use this view when dynamically constructing protobuf descriptor pools (`DescriptorPool`) to parse or serialize asset configurations and skill parameters.
  - `ASSET_VIEW_TYPE_FULL`: Returns all metadata and the full `deployment_data` (`DataDeploymentData`, `SceneObjectDeploymentData`, `ProcessDeploymentData`). Use this view only when inspecting or exporting raw scene object geometries, behavior tree protos, or data payloads, as it substantially increases message size.
- **Handling `UpdatePolicy` during installation**:
  - `UPDATE_POLICY_ADD_NEW_ONLY`: Rejects installation if any version of the asset is already installed. When installing via `UPDATE_POLICY_ADD_NEW_ONLY`, if the long-running operation completes with error code `ALREADY_EXISTS`, treat the asset as already present rather than a hard failure.
  - `UPDATE_POLICY_UPDATE_UNUSED`: Updates an existing asset only if no resource instances or processes currently reference it.
  - `UPDATE_POLICY_UPDATE_COMPATIBLE`: Updates an existing asset if the new version is backwards-compatible or currently unused.
- **Automatic derived fields when sideloading `ProcessAsset` definitions**:
  When installing a process asset via `CreateInstalledAssetRequest.Asset.process`, the server automatically updates `ProcessAsset.behavior_tree.description.id_version` and `ProcessAsset.behavior_tree.description.sideloaded`. This server-side mutation applies only to the `process` variant and does not occur when installing from `catalog` (`IdVersion`).
- **Clearing `OUTPUT_ONLY` metadata fields before releasing to `AssetCatalog`**:
  When fetching an installed asset via `intrinsic_proto.assets.v1.InstalledAssets/GetInstalledAsset` (`ASSET_VIEW_TYPE_FULL`) to package it into an `intrinsic_proto.catalog.AssetCatalog/CreateAsset` request (`CreateAssetRequest`), explicitly clear `Metadata.file_descriptor_set` (`installed_asset.metadata.ClearField("file_descriptor_set")`) before submitting `CreateAssetRequest`. Both `file_descriptor_set` and `provides` are `OUTPUT_ONLY` fields computed server-side.
- **Checking operation warnings**:
  Even when `CreateInstalledAsset` completes without an operation error, inspect `CreateInstalledAssetMetadata.warnings` (`intrinsic_proto.status.ExtendedStatus`) to detect non-fatal dependency validation notices.
- **Asset identifier naming rules and Kubernetes label mapping (`intrinsic_proto.assets.Id`)**:
  `Id.package` must be a lowercase dot-separated reverse domain string containing at least one period (for example, `ai.intrinsic`), starting with an alphabetic character, ending with an alphanumeric character, and containing no consecutive underscores (`__`). `Id.name` must be lowercase alphanumeric with single underscores. This naming restriction guarantees that asset IDs can be deterministically mapped to Kubernetes labels by replacing `_` with `-` and `.` with `--` without collisions.
- **Filtering by interface URI (`provides`)**:
  Pass exact interface URIs in `ListInstalledAssetsRequest.Filter.provides` (for example, `grpc://intrinsic_proto.motion_planning.MotionPlannerService` or `data://intrinsic_proto.perception.v1.PoseEstimationConfig`) to discover installed assets that expose specific gRPC services or data schemas. Inspect `Metadata.provides` rather than relying on `AssetTag` (`ASSET_TAG_CAMERA`, `ASSET_TAG_GRIPPER`) to programmatically determine whether an asset supports a particular gRPC API.

### `intrinsic_proto.assets.AssetDeploymentService`

- `intrinsic_proto.assets.AssetDeploymentService/CreateResourceFromCatalog`: Spawns a new named resource instance from an installed asset type (`type_id_version`) using `intrinsic_proto.assets.ResourceInstanceConfiguration`. Returns a `google.longrunning.Operation` unpacking to `intrinsic_proto.assets.CreateResourceFromCatalogResponse`. On failure, all workcell and world changes are automatically reverted.
- `intrinsic_proto.assets.AssetDeploymentService/UpdateResource`: Updates the runtime configuration (`intrinsic_proto.assets.ResourceConfiguration`) or backing `id_version` of an existing resource instance. Returns a `google.longrunning.Operation` unpacking to `intrinsic_proto.assets.Resource`.
- `intrinsic_proto.assets.AssetDeploymentService/DeleteResource`: Removes a resource instance and automatically deletes its corresponding world object and any attached child frames. Returns a `google.longrunning.Operation` unpacking to `google.protobuf.Empty`.

#### `API` usage hints for `AssetDeploymentService`

- **World attachment constraints**: In `intrinsic_proto.assets.ResourceInstanceConfiguration`, the `parent` (`intrinsic_proto.world.ObjectReferenceWithEntityFilter`) and `parent_t_this` (`intrinsic_proto.Pose`) fields can only be populated if the backing asset type defines a `world_fragment` (such as scene objects or physical hardware devices). Specifying `parent` or `parent_t_this` for a pure software service without a world fragment causes the RPC to fail.
- **Deprecated `world_id` field**: The `world_id` field on `CreateResourceFromCatalogRequest`, `UpdateResourceRequest`, and `DeleteResourceRequest` is deprecated. Always leave `world_id` empty to target the default active world; setting a non-empty value triggers validation errors.

---

## Asset instances, dependency resolution, and connection graphs

### `intrinsic_proto.assets.v1.AssetInstances` and `intrinsic_proto.assets.v1.AssetInstancesReader`

- `intrinsic_proto.assets.v1.AssetInstances/GetAssetInstance`
- `intrinsic_proto.assets.v1.AssetInstances/BatchGetAssetInstances`
- `intrinsic_proto.assets.v1.AssetInstances/ListAssetInstances`
*(Read-only counterparts are exposed on `intrinsic_proto.assets.v1.AssetInstancesReader`.)*

#### `API` usage hints for `AssetInstances`

- **Explicit view selection on list calls (`AssetInstanceView`)**:
  `ListAssetInstances` defaults to `ASSET_INSTANCE_VIEW_BASIC`, which strips `config`, `details`, and `metadata`. Server-side `strict_filters` (including `Filter.fulfills`) are evaluated before view stripping, so filtering by capability works with `ASSET_INSTANCE_VIEW_BASIC`, but `details.service.grpc_connection` is absent unless `view = ASSET_INSTANCE_VIEW_DETAIL` (or `ASSET_INSTANCE_VIEW_FULL`) is explicitly requested.
- **Empty filter behavior in `strict_filters`**:
  Because entries in `strict_filters` are combined with logical OR (`Filter_1 OR Filter_2`), appending an empty `Filter{}` (with no fields populated) evaluates `Filter_1 OR true = true`, disabling all filtering and returning every instance in the solution.
- **All-or-nothing batch semantics**:
  `BatchGetAssetInstances` returns `INVALID_ARGUMENT` if `names` is empty, and fails the entire RPC with `NOT_FOUND` if any single requested instance name does not exist in the solution.
- **Hardware device `InstanceConfig` variant**:
  A `HardwareDevice` asset instance populates `InstanceConfig.hardware_device` (which nests both `scene_object` and `service`), rather than `InstanceConfig.service` or `InstanceConfig.scene_object` directly. Check `AssetInstance.metadata.asset_type` or inspect the `oneof variant` before unpacking `service_config`.

### Dependency declarations (`intrinsic_proto.assets.v1.Dependency` and `intrinsic_proto.assets.v1.ResolvedDependency`)

Use `intrinsic_proto.assets.v1.ResolvedDependency` in a skill parameter message or a service configuration message whenever the skill or service communicates with another gRPC service, reads a data asset payload, or interacts with a world object provided by another asset instance. Annotate fields with `(intrinsic_proto.assets.field_metadata).dependency`:

```protobuf
message SkillOrServiceConfig {
  intrinsic_proto.assets.v1.ResolvedDependency camera = 1 [
    (intrinsic_proto.assets.field_metadata).dependency = {
      requires: "grpc://intrinsic_proto.perception.v1.CameraService",
      requires: "grpc://intrinsic_proto.gpio.v1.GPIOService",
      requires_object: {}
    }
  ];
}
```

#### `API` usage hints for `ResolvedDependency`

- **Configuration-time vs. runtime state of `ResolvedDependency`**:
  - **At authoring/configuration time**: Only `ResolvedDependency.name` is set (to the asset instance name for services/hardware devices/scene objects, or to the asset ID for data assets). The `interfaces` map and `object` field are empty.
  - **At runtime (inside `Skill.execute`, `Skill.preview`, `Skill.get_footprint`, or a running service container)**: The platform dependency resolver populates `interfaces` and `object`, and **clears `ResolvedDependency.name` to an empty string (`""`)**.
  - **Actionable rule**: Extract target connection information from `interfaces` or `object` rather than reading `resolved_dep.name` at runtime (as the platform dependency resolver clears `name` to `""` upon injection):
    - For gRPC interfaces: Read `resolved_dep.interfaces["grpc://<FQ_Service>"].grpc.connection` (or call `intrinsic.assets.dependencies.utils.connect(resolved_dep, "grpc://<FQ_Service>")`). To obtain the instance name for logging, inspect `connection.metadata` for key `"x-resource-instance-name"`.
    - For data interfaces: Read `resolved_dep.interfaces["data://<FQ_Message>"].data.id` and pass it to `intrinsic_proto.data.v1.DataAssets/GetDataAsset` (or call `intrinsic.assets.dependencies.utils.get_data_payload(resolved_dep, "data://<FQ_Message>")`).
    - For world objects: Read `resolved_dep.object.name` and query `ObjectWorldClient.get_object(resolved_dep.object.name)`.
- **Skill execution context vs. preview/footprint context (`always_provide_connection_info`)**:
  - During `Skill.preview()` and `Skill.get_footprint()`, the resolver populates all `data://` entries and `object.name`.
  - For `grpc://` entries, the `interfaces["grpc://..."]` map entry is present (`has_grpc() == true`), but by default (`SkillAnnotations.always_provide_connection_info = false`), the resolver clears `grpc.connection` when not running in `ExecuteContext` to prevent skills from actuating physical hardware during planning.
  - Calling `intrinsic.assets.dependencies.utils.connect(resolved_dep, "grpc://...")` during `preview()` without `always_provide_connection_info: true` raises `NotGRPCError("Interface is not gRPC or no connection information is available: ...")`. If a skill delegates `preview()` to `execute()` (for example, via `preview_via_execute`), set `skill_annotations: { always_provide_connection_info: true }` in the `(intrinsic_proto.assets.field_metadata).dependency` annotation.
- **Constraint cardinality and separation**:
  - A single `ResolvedDependency` field can require multiple `grpc://` URIs simultaneously and combine them with `requires_object: {}`.
  - A single `ResolvedDependency` field can require **at most one `data://` URI** and cannot mix `data://` with `grpc://` or `requires_object`. Specifying multiple `data://` URIs on a single `ResolvedDependency` field fails during recommendation resolution with `INTERNAL` (`"cannot have multiple data requirements"`).

### Automatic configuration recommendations (`intrinsic_proto.assets.v1.AssetConfigurationService`)

- `intrinsic_proto.assets.v1.AssetConfigurationService/RecommendAssetConfiguration`
- `intrinsic_proto.assets.v1.AssetConfigurationService/BatchRecommendAssetConfigurations`
- `intrinsic_proto.assets.v1.AssetConfigurationService/GetAssetRecommendationInfo`

#### `API` usage hints for `AssetConfigurationService`

- **Identifier convention depends on asset type**:
  - For **skills** (`ASSET_TYPE_SKILL`), pass the **asset ID** (for example, `"ai.intrinsic.move_robot"`) in `RecommendAssetConfigurationRequest.name`. Passing an instance name for a skill returns `INVALID_ARGUMENT`.
  - For **services** (`ASSET_TYPE_SERVICE`) and **hardware devices** (`ASSET_TYPE_HARDWARE_DEVICE`), pass the **instance name** in the solution (for example, `"robot_controller"`). Passing an asset ID for a service returns `INVALID_ARGUMENT`.
- **`GetAssetRecommendationInfo` vs. `RecommendAssetConfiguration` precondition discrepancy**:
  Calling `GetAssetRecommendationInfo` checks only whether `RecommendedConfigurationConfig.source` is `SOURCE_UNSPECIFIED` or `SOURCE_PLATFORM_DEFAULT`, returning `has_recommendation = true` even if a service or hardware device does not define a configuration message type in its manifest. However, calling `RecommendAssetConfiguration` on a service or hardware device without a configuration message type returns `FAILED_PRECONDITION` (`"<name> has no configuration message"`). Always handle `FAILED_PRECONDITION` when calling `RecommendAssetConfiguration`.
- **Unique dependency auto-resolution heuristics**:
  `RecommendAssetConfiguration` traverses all `ResolvedDependency` fields annotated with `(intrinsic_proto.assets.field_metadata).dependency`. If **exactly one** asset instance (for `grpc://` or `requires_object`) or data asset (for `data://`) in the solution satisfies all requirements, its identifier is written into `ResolvedDependency.name`. If zero or two or more candidates match, `ResolvedDependency.name` is left empty (`""`) without error.

### Composite asset connection graphs (`intrinsic_proto.assets.v1.AssetGraph`) and connected service introspection

- **Composite connection graphs (`intrinsic_proto.assets.v1.AssetGraph`)**:
  Composite asset manifests (such as `HardwareDeviceManifest`) define an internal directed acyclic graph (`nodes` and `edges`) specifying how bundled sub-assets wire together. An `intrinsic_proto.assets.v1.AssetEdge` with the `configures` relation connects a `Data` asset node to the target service or scene object node that it configures.
- **Introspecting services connected to a scene object (`intrinsic_proto.assets.v1alpha1.AssetInfoInternal/GetConnectedServices`)**:
  Use `intrinsic_proto.assets.v1alpha1.AssetInfoInternal/GetConnectedServices` to query all service instances connected to a specific physical scene object in the active solution, or `intrinsic_proto.assets.v1alpha1.AssetInfoInternal/CheckSolutionValidity` to verify that all dependency and connection graph constraints across the solution are satisfied.

### Solution deployment (`intrinsic_proto.assets.v1.SolutionDeploymentService`)

- `intrinsic_proto.assets.v1.SolutionDeploymentService/CreateSolutionDeploymentFromVersionedSolution`: Starts a solution deployment from a versioned `solution_id` in the specified `intrinsic_proto.config.OperationMode` (`OPERATION_MODE_SIM` or `OPERATION_MODE_REAL`).
- `intrinsic_proto.assets.v1.SolutionDeploymentService/GetSolutionDeployment`: Returns the active `SolutionDeployment` in basic view (without full asset manifests or object world updates).
- `intrinsic_proto.assets.v1.SolutionDeploymentService/UpdateSolutionDeployment`: Updates the active solution deployment.
- `intrinsic_proto.assets.v1.SolutionDeploymentService/DeleteSolutionDeployment`: Stops and tears down the active solution deployment (idempotent no-op if no solution is currently running).
- **Desynchronized solution state and LRO runner serialization**:
  Versioned solution deployment RPCs enqueue operations on an asynchronous runner queue, serializing state transitions safely. If an error occurs during runtime database population or chart application, cluster metadata can remain marked as active. Run `inctl solution stop --address=localhost:17080` to clear local orchestrator state before re-deploying.
- **Namespace termination race on redeployment (`0/0 pods ready` false positive)**:
  When redeploying a solution, previous `ChartAssignment` namespaces enter asynchronous Kubernetes `Terminating` state. Because readiness controllers evaluate `readyPods == totalPods`, a terminating deployment with `0/0` pods evaluates as `Ready: True`, which can cause `conductor` to start execution prematurely. Verify that terminating pods have completely cleared (`kubectl get pods -A`) before triggering a redeploy.
- **Switching between simulation and real hardware modes**:
  Always execute `inctl solution stop --address=localhost:17080` before switching a solution between simulation (`sim`) and hardware (`real`) operation modes. Switching modes without stopping leaves real-time control pods (`icon`, hardware modules) deadlocked on StatefulSet volume leases.

---

## Artifact catalog (`ArtifactCatalogService`) and storage `API`s

| Fully qualified gRPC service | Primary role | When to use |
| :--- | :--- | :--- |
| `intrinsic_proto.storage.artifacts.v1.ArtifactServiceApi` | Direct-injection container image, tarball, and blob upload service | Use when sideloading container images or tarballs directly into a workcell's `containerd` store (`localhost:17127` or `localhost:17080`) without an external OCI registry. |
| `intrinsic_proto.storage.artifacts.v1.ArtifactCatalogService` | Service routing alias for catalog artifact uploads | Use as a gRPC method-path rewrite target (`/intrinsic_proto.storage.artifacts.v1.ArtifactCatalogService/<Method>`) when uploading release container images to the catalog OCI registry proxy (ArtiCat). |
| `intrinsic_proto.assets.v1.AssetArtifacts` | Stateful chunked upload and server-side processing of binary asset files | Use when uploading CAD geometry files, GZF bundles, or large data payloads (`> 1 MiB`) referenced via `intrinsic_proto.data.v1.ReferencedData` (`StartUpload`, `UploadChunk`, `FinalizeUpload`, `Process`). |
| `intrinsic_proto.content_addressable_storage.v1.ContentAddressableStorageService` | Immutable blob storage keyed by content digest | Use when storing or fetching arbitrary immutable data blobs (`Get`, `Create`, `Delete`, `Stat`, `GetRange`) by digest across cluster storage. |
| `intrinsic_proto.hot_shared_state.v1.HotSharedStateClusterService` | Low-latency cluster configuration state store | Use when reading or updating the active cluster configuration proto (`GetCurrentCluster`, `SetCurrentCluster`) with optimistic concurrency control. |
| `intrinsic_proto.onprem_cas_preload.v1.OnpremCASPreloadService` | Pre-caching CAS blobs onto local workcell disk | Use when atomically preloading sets of CAS object IDs onto an on-prem cluster prior to execution (`UpsertPreloadSet`). |

### Multi-`RPC` sequence for container image upload (`ArtifactServiceApi` / `ArtifactCatalogService`)

1. **Direct upload endpoint and pod scheduling architecture**:
   Direct asset uploads push OCI layers over HTTP to `localhost:17127`, which maps to container port `9090` on the `artifacts-deployment` pod via a static Kubernetes `hostPort: 17127`. Because `artifacts-deployment` binds a static hostPort, ensure the pod deployment uses `strategy.type: Recreate` on single-node clusters so replacement pods avoid `PodFitsHostPorts` scheduling deadlocks.
2. **Pre-flight check (`intrinsic_proto.storage.artifacts.v1.ArtifactServiceApi/CheckImage`)**:
   - Pass target image `name` and raw manifest bytes in `ImageManifest.data`.
   - Inspect `ArtifactResponse.max_update_size` (default to `4194304` bytes / 4 MB if `<= 0`) and `missing_refs` / `present_refs`.
3. **Session isolation header setup**:
   - Attach a unique `intrinsic-client-id` header (`<client_uuid>/<per_image_uuid>`) to outgoing gRPC metadata before calling `WriteContent` or `UploadContent`. Because server-side upload sessions are keyed by `<client_id>:<ref>`, omitting a distinct `intrinsic-client-id` causes concurrent image uploads sharing common base layers to collide with `codes.Aborted: repeated update request` or `updater already finalized`.
4. **Concurrent layer and config blob upload (`WriteContent`)**:
   - Prefer sequential unary calls to `WriteContent` over streaming `UploadContent` when traversing ingress or relay proxies that buffer or time out long-lived gRPC client streams.
   - Send an exploratory 100-byte first chunk (`UPDATE_ACTION_UPDATE`) with `expected_digest`, monotonic `chunk_id`, and `content.annotations["intrinsic.ai/content.image.name"] = "<registry>/<repo>:<tag>"`. If the server returns `AlreadyExists`, treat the blob as already present and skip remaining chunks.
   - **Mandatory annotation**: Omitting `"intrinsic.ai/content.image.name"` causes catalog uploads (`intrinsic_proto.storage.artifacts.v1.ArtifactCatalogService`) to fail immediately with `missing "intrinsic.ai/content.image.name" annotation`.
   - Stream remaining chunks up to `max_update_size` and conclude with `UPDATE_ACTION_COMMIT` (or `UPDATE_ACTION_ABORT` on cancellation to release the 4-hour `containerd` ingest lease).
   - If an upload stream is interrupted, the server deletes the active session finalizer. A client retrying from chunk 2+ against a recreated finalizer is rejected with `manifest content details not provided`. In case of interruption, restart the upload cleanly from chunk 0.
5. **Triple-indexed manifest commit**:
   - After all layers and config blobs succeed, upload the raw single-platform OCI manifest (`application/vnd.oci.image.manifest.v1+json`; multi-arch image indexes are rejected by OCI registry backends) sequentially under **three distinct `Ref` strings**:
     1. Primary tagged reference: `<registry>/<name>:<tag>` (e.g., `localhost:17127/my_service:latest`)
     2. Raw digest reference: `sha256:<manifest_digest>`
     3. Digest-qualified repository reference: `<registry>/<name>@sha256:<manifest_digest>`
6. **Downstream asset registration**:
   - Reference the uploaded image in `intrinsic_proto.kubernetes.workcell_spec.Image` with `registry = "localhost:17127"`, `name = "<image_name>"`, and `tag = "@sha256:<manifest_digest>"` (including the leading `@`).

### `API` usage hints for `AssetArtifacts`

- **Inlining threshold for small artifacts**: Files smaller than or equal to 1 MiB (`1048576` bytes) should be inlined directly into `intrinsic_proto.data.v1.ReferencedData.inlined` with an empty `digest` field rather than uploaded via `StartUpload` / `UploadChunk`.
- **Session timeout and sequential offsets**: `StartUpload` sessions expire after 60 seconds of inactivity, discarding temporary chunks. `UploadChunkRequest.offset` must strictly equal the total number of bytes acknowledged so far (`ack_offset`).

---

## Build and diagnostic tooling (`inbuild` and `inctl doctor`)

### `inctl doctor` (`intrinsic_proto.inctl.doctor.v1.Report`)

`inctl doctor` is an enterprise-level only offering.

- **Recommended local workcell inspection sequence (authoritative replacement for `inctl doctor`)**:
  1. `inctl service state list --address=localhost:17080 --output=json`
     - Invokes `intrinsic_proto.services.v1.SystemServiceState/ListInstanceStates` to inspect `STATE_CODE_*` and `ExtendedStatus` across all running service instances.
  2. `inctl asset instance list --address=localhost:17080`
     - Lists all active asset instances (services, hardware modules, scene objects) in the running solution, mapping instance names to asset package IDs.
  3. `inctl icon status --instance_name=icon --address=localhost:17080`
     - Invokes `intrinsic_proto.icon.v1.IconApi/GetOperationalStatus` and `GetStatus` with header `x-resource-instance-name: icon` to inspect real-time control status and safety state. **Note**: Always pass `--instance_name=icon`; omitting this flag causes the ingress to return `code = Unimplemented desc = `.
  4. `inctl world reset --address=localhost:17080`
     - Resets the live belief world scene graph state to baseline (`init_world`). This replaces `inctl sim reset` on local workcells.
- **CLI addressing flag split (`--address` vs `--server`) and transport credentials**:
  - **Commands using `--address=localhost:17080`**: `inctl asset`, `inctl service`, `inctl skill`, `inctl icon`, `inctl solution start`, `inctl solution stop`, and `inctl world`.
  - **Commands using `--server=localhost:17080`**: `inctl process get`, `inctl process set`.
- **Service configuration wire format (`inctl service add --config`)**:
  When adding a service instance via `inctl service add <asset_id> --name=<instance_name> --config=<path> --address=localhost:17080`, the `--config` flag requires a binary-serialized `google.protobuf.Any` protobuf file (`.binpb`). Passing a textproto (`.textproto`) file fails immediately with `Error: could not unmarshal configuration proto: proto: cannot parse invalid wire-format data`.
- **Top-level CLI help discovery (`inctl help` vs `inctl --help`)**:
  Running `inctl --help` is intercepted by Go's standard library `flag` parser and prints only global logging flags (`-alsologtostderr`, `-v`, `-log_dir`). Always run `inctl help` (without `--`) or `inctl <subcommand> --help` to view the Cobra subcommand tree.

### Packaging and deploying assets with `inbuild`

- **Service bundles (`inbuild service bundle`)**:
  - Requires `--manifest` (`intrinsic_proto.services.ServiceManifest` textproto) and `--oci_image` (at least one and at most two OCI tarballs for real and simulation specs; image basenames must be unique). If `--default_config` is passed, at least one `--file_descriptor_set` binary proto must be provided.
  - At container startup, the platform injects a serialized `intrinsic_proto.config.RuntimeContext` binary proto at `/etc/intrinsic/runtime_config.pb`. The service reads `/etc/intrinsic/runtime_config.pb`, parses `RuntimeContext`, and unpacks `context.config` (`google.protobuf.Any`).
- **4-stage skill build pipeline (`inbuild skill`)**:
  1. **Consolidate manifest and descriptors (`inbuild skill manifest`)**: Consolidates `--manifest` (`intrinsic_proto.skills.SkillManifest`) and transitive `--file_descriptor_sets` into a binary manifest and descriptor set.
  2. **Generate entrypoint and augment descriptors (`inbuild skill generate entrypoint`)**: Generates `main.py` or `main.cc`, merges platform skill service descriptors (`INTRINSIC_PROTO_SKILLS_PROJECTOR`, `INTRINSIC_PROTO_SKILLS_EXECUTOR`, `INTRINSIC_PROTO_SKILLS_SKILL_INFORMATION`), and prunes source code info.
  3. **Generate runtime skill service config (`inbuild skill generate config`)**: Produces a serialized binary skill service config. Inside the skill OCI image, the compiled entrypoint binary must be symlinked to `/skills/skill_service` and the serialized binary skill service config to `/skills/skill_service_config`.
  4. **Package final skill bundle (`inbuild skill bundle`)**: Validates message types against the `FileDescriptorSet` registry and outputs the `.bundle.tar` archive.
- **Data assets (`inbuild data bundle`) and HTTP/JSON bridges (`inbuild httpjson generatemain`)**:
  - `inbuild data bundle` automatically merges built-in descriptors (`ReferencedDataStruct`, `DataManifest`) and resolves payload file paths relative to the manifest directory.
  - `inbuild httpjson generatemain` generates an HTTP-to-gRPC reverse proxy bridge using repeatable `--http_service "<service_fqn>:<go_import_path>"` mappings and an OpenAPI spec (`--openapi_path`).

---

## Diagnostic decision trees and `gRPC` status taxonomy

### Decision tree 1: ingress routing and `gRPC` status failure triage

- **Status code 12 (`UNIMPLEMENTED`) with empty description (`desc = ""`)**:
  - Service/hardware asset (`rs-<name>`, `icon`, `ur_module`): Attach metadata `('x-resource-instance-name', '<instance_name>')` to RPC call (or pass `--instance_name=<name>` to CLI).
  - Core platform service (`ObjectWorldService`, `Operations`, `InstalledAssets`): Omit `x-resource-instance-name` header (or set `exact: 'intrinsic_runtime'`). Omit asset instance headers when polling operations proxies.
- **Status code 12 (`UNIMPLEMENTED`) with HTTP 404 in description**:
  - HTTP REST gateway path: Prepend `/api/http-gateway/` to URI (e.g., `/api/http-gateway/api/kvstore/...`).
  - Deprecated or unregistered gRPC service: Inspect proto package name and verify `VirtualService` registration via `kubectl get vs -A`.
- **Status code 14 (`UNAVAILABLE`) with "no healthy upstream" or "Connection refused"**:
  - Single service failing: Run `kubectl describe pod <pod-name> -n app-intrinsic-app-chart`. Inspect all containers (including sidecars) for SIGSEGV or exit code 128.
  - Multiple services failing simultaneously: Check host CPU Pressure Stall Information (PSI) and runnable queues (`vmstat r`). CFS quota throttling on hybrid Intel cores stalls istiod and apiserver xDS streams.
- **Status code 8 (`RESOURCE_EXHAUSTED`) with "received message larger than max"**: Default client channel buffer is capped at 4 MB. Configure explicit 64 MB options (`grpc.max_receive_message_length = 64 * 1024 * 1024`).

### Decision tree 2: `CLI` command addressing and flag selection

- **Subcommands `asset`, `service`, `skill`, `icon`, `solution start/status/stop`, `world`**: Use `--address=localhost:17080`. For `icon` (status, enable, disable, clear-faults), always append `--instance_name=icon`. For `service add`, ensure `--config` points to a binary `.binpb` file (`google.protobuf.Any`).
- **Subcommands `process` (`get`, `set`)**: Use `--server=localhost:17080` (credentials auto-select insecure transport).
- **Subcommands `executive` (`run`, `state`)**: Use `--server=localhost:17080`. For executive commands, append `--insecure_grpc` to prevent TLS handshake errors.
- **Subcommand `doctor` (`check`)**: Execute the 4-step local inspection sequence on standalone workcells (`service state list`, `asset instance list`, `icon status`, `world reset`) rather than `inctl doctor`, which only checks cloud authentication and cloud control-plane cluster/solution state (`inctl auth list`, `inctl cluster list`, `inctl solution get`).

### Decision tree 3: `OCI` direct upload and container image scheduling

- **Pre-flight `CheckImage` returns `missing_refs`**: Send 100-byte probe chunk with `UPDATE_ACTION_UPDATE` and `annotations["intrinsic.ai/content.image.name"]`.
- **Upload aborts with `codes.Aborted: repeated update request` or `updater already finalized`**: Concurrent session collision. Attach distinct `intrinsic-client-id: <client_uuid>/<per_image_uuid>` gRPC metadata header to each upload stream.
- **Stream interrupted and retry fails with "manifest content details not provided"**: Server deleted upload finalizer upon disconnect. Restart layer upload cleanly from chunk 0 rather than resuming from chunk 2+.
- **Image rejected with `InvalidImageName` ("repository name must not be more than 255 characters")**: Repeated sideloading accumulated `sideloaded__` prefixes. Re-install asset bundle cleanly to reset repository naming prefixes.
- **Pod fails to schedule with `PodFitsHostPorts`**: `artifacts-deployment` binds static `hostPort: 17127`. Ensure deployment spec uses `strategy.type: Recreate` so old pod terminates before new pod starts.
- **Upload fails with `failed to dial "/run/containerd/containerd.sock": connect: connection refused` or port 17127 connection refused**: Backend cluster infrastructure failure. The `artifacts-deployment` pod in `k3s` cannot communicate with the host containerd daemon. The containerd socket and its mount belong inside `k3s` on the cluster host, NOT in the agent's local client execution environment (e.g. Bubblewrap sandbox). Stop after at most 2 attempts. Do NOT attempt to debug local `/run` sockets, search with `find /run`, or attempt to proxy `/run/containerd/containerd.sock`. Halt cluster upload attempts and fall back to verifying the build and tests locally via hermetic Bazel commands (`bazel build //...`, `bazel test //...`).

## Paired guardrails and execution invariants

To ensure deterministic platform stability, adhere to these five paired operational guardrails:

| Rule | Operational domain | Mandatory affirmative action | Paired negative constraint |
| :--- | :--- | :--- | :--- |
| **1** | **Envoy ingress routing** | Attach the `x-resource-instance-name: <instance_name>` metadata header when dialing named asset instances; omit the header (or set `exact: intrinsic_runtime`) when dialing core platform singletons and LRO proxies. | Do not attach `x-resource-instance-name` when invoking singleton platform services or polling long-running operations. |
| **2** | **CLI target addressing** | Use `--address=localhost:17080` for asset, service, skill, icon, and world commands; use `--server=localhost:17080` for process, sim, and executive commands. | Do not pass `--address` to `inctl process` or pass `--server` to `inctl service` or `inctl icon`. |
| **3** | **Service configuration wire format** | Serialize configuration protobuf messages inside `google.protobuf.Any` to binary wire-format files (`.binpb`) before passing to `inctl service add --config`. | Do not pass textproto (`.textproto`) files to `inctl service add --config`. |
| **4** | **Asset teardown ordering** | Delete all active resource instances via `AssetDeploymentService/DeleteResource` before uninstalling the asset definition via `InstalledAssets/DeleteInstalledAsset`. | Do not attempt to delete an installed asset definition while runtime resource instances of that asset are active in the solution. |
| **5** | **Solution mode switching** | Execute `inctl solution stop --address=localhost:17080` before switching between simulation (`sim`) and physical hardware (`real`) operation modes. | Do not change the solution execution mode while workload pods or controllers are actively running. |

## System 2 reflection checkpoints and circuit breakers

### System 2 reflection checkpoint 1: pre-execution and service instantiation

Before invoking `CreateResourceFromCatalog`, `UpdateResource`, or instantiating services:
1. **Target instance verification**: Confirm that the asset type is installed via `InstalledAssets/GetInstalledAsset` and verify that the proposed instance name is unique in `AssetInstances/ListAssetInstances`.
2. **World attachment check**: If populating `parent` or `parent_t_this` in `ResourceInstanceConfiguration`, verify that the asset type defines a `world_fragment`. Leave `world_id` empty.
3. **Descriptor pool validation**: If parsing dynamic `google.protobuf.Any` configurations, ensure the descriptor set was fetched using `ASSET_VIEW_TYPE_ALL_METADATA` and loaded into a descriptor pool.
4. **Ingress metadata check**: Confirm that outgoing client channels specify `x-resource-instance-name: <instance_name>`.

### System 2 reflection checkpoint 2: pre-mutation teardown and mode switching

Before deleting assets, resetting worlds, or stopping/starting solutions:
1. **Dependency impact review**: Query `AssetInfoInternal/GetConnectedServices` and `AssetInstances/ListAssetInstances` to ensure no active processes or services depend on the instance being deleted.
2. **Teardown ordering check**: Verify that all resource instances are deleted via `AssetDeploymentService/DeleteResource` and their LROs are complete before attempting `InstalledAssets/DeleteInstalledAsset`.
3. **Mode transition check**: Confirm that `inctl solution stop --address=localhost:17080` has executed and all workload pods have terminated before starting a solution in a different operation mode.

### Anti-thrashing circuit breakers

- **Ingress routing circuit breaker**: If a gRPC call returns status code 12 (`UNIMPLEMENTED`) >= 2 times, stop modifying application logic. Verify that the `x-resource-instance-name` header matches the exact instance name for assets, or is omitted for platform singletons. Check `kubectl get virtualservice -A`.
- **OCI layer upload circuit breaker**: If layer upload aborts with `codes.Aborted: repeated update request` or `manifest content details not provided` >= 2 times, halt intermediate chunk retries. Generate a fresh client UUID (`intrinsic-client-id: <uuid>/<uuid>`) and restart the upload cleanly from chunk 0.
- **Pod scheduling circuit breaker**: If a deployed pod remains in `Pending` or `CrashLoopBackOff` across 2 consecutive deployments, halt solution restarts. Run `kubectl describe pod` to check for `Insufficient nvidia.com/gpu`, `PodFitsHostPorts`, or missing `gazebo_simulator` asset dependencies.
- **Backend containerd socket and OCI upload circuit breaker**: When `inctl asset install` or sideloading fails with `failed to dial "/run/containerd/containerd.sock": connect: connection refused` or connection refused on `localhost:17127`, cap retries at <= 2 attempts. The failure is on the backend server side (the containerd socket and its mount belong inside `k3s` on the cluster host, NOT in the agent's local client execution environment, e.g. Bubblewrap sandbox). Do NOT attempt to debug local `/run` sockets, search for sockets with `find /run`, or attempt to proxy `/run/containerd/containerd.sock`. Immediately halt cluster upload attempts and fall back to verifying hermetic Bazel build and test targets locally (`bazel build //...`, `bazel test //...`).

## Multi-`RPC` sequences

### Sequence 1: discovering a service instance and invoking its `gRPC` `API`

1. **Discover fulfilling instances**:
   Call `intrinsic_proto.assets.v1.AssetInstances/ListAssetInstances` with `view = ASSET_INSTANCE_VIEW_DETAIL` and `strict_filters = [{ fulfills: { requires: ["grpc://<FQ_ServiceName>"] } }]`. Ensure each entry in `strict_filters` contains populated criteria (as an empty `Filter{}` evaluates to true and matches all instances).
2. **Extract `GrpcConnection`**:
   Read `instance.details.service.grpc_connection` from the returned `AssetInstance`.
3. **Establish an intercepted channel**:
   Pass `grpc_connection` to `intrinsic.assets.instances.connect.connect(grpc_connection)` (or attach `metadata=[('x-resource-instance-name', instance_name)]`) so that `x-resource-instance-name: <name>` is injected into every outgoing RPC.

### Sequence 2: sideloading a local asset with large artifacts (> 1 `MiB`) and instantiating a service

1. **Upload and process large artifacts (> 1 MiB)**:
   - Call `intrinsic_proto.assets.v1.AssetArtifacts/StartUpload` to obtain an `upload_id`.
   - Loop over the artifact bytes and invoke `intrinsic_proto.assets.v1.AssetArtifacts/UploadChunk` with sequential `offset` values (`offset == ack_offset`).
   - Call `intrinsic_proto.assets.v1.AssetArtifacts/FinalizeUpload` with `upload_id` and optional `sha512:<hex>` digest to receive an `intrinsic_proto.data.v1.ReferencedData`.
   - Call `intrinsic_proto.assets.v1.AssetArtifacts/Process` passing `ReferencedData`, poll `google.longrunning.Operations/WaitOperation` until `done == true`, and replace the local file reference in the asset manifest with `ProcessResponse.referenced_data`.
2. **Install the asset definition**:
   - Call `intrinsic_proto.assets.v1.InstalledAssets/CreateInstalledAsset` with the processed manifest and `UpdatePolicy.UPDATE_POLICY_UPDATE_COMPATIBLE`, then poll `google.longrunning.Operations/WaitOperation`.
3. **Generate recommended configuration (optional)**:
   - Call `intrinsic_proto.assets.v1.AssetConfigurationService/RecommendAssetConfiguration` with the target instance name to auto-populate unambiguous `ResolvedDependency` fields.
4. **Instantiate the runtime service**:
   - Call `intrinsic_proto.assets.AssetDeploymentService/CreateResourceFromCatalog` with `type_id_version` (`<package>.<name>.<version>`) and `ResourceInstanceConfiguration`, then poll `google.longrunning.Operations/WaitOperation`.

### Sequence 3: safe teardown and uninstallation of a service asset

Because `intrinsic_proto.assets.v1.InstalledAssets/DeleteInstalledAsset` with `DeletePolicy.POLICY_REJECT_USED` rejects uninstallation if any resource instances of that asset type exist, teardown must proceed from instances to asset types:

1. **Discover active instances**:
   Call `intrinsic_proto.assets.v1.AssetInstances/ListAssetInstances` with `Filter.id` set to the target `intrinsic_proto.assets.Id` (`package` and `name`).
2. **Delete each active resource instance**:
   For each returned instance, call `intrinsic_proto.assets.AssetDeploymentService/DeleteResource` with `DeleteResourceRequest.name` set to the instance name and poll `google.longrunning.Operations/WaitOperation` until completion.
3. **Uninstall the asset type**:
   Call `intrinsic_proto.assets.v1.InstalledAssets/DeleteInstalledAsset` with `DeleteInstalledAssetRequest.asset` set to the target `intrinsic_proto.assets.Id` and poll `google.longrunning.Operations/WaitOperation` until `done == true`.

### Sequence 4: inspecting dynamic protobuf configurations of installed assets

When a client or tool needs to parse or construct `google.protobuf.Any` configurations for installed services or skills whose protobuf definitions are not compiled into the client binary:

1. **Fetch installed assets with descriptor sets**:
   Call `intrinsic_proto.assets.v1.InstalledAssets/ListInstalledAssets` with `view` set to `intrinsic_proto.catalog.AssetViewType.ASSET_VIEW_TYPE_ALL_METADATA`.
2. **Build a local descriptor pool**:
   Extract `InstalledAsset.metadata.file_descriptor_set` (`google.protobuf.FileDescriptorSet`) for each asset and register the file descriptors in a dynamic protobuf descriptor pool (`DescriptorPool`).
3. **Deserialize or serialize instance configurations**:
   Use the populated descriptor pool to unpack `ResourceInstanceConfiguration.configuration` (`google.protobuf.Any`) or parse textproto configuration files into strongly typed dynamic messages before invoking `CreateResourceFromCatalog` or `UpdateResource`.

### Sequence 5: installing a skill asset and executing a behavior tree via `ExecutiveService`

1. **Install the skill bundle (`inctl skill install <skill.bundle.tar> --address=localhost:17080`)**:
   Call `intrinsic_proto.assets.v1.InstalledAssets/CreateInstalledAsset` and poll `google.longrunning.Operations/WaitOperation` until `done == true`.
2. **Reset world state prior to execution (`inctl world reset --address=localhost:17080`)**:
   Note that `/usr/local/bin/inctl` (`inctl_external`) omits `sim` and `executive` subcommands; reset the world via `inctl world reset --address=localhost:17080` or call `intrinsic_proto.simulation.v1.SimulationService/ResetSimulation` over gRPC, retrying with exponential backoff on `codes.Unavailable` while ingress routes stabilize.
3. **Load and execute the behavior tree via gRPC (`ExecutiveService`)**:
   Note that `inctl process set --server=localhost:17080` only calls `ListOperations`, `DeleteOperation`, and `CreateOperation` (it never calls `StartOperation`), and `inctl process get --server=localhost:17080` only returns the static `BehaviorTree` proto definition rather than runtime execution state. To execute and monitor a behavior tree:
   - Call `intrinsic_proto.executive.ExecutiveService/ListOperations`.
   - If an existing operation is present (`len(operations) == 1`), call `intrinsic_proto.executive.ExecutiveService/DeleteOperation`.
   - Call `intrinsic_proto.executive.ExecutiveService/CreateOperation` with `runnable_type = BehaviorTree`.
   - Call `intrinsic_proto.executive.ExecutiveService/StartOperation` with `name = operation.name` and `simulation_mode = SIMULATION_MODE_REALITY` or `SIMULATION_MODE_DRAFT`.
4. **Monitor execution state via gRPC (`ExecutiveService/ListOperations`)**:
   Call `intrinsic_proto.executive.ExecutiveService/ListOperations` and inspect `operations[0].done` and `operations[0].metadata` (`intrinsic_proto.executive.RunMetadata.operation_state`).
