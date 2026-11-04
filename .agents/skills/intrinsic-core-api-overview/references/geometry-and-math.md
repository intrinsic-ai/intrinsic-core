# Geometry service, shape representations, and math reference

This reference covers `intrinsic_proto.geometry.GeometryService` RPCs, content-addressed storage references, oriented bounding boxes (OBBs), triangle meshes, spatial octrees, analytical v1 primitive shape representations, spatial math primitives (`Pose3`, `Rotation3`, `Pose`, `Quaternion`, `Transform`), and multi-RPC geometry processing pipelines.

**Contents**:
- [Core `gRPC` services and `RPC`s](#core-grpc-services-and-rpcs)
- [Core geometry and spatial math message decision matrix](#core-geometry-and-spatial-math-message-decision-matrix)
- [Input-aware decision trees for geometry and spatial math](#input-aware-decision-trees-for-geometry-and-spatial-math)
- [Spatial math primitives (`Pose3`, `Rotation3`, `Pose`, `Quaternion`, `Transform`)](#spatial-math-primitives-pose3-rotation3-pose-quaternion-transform)
- [System 2 reflection and circuit breaker for geometry and math](#system-2-reflection-and-circuit-breaker-for-geometry-and-math)
- [Core paired safety guardrails](#core-paired-safety-guardrails)
- [Non-obvious `API` usage hints for `GeometryService` and shape protos](#non-obvious-api-usage-hints-for-geometryservice-and-shape-protos)
- [Multi-`RPC` geometry processing sequences](#multi-rpc-geometry-processing-sequences)

## Core `gRPC` services and `RPC`s

| Fully qualified gRPC method | Request / response types | When and why to use |
| :--- | :--- | :--- |
| `intrinsic_proto.geometry.GeometryService/CreateGeometry` | `intrinsic_proto.geometry.CreateGeometryRequest` -> `intrinsic_proto.geometry.GeometryWithMetadata` | Register new geometry data (`inline_geometry`, `primitive_set`, `obj_data`, `gltf_bytes`, or `stl_bytes`) in content-addressed storage (CAS) and return opaque v1 storage references (`response.geometry_storage_refs`). Call before creating or updating a world object with custom collision or visual geometry. |
| `intrinsic_proto.geometry.GeometryService/GetGeometry` | `intrinsic_proto.geometry.GetGeometryRequest` -> `intrinsic_proto.geometry.GeometryWithMetadata` | Resolve an opaque `intrinsic_proto.geometry.v1.GeometryStorageRefs` into full computational geometry (`inline_geometry.exact_geometry`), including triangle mesh vertices, primitive shape sets, and point clouds. |
| `intrinsic_proto.geometry.GeometryService/GetRenderable` | `intrinsic_proto.geometry.GetRenderableRequest` -> `intrinsic_proto.geometry.RenderableWithMetadata` | Retrieve binary GLB/GLTF rendering payloads (`renderable.glb_bytes`) for visualization clients at a specified level of detail (`lod_level`). If a pre-generated renderable is not cached in storage, the service dynamically synthesizes a GLB mesh from `exact_geometry_ref` on demand. |
| `intrinsic_proto.geometry.GeometryService/ProcessGeometry` | `intrinsic_proto.geometry.ProcessGeometryRequest` -> `google.longrunning.Operation` | Execute asynchronous geometry processing pipelines (such as alpha wrapping, convex hull generation, mesh simplification, voxel remeshing, uniform scaling, or CoACD convex decomposition). Unpacks operation metadata to `intrinsic_proto.geometry.GeometryProcessingStatus` and final response to `intrinsic_proto.geometry.GeometryProcessingResult`. |
| `google.longrunning.Operations/GetOperation` | `google.longrunning.GetOperationRequest` -> `google.longrunning.Operation` | Poll the status and completion result of an asynchronous `ProcessGeometry` operation (`"geometry/ProcessGeometry/<fingerprint>/<nanos>"`). |

## Core geometry and spatial math message decision matrix

| Fully qualified message name | Primary fields | When to use | Why this representation is used |
| :--- | :--- | :--- | :--- |
| `intrinsic_proto.Pose` | `position` (`intrinsic_proto.Point`), `orientation` (`intrinsic_proto.Quaternion`) | Representing 6-DOF rigid body poses in scene graphs (`intrinsic_proto.world.ObjectWorldService`), IK/trajectory targets (`intrinsic_proto.motion_planning.v1.MotionPlannerService`), perception estimates, and skill parameters. | Standard 6-DOF pose representation across all Intrinsic gRPC services and Executive skill interfaces. Semantically separates a 3D point location (`Point`) from rotational orientation (`Quaternion`). |
| `intrinsic_proto.Quaternion` | `x`, `y`, `z`, `w` (`double`) | Representing 3D rotations inside `Pose`, `Transform`, and kinematic configurations without gimbal lock. | Uses Hamiltonian `[x, y, z, w]` convention (`x*i + y*j + z*k + w`). Must maintain unit norm (sqrt(x^2 + y^2 + z^2 + w^2) == 1.0) for valid rigid-body rotations. |
| `intrinsic_proto.Transform` | `translation` (`intrinsic_proto.Vector3`), `rotation` (`intrinsic_proto.Quaternion`) | ROS 2 / TF2 bridge messages (`geometry_msgs/msg/Transform`), sensor driver frame offsets, and `TransformStamped` payloads. | Represents a relative coordinate frame displacement vector (`Vector3` translation rather than an absolute `Point`). In skill parameters and executive workflows, use `intrinsic_proto.Pose` instead. |
| `intrinsic_proto.geometry.v1.Geometry` | `oneof data` (`geo_ref`, `inline_geometry_data`), `material_overrides`, `provenance` | Representing geometry attached to a world entity (`intrinsic_proto.world.GeometryComponent`) or scene update (`intrinsic_proto.scene_object.v1.GeometryUpdate`). | Decouples world state messages from heavy mesh payloads by supporting either an inline representation (`InlineGeometry`) or an opaque storage pointer (`GeometryStorageRefs`). |
| `intrinsic_proto.geometry.v1.InlineGeometry` | `exact_geometry` (`ExactGeometry`), `oneof renderable_type` (`renderable`, `generated_renderable`) | Inspecting or constructing self-contained geometry that bundles exact computational geometry with optional GLB rendering buffers. | Separates computational geometry used for collision and distance checks (`exact_geometry`) from visualization assets (`renderable` or `generated_renderable`). |
| `intrinsic_proto.geometry.v1.ExactGeometry` | `oneof data` (`primitive_set`, `triangle_mesh`, `point_cloud`), `options` (`GeometryOptions`) | Extracting vertices, faces, or analytical primitives for motion planning, collision checking, grasp generation, or bounding-box computation. | Provides the exact mathematical representation of an object's volume or surface, distinct from level-of-detail visual approximations. |
| `intrinsic_proto.geometry.v1.GeometryStorageRefs` | `exact_geometry_ref`, `renderable_ref`, `keep_renderable` | Storing and passing opaque content-addressed storage handles returned by `CreateGeometry` and embedded in `GeometryComponent`. | Prevents gRPC payload bloat by replacing multi-megabyte mesh/point-cloud buffers with immutable CAS tokens. |
| `intrinsic_proto.geometry.TransformedGeometryStorageRefs` | `geometry_storage_refs`, `ref_t_shape_aff` (`intrinsic_proto.Matrixd`) | Representing spatial volume locks in `intrinsic_proto.skills.Footprint.volume` (`VolumeReservation.shape`) and transformed geometry storage references. | Associates an opaque geometry storage reference with a 4x4 column-major affine transform matrix in the reference coordinate frame without duplicating mesh data. |
| `intrinsic_proto.geometry.OrientedBoundingBox3` | `dimensions` (`intrinsic_proto.Vector3`), `ref_t_box` (`intrinsic_proto.Pose`) | Defining 3D grasp and manipulation workspaces (`OrientedBoundingBoxWorkspace`), point cloud cropping regions (`SegmentAndProposeGraspsParams.region_of_interest`), and spatial containment checks (`NodesAndBoundingBox`). | Compactly parameterizes a 3D oriented box by its full edge lengths (`dimensions`) and center pose (`ref_t_box`) in a reference frame. |
| `intrinsic_proto.geometry.OrientedBoundingBox3WithSurfaces` | `dimensions`, `ref_t_box`, `repeated Surface surfaces` | Defining 3D regions of interest together with explicit directional surface faces (`X`, `Y`, `Z`, `MINUS_X`, `MINUS_Y`, `MINUS_Z`) for camera viewpoint planning and surface scanning (`ReconstructCellParams.regions_of_interest`). | Combines a 3D spatial containment volume with target face selection for multi-view perception and reconstruction. |
| `intrinsic_proto.geometry.v1.GeometricTransform` | `oneof data` (`matrix4d`, `trs`) | Specifying the coordinate transform (`ref_t_shape`) between an entity/link frame and a geometry shape or primitive. | Supports both machine-generated 4x4 column-major affine matrices (`matrix4d`) and human-authorable Translation/Rotation/Scale parameters (`trs`). |
| `intrinsic_proto.geometry.v1.PrimitiveShape` | `oneof shape` (`box`, `capsule`, `cylinder`, `ellipsoid`, `frustum`, `sphere`) | Defining analytical collision volumes, keepout zones, sensor field-of-view frustums, or simple workpieces. | Evaluates signed distance and collision queries analytically in O(1) time without mesh triangulation or octree discretization error. |
| `intrinsic_proto.geometry.v1.TriangleMesh` | `vertices` (`repeated double`), `faces` (`repeated int32`) | Representing polyhedral surfaces, imported CAD parts, and convex hull approximations. | Compact flat-array storage format (`[x0, y0, z0, ...]` and `[i0, i1, i2, ...]`) in row-major order optimized for serialization and buffer mapping. |
| `intrinsic_proto.geometry.v1.PointCloud` | `points` (`repeated double [packed = true]`), `normals` (`repeated double [packed = true]`) | Publishing 3D perception point clouds, bounding-box wireframes, or graspability heatmaps into `intrinsic_proto.geometry.GeometryService` and `intrinsic_proto.world.ObjectWorldService`. | Packed row-major flat arrays (`[x0, y0, z0, ...]`) for zero-copy serialization. |
| `intrinsic_proto.geometry.v1.Octree` & `OctreeWrapping` | `is_child_k_leaf`, `has_child_k`, `morton_list`, `workspace_bounding_box`, `morton_to_primitives` | Hierarchical spatial indexing of meshes and point clouds for collision checking and signed distance queries. | Encodes octree topology via 64-bit Morton Z-order codes and per-node bitmasks, mapping occupied leaf voxels to mesh triangle indices. |

## Input-aware decision trees for geometry and spatial math

### Decision tree for spatial math primitive selection

```
What spatial mathematical concept needs representation?
│
├── 6-DoF rigid body pose in world, motion planning target, or skill parameter
│   └── Precondition: Expressing coordinate frame location and orientation
│       ├── Diagnostic check: Is this an absolute 6-DoF pose in a coordinate frame?
│       │   └── Targeted action: Use intrinsic_proto.Pose (Point position + Quaternion orientation).
│       │       Ensure orientation.w = 1.0 for identity.
│       └── Diagnostic check: Is this an in-memory Python spatial calculation?
│           └── Targeted action: Use intrinsic.math.python.data_types.Pose3(rotation=..., translation=...).
│               Always use keyword arguments.
│
├── Relative coordinate transform or TF2 bridge message
│   └── Precondition: Bridging ROS 2 transforms or publishing TransformStamped
│       ├── Diagnostic check: Is the message an inter-frame displacement vector?
│       │   └── Targeted action: Use intrinsic_proto.Transform (Vector3 translation + Quaternion rotation).
│       └── Diagnostic check: Does this represent a skill parameter or service API?
│           └── Targeted action: Convert to intrinsic_proto.Pose; executive services standardize on Pose.
│
└── 3D spatial region of interest (ROI) or bounding volume
    └── Precondition: Defining 3D bounding envelope for search, cropping, or workspace limits
        ├── Diagnostic check: Are specific surface faces (X, Y, Z, -X, -Y, -Z) required for viewpoint planning?
        │   └── Targeted action: Use intrinsic_proto.geometry.OrientedBoundingBox3WithSurfaces.
        ├── Diagnostic check: Is spatial containment or volume clipping only needed?
        │   └── Targeted action: Use intrinsic_proto.geometry.OrientedBoundingBox3.
        └── Diagnostic check: Is an axis-aligned broad-phase bounding box needed?
            └── Targeted action: Use intrinsic_proto.geometry.AxisAlignedBoundingBox3 (both min and max set).
```

### Decision tree for geometry access and registration

```
What geometry operation is being performed?
│
├── Inspecting world entity geometry from ObjectWorldService
│   └── Precondition: Entity has GeometryComponent.named_geometries[role]
│       ├── Diagnostic check: Does geometry.WhichOneof("data") == "inline_geometry_data"?
│       │   └── Targeted action: Read geometry.inline_geometry_data.exact_geometry directly from memory without network RPC overhead.
│       └── Diagnostic check: Does geometry.WhichOneof("data") == "geo_ref"?
│           └── Targeted action: Call GeometryService/GetGeometry(geometry_storage_refs=geo_ref).
│
├── Displaying or retrieving visual mesh assets
│   └── Precondition: Client requires binary GLTF/GLB rendering buffer
│       ├── Diagnostic check: Is exact_geometry_ref non-empty?
│       │   └── Targeted action: Call GeometryService/GetRenderable(geometry_storage_refs=refs, lod_level=0).
│       │       The service synthesizes GLB on demand if renderable_ref is absent.
│       └── Diagnostic check: Is exact_geometry_ref missing or empty?
│           └── Targeted action: Resolve exact_geometry_ref from world object or re-register;
│               GetRenderable returns INVALID_ARGUMENT without it.
│
└── Registering new geometry with GeometryService / ObjectWorld
    └── Precondition: Custom mesh, STL, OBJ, GLB, or point cloud data ready
        ├── Diagnostic check: Does geometry require scaling, remeshing, or convex decomposition?
        │   └── Targeted action: Submit via GeometryService/ProcessGeometry; poll GetOperation.
        └── Diagnostic check: Is geometry already processed and scaled to meters?
            └── Targeted action: Call GeometryService/CreateGeometry or world.register_geometry_v1;
                extract v1 response.geometry_storage_refs.
```

## Spatial math primitives (`Pose3`, `Rotation3`, `Pose`, `Quaternion`, `Transform`)

### `API` usage hints for `Pose3` and `Rotation3`

- **Mandatory keyword arguments on `Pose3(rotation=..., translation=...)`**:
  - In Python (`intrinsic.math.python.data_types.Pose3`), the constructor signature `Pose3.__init__(self, rotation: Optional[Rotation3] = None, translation: Optional[VectorType] = None)` orders **`rotation` first and `translation` second**.
  - Conversely, the 7-element vector property `pose.vec7` and factory `Pose3.from_vec7([tx, ty, tz, qx, qy, qz, qw])` order **translation first (`[:3]`) and quaternion second (`[3:7]`)**.
  - Always pass explicit keyword arguments (`Pose3(rotation=rot, translation=[x, y, z])`) to prevent positional argument transposition.
- **Zero-initialized quaternion norm failures (`w=0` vs. `w=1.0`)**:
  - In `proto3`, a newly instantiated `intrinsic_proto.Pose()`, `intrinsic_proto.Quaternion()`, or `OrientedBoundingBox3().ref_t_box` has `x=0.0, y=0.0, z=0.0, w=0.0` (norm `0.0`).
  - Passing unnormalized quaternions (`norm != 1.0`) in motion targets or constraint frames causes solver divergence and unhandled exceptions during constraint intersection (`b/527431025`, `b/406445019`).
  - Calling `proto_conversion.pose_from_proto(pose_proto)` or server-side C++ `FromProto` on an uninitialized pose fails with `ValueError: Quaternion is not normalized` or `INVALID_ARGUMENT: Quaternion norm is 0`.
  - Always set `pose_proto.orientation.w = 1.0` when constructing an identity `intrinsic_proto.Pose` or `OrientedBoundingBox3.ref_t_box`.
- **Antipodal quaternion double-cover (q == -q) across SDK equality operators**:
  - Unit quaternions q = (x, y, z, w) and -q = (-x, -y, -z, -w) represent the same 3D rotation.
  - Python `Pose3.__eq__`, `Rotation3.__eq__`, and `Pose3.almost_equal` are antipodal-safe (`q1 == q2 or q1 == -q2` returns `True`).
  - `Quaternion.__eq__` and Protobuf `pose_pb2.Pose.__eq__` are strictly component-wise (`np.array_equal(q1.xyzw, q2.xyzw)`), returning **`False`** for antipodal quaternions +q and -q. To compute shortest angular distance between two poses, evaluate `(pose_a.rotation.inverse() * pose_b.rotation).angle()`.
  - Evaluate translational and rotational errors between two `Pose3` instances using:
```python
import math
import numpy as np
from intrinsic.math.python import data_types


def compute_pose_errors(
    pose_a: data_types.Pose3, pose_b: data_types.Pose3
) -> tuple[float, float]:
  """Computes translation distance (m) and shortest angular distance (deg)."""
  translation_diff = pose_a.translation - pose_b.translation
  position_error_m = float(np.linalg.norm(translation_diff))

  relative_rotation = pose_a.rotation.inverse() * pose_b.rotation
  orientation_error_rad = float(relative_rotation.angle())
  orientation_error_deg = float(math.degrees(orientation_error_rad))
  return position_error_m, orientation_error_deg
```
- **Numerically stable relative pose computation (`multiply_by_inverse`)**:
  - To compute the relative transform from frame A to frame B (`a_t_b`) given `world_t_a` and `world_t_b`, call `world_t_a.multiply_by_inverse(world_t_b)` (`self^-1 * other`), which subtracts `world_t_b.translation - world_t_a.translation` directly in world coordinates before rotating by `world_t_a.rotation.inverse()`, reducing floating-point cancellation error.
- **Converting `intrinsic_proto.Transform` to `Pose3`**:
  - `intrinsic.math.python.proto_conversion` does not provide a direct converter for `intrinsic_proto.Transform`. Convert it explicitly:
```python
from intrinsic.math.python import data_types
from intrinsic.math.python import proto_conversion

pose = data_types.Pose3(
    rotation=data_types.Rotation3(
        quat=proto_conversion.quaternion_from_proto(tf_proto.rotation),
        normalize=True,
    ),
    translation=proto_conversion.ndarray_from_vector3_proto(
        tf_proto.translation
    ),
)
```

## System 2 reflection and circuit breaker for geometry and math

### System 2 reflection checklist before geometric execution

Before invoking state-mutating world updates, submitting geometry to CAS, or planning trajectories, perform this self-critique:
1. **Reference frame sanity**: Is the pose expressed relative to `root` or an object's base entity frame? (Transforms targeting child links to move an object are rejected by the world service).
2. **Quaternion normalization**: Is `orientation.w = 1.0` set for identity poses, and does sqrt(x^2+y^2+z^2+w^2) ~= 1.0?
3. **Unit scaling**: Are all spatial dimensions and mesh coordinates expressed in **meters** (not millimeters)?
4. **Memory layout**: If reading or writing a 4x4 `Matrixd` affine matrix, is column-major ordering (`order='F'`) enforced?
5. **Storage reference schema**: Does the geometry registration call use v1 references (`register_geometry_v1` or `response.geometry_storage_refs`), ensuring `exact_geometry_ref` is populated?

### Anti-thrashing circuit breaker for spatial math and geometry operations

When geometric operations fail verification, avoid blind retry loops. Branch to alternative diagnostic paths after at most **2 failed verification checks**:
- **Branch 1 (Quaternion normalization error)**:
  - If `ValueError: Quaternion is not normalized` or `INVALID_ARGUMENT: Quaternion norm is 0` occurs, check whether `orientation.w` is `0.0`. Set `w = 1.0` for identity, or pass `normalize_quaternion=True` to `proto_conversion.pose_from_proto(pose, normalize_quaternion=True)`.
  - If error persists after 1 retry, re-create the pose using explicit `Pose3(rotation=data_types.Rotation3.identity(), translation=...)`.
- **Branch 2 (Missing exact geometry storage refs in `GetRenderable`)**:
  - If `GetRenderable` returns `INVALID_ARGUMENT: Missing exact geometry storage refs.`, check `request.geometry_storage_refs.exact_geometry_ref`.
  - If empty, inspect the entity's `GeometryComponent` under role `"Intrinsic_Collision"` or `"Intrinsic_Visual"` to retrieve the true CAS token, or call `world.register_geometry_v1` to generate valid v1 references.
- **Branch 3 (Bounding box min/max validation failure)**:
  - If `AxisAlignedBoundingBox3` returns `INVALID_ARGUMENT: Axis aligned bounding box does not have any min point.`, check whether only one of `min` or `max` was set.
  - Explicitly initialize both `bbox.min` and `bbox.max` for non-empty boxes, or leave both unset (`!has_min() && !has_max()`) for empty boxes.

## Core paired safety guardrails

To maintain deterministic safety and avoid the "pink elephant problem" in language model attention, all negative constraints are paired with affirmative target actions:

| Negative constraint (what to avoid) | Affirmative target action (what to do) |
| :--- | :--- |
| **1. Positional arguments on `Pose3`**: Do not invoke `Pose3(rot, trans)` or `Pose3(trans, rot)` positionally. | Always pass explicit keyword arguments: `Pose3(rotation=rot, translation=trans)`. |
| **2. Zero-norm quaternions**: Do not leave quaternion orientation default-initialized with w=0.0. | Always explicitly initialize `orientation.w = 1.0` when constructing an identity pose or bounding box pose. |
| **3. Legacy geometry registration**: Do not call `ObjectWorldClient.register_geometry` (evaluates to empty v0 refs). | Always call `ObjectWorldClient.register_geometry_v1` or `GeometryService/CreateGeometry` and extract v1 `geometry_storage_refs`. |
| **4. Row-major matrix reshaping**: Do not reshape `intrinsic_proto.Matrixd` with default C-contiguous ordering. | Always reshape with Fortran column-major `order='F'` or use `proto_conversion.ndarray_from_matrix_proto`. |
| **5. Millimeter CAD mesh units**: Do not upload CAD meshes exported in millimeters directly to CAS. | Always scale vertex coordinates by 1e-3 to meters before calling `CreateGeometry` or use `ProcessGeometry` with `ScaleProcessorConfig`. |

## Non-obvious `API` usage hints for `GeometryService` and shape protos

### 1. Why `ObjectWorldClient.register_geometry` returns a v0 schema and why callers must use `register_geometry_v1`

When `intrinsic_proto.geometry.GeometryService/CreateGeometry` succeeds, the server populates both `response.geometry_storage_refs` (`intrinsic_proto.geometry.v1.GeometryStorageRefs`, field tag 4) and `response.geometry_storage_refs_v0` (`intrinsic_proto.geometry.GeometryStorageRefs`, field tag 3).
- On `ObjectWorldClient`, the legacy `world.register_geometry(geometry=request)` helper returns `CreateGeometry(geometry).geometry_storage_refs_v0` (`v0` schema), which lacks `exact_geometry_ref` and causes subsequent v1 object creation, update, or `GetRenderable` calls to fail with missing exact geometry references.
- Always call `world.register_geometry_v1(geometry=request)` or invoke `intrinsic_proto.geometry.GeometryService/CreateGeometry` directly and read `response.geometry_storage_refs` (`intrinsic_proto.geometry.v1.GeometryStorageRefs`).
- **Public SDK import mappings for geometric structures**:
  - For binary renderable retrieval, invoke `intrinsic_proto.geometry.GeometryService/GetRenderable` (`geometry_stub.GetRenderable(GetRenderableRequest(geometry_storage_refs=v1_refs))`).
  - For bounding box structures and spatial poses, import `intrinsic.geometry.proto.axis_aligned_bounding_box_pb2` and `intrinsic.math.proto.pose_pb2`.
  - For point cloud buffers, import `intrinsic.geometry.proto.v1.geometry_pb2` or `intrinsic.geometry.proto.geometry_service_pb2`.
  - For spatial 6-DoF poses, import `intrinsic.math.proto.pose_pb2` (`pose_pb2.Pose`).

### 2. Why `GetRenderable` requires a non-empty `exact_geometry_ref` and dynamically synthesizes binary `GLB` meshes

When calling `intrinsic_proto.geometry.GeometryService/GetRenderable`:
- **`exact_geometry_ref` is mandatory**: `request.geometry_storage_refs.exact_geometry_ref` must be non-empty (`INVALID_ARGUMENT: Missing exact geometry storage refs.` is returned otherwise), even if `renderable_ref` is already populated. This is because the service first resolves the computational `Geometry` object from `exact_geometry_ref` as its primary key and then invokes `GetOrGenerateRenderable(geo)`.
- **Dynamic GLB synthesis**: If `renderable_ref` is absent or not cached in storage, `GetRenderable` dynamically generates a binary glTF 2.0 (`.glb`) renderable buffer from the underlying triangle mesh or primitive set on the fly.
- **Asymmetric CAS payload encodings**: In Content-Addressable Storage (CAS), `exact_geometry_ref` resolves to a serialized `intrinsic_proto.geometry.v1.ExactGeometry` protobuf message, whereas `renderable_ref` resolves directly to raw binary glTF 2.0 (`.glb`) bytes (not a serialized protobuf message).
- **`keep_renderable` governs lazy loading**: If `InlineGeometry.renderable` (user-authored visual asset) was populated during creation, `GeometryStorageRefs.keep_renderable` is set to `true` so the user's custom visual mesh is preserved across geometric operations. If `InlineGeometry.generated_renderable` (system-synthesized cache) was populated, `keep_renderable` is set to `false`, allowing the storage layer to skip downloading `renderable_ref` and regenerate the `.glb` on demand from `ExactGeometry`.
- **`lod_level` returns original quality (`0`)**: Although `GetRenderableRequest.lod_level` accepts integer levels of detail where `0` is original quality and higher numbers request simplified meshes, `GetRenderable` currently returns original quality with `response.lod_level = 0`.
- **`GetRenderable` and `Geometry.material_overrides`**: Because `GetRenderableRequest` takes `GeometryStorageRefs` rather than the outer `intrinsic_proto.geometry.v1.Geometry` message where `material_overrides` resides, calling `GeometryService/GetRenderable` returns the base `.glb` stored in CAS. Use `intrinsic_proto.geometry.GeometryService/GetRenderable` to fetch binary GLB renderables directly across all environments.

### 3. Checking inline geometry before calling `GetGeometry` and portable stub resolution

- **Always inspect `geometry.WhichOneof("data")` first**:
  - Analytical primitives (`ExactGeometry.primitive_set`) are serialized inline under `geometry.inline_geometry_data` (`InlineGeometry`), whereas meshes and point clouds are stored externally under `geometry.geo_ref` (`GeometryStorageRefs`).
  - Prioritize geometry roles in order: `"Intrinsic_Collision"` first (for physical accuracy and clean bounds), then `"Intrinsic_Visual"`.
  - If `geometry.WhichOneof("data") == "inline_geometry_data"`, extract `geometry.inline_geometry_data.exact_geometry` directly without making a network call. Only call `intrinsic_proto.geometry.GeometryService/GetGeometry` when `WhichOneof("data") == "geo_ref"`.
  - When inspecting `GetGeometry` responses across v0 and v1 stubs, check candidate fields in order: `getattr(resp, "inline_geometry", None)`, `getattr(resp, "geometry", None)`, and `resp`.
- **`GeometryServiceStub` resolution across skill execution and standalone environments**:
  - Inside skill execution contexts (`ExecuteContext`, `PreviewContext`), access `context.geometry_service` directly (`geometry_service_pb2_grpc.GeometryServiceStub`).
  - In standalone Python scripts or tests where `context` is unavailable, instantiate `GeometryServiceStub` directly on the Envoy ingress channel:
```python
import grpc
from intrinsic.geometry.proto import geometry_service_pb2_grpc

# In skill context:
geometry_stub = getattr(context, "geometry_service", None)

# In standalone script / test client:
if geometry_stub is None:
  channel = grpc.insecure_channel("localhost:17080")
  geometry_stub = geometry_service_pb2_grpc.GeometryServiceStub(channel)
```

### 4. `OBB`, triangle mesh, octree, and v1 primitive shape representations

#### Oriented bounding boxes (`OrientedBoundingBox3` vs. `OrientedBoundingBox3WithSurfaces`) and `AxisAlignedBoundingBox3`
- **Full-length dimensions vs. half-extents**: Both `OrientedBoundingBox3.dimensions` and `OrientedBoundingBox3WithSurfaces.dimensions` (`intrinsic_proto.Vector3`) represent **full box edge lengths** (l_x, l_y, l_z) in meters, not half-extents. Local vertices span [-l_x/2, +l_x/2] x [-l_y/2, +l_y/2] x [-l_z/2, +l_z/2] around `ref_t_box`. All three components must be strictly positive (`> 0`).
- **Explicit identity quaternion required on `OrientedBoundingBox3`**: Always initialize `ref_t_box.orientation.w = 1.0` when constructing an `OrientedBoundingBox3` (or `OrientedBoundingBox3WithSurfaces`). Leaving `ref_t_box.orientation` at default `(0, 0, 0, 0)` causes C++ server deserialization to fail with `INVALID_ARGUMENT: Quaternion norm is 0`.
- **Empty `AxisAlignedBoundingBox3` requires BOTH `min` and `max` to be unset**: An `AxisAlignedBoundingBox3` with neither `min` nor `max` populated (`!has_min() && !has_max()`) represents a valid empty box. If only `min` or only `max` is set (such as setting `max = Vector3(1, 1, 1)` while assuming `min` defaults to `(0, 0, 0)` without initializing `bbox.min`), server deserialization fails with `INVALID_ARGUMENT: Axis aligned bounding box does not have any min point.` Always explicitly initialize both `min` and `max` submessages for non-empty boxes.
- **Structural conversion between `OrientedBoundingBox3WithSurfaces` and `OrientedBoundingBox3`**: `OrientedBoundingBox3WithSurfaces` shares identical field numbers and types for `dimensions` (`1`) and `ref_t_box` (`2`). Use `axis_aligned_bounding_box_pb2` (`intrinsic.geometry.proto.axis_aligned_bounding_box_pb2`) for bounding volumes, or copy `dimensions` and `ref_t_box` (`pose_pb2.Pose`) directly:
```python
from intrinsic.geometry.proto import axis_aligned_bounding_box_pb2
from intrinsic.math.proto import pose_pb2

# Copy dimensions (Vector3) and center pose (pose_pb2.Pose) directly:
box_dimensions = roi_with_surfaces.dimensions
box_pose = pose_pb2.Pose()
box_pose.CopyFrom(roi_with_surfaces.ref_t_box)
```
- **Reference frame disambiguation across bounding box wrappers**: Neither `OrientedBoundingBox3` nor `OrientedBoundingBox3WithSurfaces` contains a `TransformNodeReference` field. Always check the enclosing message:
  - `ReconstructCellParams.regions_of_interest` and `SegmentAndProposeGraspsParams.region_of_interest`: expressed in the **world (`root`) frame**.
  - `OrientedBoundingBoxWorkspace`: pairs `bounding_box` with `optional TransformNodeReference reference_frame`, which **defaults to the robot base frame if unset** (not `root`).
  - `NodesAndBoundingBox`: pairs `bounding_box` with an explicit `TransformNodeReference reference_node`.
- **Surface enum ordering vs. Three.js box face indices**: In `intrinsic_proto.geometry.Surface`, positive faces are numbered `X = 1`, `Y = 2`, `Z = 3` and negative faces are numbered `MINUS_X = 4`, `MINUS_Y = 5`, `MINUS_Z = 6`. When rendering in Three.js (`THREE.BoxGeometry`), material array indices follow `[+X (0), -X (1), +Y (2), -Y (3), +Z (4), -Z (5)]`. Map enum values explicitly (`X -> 0`, `MINUS_X -> 1`, `Y -> 2`, `MINUS_Y -> 3`, `Z -> 4`, `MINUS_Z -> 5`).

#### Analytical v1 primitive shape conventions (`intrinsic_proto.geometry.v1.PrimitiveShape`)
All analytical shapes in `intrinsic_proto.geometry.v1.*` are centered at the local origin (0, 0, 0) and require strictly positive, finite dimensions:
- **`Box` (`size`) vs. `Ellipsoid` (`radii`)**:
  - `Box.size` (`intrinsic_proto.Vector3`) specifies **full edge lengths** (L_x, L_y, L_z) along local X, Y, and Z (extents [-L_x/2, +L_x/2]).
  - `Ellipsoid.radii` (`intrinsic_proto.Vector3`) specifies **semi-principal axes (radii)** (r_x, r_y, r_z) along local X, Y, and Z (full bounding extents are 2r_x x 2r_y x 2r_z).
- **`Cylinder` vs. `Capsule` height along the local Z-axis**:
  - Both align their symmetry axis along the **local Z-axis**.
  - For **`Cylinder`**, `length` is the total tip-to-tip height along Z (z in [-length/2, +length/2]).
  - For **`Capsule`**, `length` is the height of the **cylindrical shaft only** (between the centers of the two hemispherical caps). Because the hemispherical caps extend by `radius` at each end, the **total tip-to-tip bounding height of a `Capsule` along Z is `length + 2 * radius`** (z in [-length/2 - radius, +length/2 + radius]).
- **`Frustum` transposed half-angles (`x_angle` vs. `y_angle`)**:
  - The pyramid frustum apex is at (0, 0, 0), extending along the **positive +Z direction** between z = min_z_distance and z = max_z_distance (0 <= min_z_distance <= max_z_distance).
  - `x_angle` (0 <= x_angle < pi/2) is the half-angle measured from the **x-z plane** (y = 0), so it controls the frustum opening half-height along the **Y-axis** (delta_y = +/- z * tan(x_angle)).
  - `y_angle` (0 <= y_angle < pi/2) is the half-angle measured from the **y-z plane** (x = 0), so it controls the frustum opening half-width along the **X-axis** (delta_x = +/- z * tan(y_angle)).
  - To model a camera frustum with full horizontal FOV FOV_x and full vertical FOV FOV_y, set `y_angle = fov_x_rad / 2.0` and `x_angle = fov_y_rad / 2.0`.

#### Triangle meshes, point clouds, and column-major vs. row-major memory layouts
- **Column-major 4x4 affine matrices (`intrinsic_proto.Matrixd`)**:
  - In `TransformedGeometryStorageRefs.ref_t_shape_aff` and `GeometricTransform.matrix4d`, the 4x4 affine transform matrix is stored in `intrinsic_proto.Matrixd` (`rows = 4`, `cols = 4`) in **column-major (`order='F'`) layout**:
    `[R_00, R_10, R_20, 0, R_01, R_11, R_21, 0, R_02, R_12, R_22, 0, t_x, t_y, t_z, 1]`
  - Always use `intrinsic.math.python.proto_conversion.ndarray_from_matrix_proto(matrix_proto)` or `np.array(matrix_proto.values, dtype=np.float64).reshape((4, 4), order="F")`. Reshaping with default `order="C"` transposes the matrix and places translation components into the projective row.
- **Row-major flat arrays (`TriangleMesh` and `PointCloud`)**:
  - Conversely, `TriangleMesh.vertices` (`[x0, y0, z0, x1, y1, z1, ...]`, length 3 * N_vertices), `TriangleMesh.faces` (`[v0_0, v0_1, v0_2, ...]`, length 3 * N_faces), and `PointCloud.points` / `PointCloud.normals` are stored in **row-major (`order='C'`) layout** (`np.reshape(-1, 3)`).
  - In `PointCloud`, `points.size() % 3 == 0` is strictly enforced (`DATA_LOSS` otherwise), and if `normals` is non-empty, `normals.size() == points.size()` is required (`INVALID_ARGUMENT: Points and normals length mismatch`). When publishing a visualization-only `PointCloud` (such as ROI wireframes or grasp heatmaps) via `CreateObjectRequest`, set `action_for_object_entities.margin.hard_margin = 0.0` (standard collision point clouds use `0.005` m).
- **Two-level `GeometricTransform` composition**:
  - When extracting vertices or bounding boxes from a world object (`GeometryComponent.named_geometries[role].named_geometries[name]`), compose the outer `TransformedGeometry.ref_t_shape` transform with the inner `TransformedPrimitiveShape.ref_t_shape` transform:
    `T_link<-primitive = T_link<-geometry * T_geometry<-primitive`
- **`GeometricTransform.trs` composition order and defaults**:
  - When specifying `ref_t_shape` via `trs` (`TranslationRotationScale`), the affine transform is composed in right-applied order T * R * S (p' = R * (S * p) + T), where `RotationRPY` defines extrinsic XYZ Euler angles (R_z(yaw) * R_y(pitch) * R_x(roll)).
  - Always guard `trs.scale` extraction with `trs.HasField("scale")` (defaults to `(1.0, 1.0, 1.0)` when unset; reading an unset proto3 submessage returns `(0.0, 0.0, 0.0)`).

#### Octree spatial indexing, mesh unit scaling, and `MaterialProperties`
- **Mandatory explicit setting of `ExactGeometry.options.fill_inside_for_distance_queries`**:
  - In `intrinsic_proto.geometry.v1.ExactGeometry`, inline octree wrapping fields are reserved and removed; octrees are generated on demand at runtime.
  - Because an unset proto3 `optional bool` evaluates to `false`, omitting `options.fill_inside_for_distance_queries` when constructing an `ExactGeometry` message disables interior voxel flood-filling. Always explicitly set `exact_geometry.options.fill_inside_for_distance_queries = true` when registering closed solid meshes for signed distance and collision checks. Leave it `false` for open surface meshes or sparse point clouds.
- **Mesh units must be in meters**:
  - Coordinates in `obj_data`, `stl_bytes`, `gltf_bytes`, and `pts_bytes` passed to `CreateGeometry` are interpreted in **meters**. Uploading CAD files exported in millimeters without scaling by 1e-3 inflates the bounding box diagonal by 1000x, forcing the octree builder to coarsen leaf resolution (0.001 * diagonal) and degrading collision accuracy.
- **In-memory self-contained `.glb` requirement**:
  - `GeometryData.gltf_bytes` is parsed directly from memory without filesystem access. Text-based `.gltf` or `.obj` files referencing external `.bin` buffers or `.mtl` textures will fail to load; always pass self-contained binary `.glb` bytes.
- **`MaterialProperties` transmission configuration**:
  - To create transparent or refractive surfaces, set `transmission` in `[0.0, 1.0]` (keep `base_color.alpha = 1.0`, as non-unity `base_color.alpha` triggers validation error `INVALID_ARGUMENT: Base color alpha is not supported. Please use transmission in material properties instead`).

## Multi-`RPC` geometry processing sequences

### Sequence 1: asynchronous geometry processing (`ProcessGeometry` with `alpha_wrap`, `convex_hull`, `mesh_simplification`, `voxel_remeshing`, `coacd`, and `GetOperation` polling)

Used when importing non-convex CAD meshes, millimeter-unit STLs, or raw point clouds that require scaling, surface remeshing, or convex decomposition before attaching them as collision geometry:

```
Client / Skill -> GeometryService/ProcessGeometry -> Operation(name="geometry/Process...")
Client / Skill -> Operations/GetOperation(name) -> Operation(metadata: GeometryProcessingStatus, done=True, response: GeometryProcessingResult)
```

1. **Submit processing pipeline (`intrinsic_proto.geometry.GeometryService/ProcessGeometry`)**:
   - Construct `intrinsic_proto.geometry.ProcessGeometryRequest` specifying:
     - `geometry`: An `intrinsic_proto.geometry.v1.Geometry` containing either inline geometry data or a `geo_ref`.
     - `pipeline`: An `intrinsic_proto.geometry.PipelineConfiguration` containing ordered `steps`:
       - `scale`: Uniform scaling (`ScaleProcessorConfig(scale_factor=0.001)` for millimeter-to-meter conversion).
       - `alpha_wrap`: Watertight 3D alpha wrapping around noisy meshes or point clouds (`AlphaWrapProcessorConfig(alpha=...)`).
       - `convex_hull`: Single convex hull envelope (`ConvexHullProcessorConfig`).
       - `mesh_simplification`: Edge-collapse decimation within a geometric error bound (`MeshSimplificationProcessorConfig(max_hausdorff_distance=0.001)`).
       - `voxel_remeshing`: Closed, uniformly tessellated surface rebuilt on a voxel grid (`VoxelRemeshingProcessorConfig(resolution=64)`, resolution in [4, 256]).
       - `coacd`: Approximate convex decomposition into multiple collision hulls (`CoacdProcessorConfig(threshold=0.05, max_convex_hull=16)`).
2. **Poll long-running operation (`google.longrunning.Operations/GetOperation`)**:
   - Receive `google.longrunning.Operation` (`name = "geometry/ProcessGeometry/<fingerprint>/<nanos>"`) and poll `google.longrunning.Operations/GetOperation`.
   - Unpack `operation.metadata` into `intrinsic_proto.geometry.GeometryProcessingStatus` to monitor `progress` in [0.0, 1.0].
3. **Extract processed geometries (`intrinsic_proto.geometry.GeometryProcessingResult`)**:
   - When `operation.done` is `True`, unpack `operation.response` into `intrinsic_proto.geometry.GeometryProcessingResult`:
     - For single-output processors (`scale`, `alpha_wrap`, `convex_hull`, `mesh_simplification`, `voxel_remeshing`), extract `result.processed_geometries[0]` (or `result.processed_geometry`).
     - For multi-output processors (`coacd`), iterate over `result.processed_geometries` to attach each disjoint convex hull as a separate named entry inside `GeometryComponent.named_geometries["Intrinsic_Collision"].named_geometries` (or use `result.processed_geometry` for the fused union).

### Sequence 2: registering custom geometry and spawning a collision-enabled world object

When registering a GLB mesh or point cloud and attaching it to an object in `intrinsic_proto.world.ObjectWorldService`:

```python
from intrinsic.geometry.proto import geometry_service_pb2
from intrinsic.geometry.proto import geometry_service_pb2_grpc
from intrinsic.geometry.proto.v1 import geometry_pb2
from intrinsic.geometry.proto.v1 import transformed_geometry_pb2
from intrinsic.world.proto import geometry_component_pb2
from intrinsic.world.proto import object_world_service_pb2_grpc
from intrinsic.world.proto import object_world_updates_pb2


def register_glb_and_create_world_object(
    geometry_stub: geometry_service_pb2_grpc.GeometryServiceStub,
    world_stub: object_world_service_pb2_grpc.ObjectWorldServiceStub,
    world_id: str,
    object_name: str,
    glb_bytes: bytes,
) -> None:
  """Registers a GLB mesh with GeometryService and spawns a collision/visual object in the world."""
  # 1. Register the GLB payload in GeometryService
  create_geo_req = geometry_service_pb2.CreateGeometryRequest()
  create_geo_req.data.gltf_bytes = glb_bytes
  geo_resp = geometry_stub.CreateGeometry(create_geo_req)

  # 2. Extract the v1 GeometryStorageRefs (geometry_storage_refs_v0 uses a v0 schema lacking exact_geometry_ref)
  v1_refs = geo_resp.geometry_storage_refs

  # 3. Build v1 TransformedGeometry inside GeometryComponent.named_geometries
  transformed_geo = transformed_geometry_pb2.TransformedGeometry(
      geometry=geometry_pb2.Geometry(geo_ref=v1_refs)
  )
  geo_set = geometry_component_pb2.GeometryComponent.GeometrySet(
      named_geometries={"0": transformed_geo}
  )
  geo_comp = geometry_component_pb2.GeometryComponent(
      named_geometries={
          "Intrinsic_Collision": geo_set,
          "Intrinsic_Visual": geo_set,
      }
  )

  # 4. Create the object in ObjectWorldService
  create_obj_req = object_world_updates_pb2.CreateObjectRequest(
      world_id=world_id,
      name=object_name,
      name_is_global_alias=True,
  )
  create_obj_req.parent_object.reference.by_name.object_name = "root"
  create_obj_req.parent_object.entity_filter.include_base_entity = True
  create_obj_req.parent_object_t_created_object.orientation.w = 1.0
  create_obj_req.create_single_entity_object.geometry_component.CopyFrom(
      geo_comp
  )
  world_stub.CreateObject(create_obj_req)
```
