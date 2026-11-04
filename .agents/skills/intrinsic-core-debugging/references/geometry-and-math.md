# Debugging geometry, spatial math, quaternions, and collision meshes

This reference provides root-cause taxonomy, diagnostic checks, and remediation workflows for errors involving `intrinsic_proto.geometry.GeometryService`, content-addressable storage (CAS) handles, spatial math data types (`Pose3`, `Rotation3`, `Pose`, `Quaternion`), collision checking (`CoalCollisionChecker`), and scene graph frame parenting.

## Input-aware decision tree for geometry and spatial math faults

```
What geometry, spatial math, or collision symptom is observed?
│
├── Geometry registration, CAS loading, or renderable resolution failure
│   └── Precondition: Registering asset mesh or requesting GLB renderable
│       ├── Diagnostic check: Does GetRenderable return INVALID_ARGUMENT: Missing exact geometry storage refs?
│       │   └── Targeted action: Call ObjectWorldClient.register_geometry_v1 (or extract response.geometry_storage_refs
│       │       from CreateGeometry). Ensure exact_geometry_ref is populated before requesting renderables.
│       ├── Diagnostic check: Does ResetBeliefWorldAndSim return DEADLINE_EXCEEDED: Could not load geometry with fingerprint?
│       │   └── Targeted action: Check cluster authentication (inctl auth status) and network reachability of CAS
│       │       (intcas://...). Verify geometry asset fingerprint exists in storage.
│       └── Diagnostic check: Do updated CAD/URDF/SDF collision meshes appear ignored after solution push?
│           └── Targeted action: WorldSyncer applies existing world_updates/*.pbtxt over base templates. Inspect and remove
│               stale overrides in world_updates/entity_properties.pbtxt or object_properties.pbtxt before pushing.
│
├── Spatial math, quaternion normalization, or pose construction crash
│   └── Precondition: Instantiating poses, converting protobufs, or executing Cartesian motions
│       ├── Diagnostic check: Does pose_from_proto or CartesianMotionTarget raise ValueError: Quaternion is not normalized?
│       │   └── Targeted action: Proto3 defaults numeric scalars to 0.0. Initialize pose.orientation.w = 1.0 on all
│       │       newly constructed identity or target pose protobufs.
│       ├── Diagnostic check: Does Pose3 constructor invert translation and rotation?
│       │   └── Targeted action: Pose3.__init__ expects (rotation, translation), whereas from_vec7 expects translation first.
│       │       Pass explicit keyword arguments: Pose3(rotation=rot, translation=[x, y, z]).
│       └── Diagnostic check: Does rotation comparison pose_a.orientation == pose_b.orientation evaluate False unexpectedly?
│           └── Targeted action: Unit quaternions double-cover SO(3) (+q == -q). Evaluate angular distance using
│               (pose_a.rotation.inverse() * pose_b.rotation).angle() against an angular tolerance.
│
├── Collision checking timeout or motion planner collision false positive
│   └── Precondition: move_robot or compute_ik stalls for minutes or fails collision validation
│       ├── Diagnostic check: Do world pod logs show "Large volume mesh detected, collision computation may be slow"?
│       │   └── Targeted action: CAD mesh was imported in millimeters. Rescale vertex coordinates by 0.001 to meters
│       │       before CAS upload to bring bounding volume below 200.0 m³.
│       ├── Diagnostic check: Did modifying collision margins re-enable collisions between excluded link pairs?
│       │   └── Targeted action: Exclusion rules (kIsExcluded) precede margin rules (kMargin). Preserve existing exclusion
│       │       pairs when updating collision settings rather than overwriting the entire rule list.
│       └── Diagnostic check: Does collision checking pass in Gazebo simulation but fail in motion planning?
│           └── Targeted action: Motion planning (CoalCollisionChecker) checks raw triangle OBB trees; Gazebo checks
│               convex hull decompositions (CoACD). Inspect whether thin features violate raw triangle tolerances.
│
└── Transform tree parenting and scene object placement anomaly
    └── Precondition: Defining scene object templates or reparenting runtime entities
        ├── Diagnostic check: Does world composition log "Root entity <name> has parent_t_this which will be ignored"?
        │   └── Targeted action: Root entities in scene objects have no parent. Specify placement poses on the spawn frame
        │       (SpawnObject) or on child entity attachments rather than parent_t_this on the root entity.
        └── Diagnostic check: Does reparenting fail with INVALID_ARGUMENT: The ObjectReference in the request must be set?
            └── Targeted action: Populate parent_object or parent_frame explicitly in ReparentObjectRequest.
```

## Core paired safety guardrails

Adhere to these five paired safety guardrails across all geometry and spatial math workflows:

1. **Do not** leave default-instantiated `intrinsic_proto.Pose()` quaternions uninitialized (w = 0.0); **always** set `pose.orientation.w = 1.0` to guarantee a valid unit norm (|q| = 1.0).
2. **Do not** pass positional arguments to `intrinsic.math.python.data_types.Pose3`; **always** pass explicit keyword arguments `Pose3(rotation=..., translation=...)` to prevent accidental transposition.
3. **Do not** invoke legacy `ObjectWorldClient.register_geometry` (v0); **always** invoke `ObjectWorldClient.register_geometry_v1` or read `response.geometry_storage_refs` from `CreateGeometry`.
4. **Do not** reshape `intrinsic_proto.Matrixd` with default C-style row-major ordering; **always** unpack with Fortran column-major ordering (`order='F'`) or `ndarray_from_matrix_proto`.
5. **Do not** upload CAD meshes in millimeter units; **always** rescale vertex coordinates by `0.001` to meters before registering into CAS to prevent bounding volume inflation.

## `System 2` reflection and anti-thrashing circuit breakers

Before executing state-mutating geometry uploads, transform publications, or collision configuration updates, perform this brief self-critique:

- [ ] **Frame validation**: Is the transform expressed in the intended reference frame (`world`, robot base link, tool flange, or sensor optical frame)?
- [ ] **Unit consistency**: Are translational coordinates in meters (1 mm = 0.001 m) and rotational angles in radians?
- [ ] **Quaternion normalization**: Does the orientation satisfy |q| = sqrt(x^2+y^2+z^2+w^2) = 1.0 within +- 10^-6?
- [ ] **Mesh volume sanity**: Is the mesh bounding volume strictly under 200.0 m^3?
- [ ] **Exclusion preservation**: Do updated collision settings retain existing `CollisionAction::kIsExcluded` link pairs?

### Anti-thrashing circuit breaker

If a spatial transform calculation, quaternion normalization check, or collision query fails during execution:

1. **Attempt 1 (Local parameter verification)**: Verify unit scaling (10^-3 for CAD millimeters) and explicitly normalize orientation vectors using `q / np.linalg.norm(q)`.
2. **Attempt 2 (Service log inspection)**: Inspect container logs (`kubectl logs -n app-intrinsic-base deployment/world`) for `Large volume mesh detected` or `Skipping TF Message while world updater is paused`.
3. **Circuit breaker trip**: If the failure persists after 2 attempts, **halt parameter modification immediately**. Switch immediately to inspecting the upstream asset definition (`model.sdf`), checking `inctl world reset --address=localhost:17080`, or querying `ListObjects` across `init_world`, `world`, and `sim_world`.

## Geometry registration and `GeometryStorageRefs` resolution

The digital twin decouples geometric definitions into computational geometry, rendering assets, and content-addressable storage (CAS) tokens:

- **Mandatory v1 geometry registration**: Always invoke `ObjectWorldClient.register_geometry_v1` rather than `register_geometry`. The legacy v0 `register_geometry` method returns `intrinsic_proto.geometry.GeometryStorageRefs` (field 3), which lacks `exact_geometry_ref` and cannot be assigned to `intrinsic_proto.geometry.v1.Geometry.geo_ref`.
- **Resolving `Missing exact geometry storage refs`**: Calling `intrinsic_proto.geometry.GeometryService/GetRenderable` requires `request.geometry_storage_refs.exact_geometry_ref` to be populated. If passed v0 storage references, the server returns `INVALID_ARGUMENT: Missing exact geometry storage refs`, even if `renderable_ref` is present, because the service dynamically synthesizes missing GLB renderables from `exact_geometry_ref`.
- **CAS payload encodings**: Storage handles are immutable tokens. The `exact_geometry_ref` token points to serialized `intrinsic_proto.geometry.v1.ExactGeometry` protobuf bytes, whereas `renderable_ref` points to raw binary glTF 2.0 (`.glb`) payloads.
- **Stale `.pbtxt` overrides in `WorldSyncer`**: Solution package synchronization (`WorldSyncer.pull()`) serializes live world properties into `world_updates/*.pbtxt` (`entity_properties.pbtxt`, `object_properties.pbtxt`). On subsequent pushes (`WorldSyncer.push()`), these files are applied on top of the base `SceneObject` template, overriding updated CAD or collision mesh values. When updated asset definitions appear ignored at runtime, inspect and remove stale override files in `world_updates/*.pbtxt`.

## Root entity transforms and scene object parenting (`parent_t_this`)

Coordinate frames within scene objects follow strict topological parenting rules during world composition:

- **Root entity transform overrides**: During world composition, non-identity `parent_t_this` transforms placed directly on root entities inside scene object definitions are ignored by the scene loader (`Root entity <name> has parent_t_this which will be ignored`). Because root entities have no parent within the object template, their transform is fixed to the object's reference frame.
- **Specifying object placement**: Always specify initial placement poses on the spawn frame (via `SpawnObject` or `CreateObjectRequest.ref_t_object`) or on child entity attachments rather than placing `parent_t_this` directly on the root entity.
- **Kinematic connection trees**: For non-root links, `parent_t_this` denotes the pose of the child frame relative to its parent frame. Ensure joint axis definitions (`inboard`, `outboard`, and `axis`) are parameterized with consistent frame origins to prevent joint articulation direction reversals.

## Long-running `ProcessGeometry` operations and pipeline schemas

Asynchronous geometry processing operations (alpha wrapping, convex decomposition, remeshing) follow specific polling contracts:

- **Polling `ProcessGeometry` completion**: `intrinsic_proto.geometry.GeometryService/ProcessGeometry` returns a long-running operation whose name is prefixed with `geometry/` (`"geometry/ProcessGeometry/<fingerprint>/<nanos>"`). Always poll operation completion using `google.longrunning.Operations/GetOperation` in a retry loop. Calling `WaitOperation` through the central `operations:8080` proxy returns `StatusCode.NOT_FOUND` because C++ geometry processing does not support blocking wait streams.
- **Exact field names in `GeometryProcessingPipelineConfig`**:
  - `AlphaWrapProcessorConfig(alpha=...)` (specifies absolute wrap size in meters; fields `relative_alpha` and `relative_offset` are legacy and unsupported).
  - `VoxelRemeshingProcessorConfig(resolution=...)` (voxel grid size along the largest axis, in [4, 256]; `IsotropicRemeshingProcessorConfig` has been removed).
  - `CoacdProcessorConfig(threshold=..., max_convex_hull=...)` (governs approximate convex decomposition).

## Spatial math primitives, `Pose3` constructors, and quaternion normalization

Spatial calculations across Python SDK clients and gRPC services require strict data type hygiene:

- **Explicit keyword arguments on `Pose3`**:
  - In `intrinsic.math.python.data_types`, `Pose3.__init__(self, rotation=None, translation=None)` orders `rotation` first and `translation` second.
  - In contrast, `Pose3.from_vec7([tx, ty, tz, qx, qy, qz, qw])` and `pose.vec7` order translation first.
  - Always pass explicit keyword arguments: `Pose3(rotation=rot, translation=[x, y, z])`.
- **Zero-initialized Proto3 quaternion norm failures (`w = 0.0` vs. `w = 1.0`)**:
  - Default-instantiated `intrinsic_proto.Pose()` messages set `orientation.w = 0.0` (quaternion norm 0.0).
  - Passing an uninitialized pose protobuf to `intrinsic.math.python.proto_conversion.pose_from_proto` or `CartesianMotionTarget` raises `ValueError: Quaternion is not normalized`.
  - Always set `pose.orientation.w = 1.0` when constructing identity or bounding box pose protobufs.
- **Antipodal quaternion equality (+q == -q)**:
  - Unit quaternions +q and -q represent the same physical 3D orientation.
  - `Pose3.__eq__` and `Rotation3.__eq__` evaluate antipodal pairs as equal (`True`).
  - Raw `Quaternion.__eq__` and protobuf `pose_pb2.Pose.__eq__` perform component-wise comparisons and evaluate `False`.
  - Always compare poses and orientations using `Pose3` / `Rotation3`, or compute relative angular distance via `(pose_a.rotation.inverse() * pose_b.rotation).angle()`.
- **Column-major `Matrixd` vs. row-major mesh buffers**:
  - 4x4 affine transform matrices in `intrinsic_proto.Matrixd` (`ref_t_shape_aff`) are serialized in column-major (`order='F'`) format. Reshaping with default row-major C order transposes the rotation matrix and misplaces translation into the bottom row.
  - Triangle mesh vertex buffers (`TriangleMesh.vertices`) and point cloud buffers (`PointCloud.points`) use row-major C order (`[x0, y0, z0, x1, y1, z1, ...]`).
- **Verified public SDK imports**:
  - In standalone Python scripts, import geometry and math protobufs from `intrinsic.geometry.proto` (`axis_aligned_bounding_box_pb2`, `exact_geometry_pb2`, `geometry_service_pb2`) and `intrinsic.math.proto` (`pose_pb2`, `quaternion_pb2`, `vector3_pb2`).
  - Rely exclusively on these public modules; internal helper modules (`conversion_utils`, `transform_pb2`) are omitted from standalone distributions.

## Collision checking pipelines, `CAD` import scaling, and mesh topology

Discrepancies in collision checking and motion planning typically trace to unit scale, mesh topology, or checker pipeline differences:

- **Diagnosing multi-minute planning timeouts (`Large volume mesh detected`)**:
  - If motion planning stalls for minutes, inspect World Service logs (`kubectl logs -n app-intrinsic-base deployment/world`) for:
    `Large volume mesh detected, collision computation may be slow. Number of triangles: <left> and <right>. Volume: <left_vol> and <right_vol>`
  - When a mesh bounding volume exceeds `200.0 m³` (typically caused by exporting CAD models in millimeters instead of meters, inflating volume by 10^9x) and the triangle-pair product exceeds `20,000`, the collision checker falls back to slow octree traversal.
  - Rescale mesh vertex coordinates by `0.001` to meters before uploading, or replace dense visual meshes with simplified convex hulls (`CoacdProcessorConfig`).
- **Collision rule precedence (`kIsExcluded` vs. `kMargin`)**:
  - In collision rule evaluation, exclusion rules (`CollisionAction::kIsExcluded`) strictly precede margin rules (`CollisionAction::kMargin`).
  - When updating collision margins via SDK or CLI, preserve existing exclusion pairs rather than overwriting the entire collision settings list.
- **Checker pipeline differences (`CoalCollisionChecker` vs. Gazebo simulation)**:
  - Motion planning (`CoalCollisionChecker`) builds triangle-face OBB BVH trees (`coal::BVHModel<coal::OBB>`) from raw mesh triangles and executes dynamic AABB broadphase before narrowphase (whereas explicit pairwise queries like `AreObjectsInCollision` bypass broadphase).
  - Gazebo simulation checks collisions against SDF-level convex decompositions (`CoACD`).
  - Discrepancies where motion planning detects collisions that Gazebo permits usually stem from raw triangle BVH conservatism vs. convex decomposition smoothing.
- **CAD mesh topology: 2-manifold requirements vs. polygon soups**:
  - Exact geometric operations requiring CGAL surface meshes (`CleanMeshOptions::Strict()`) enforce 2-manifold topology without self-intersections, failing on open or self-intersecting CAD exports.
  - Approximate convex hull decomposition (`CoacdGeometryProcessor` with `CleanMeshOptions::BestEffort()`) operates on repaired polygon soups. For complex workpiece colliders, prefer convex hull decomposition over raw polyhedral mesh checking.

## Diagnostic utilities for workcell inspection

Use these standalone Python diagnostic functions to validate poses, quaternion norms, and mesh bounds before dispatching world updates:

```python
import math
from typing import Sequence, Tuple
import numpy as np


def validate_and_normalize_quaternion(
    qx: float, qy: float, qz: float, qw: float
) -> Tuple[float, float, float, float]:
  """Validates quaternion norm and returns unit quaternion.

  Raises:
      ValueError: If quaternion components are non-finite or norm is near-zero.
  """
  norm = math.sqrt(qx * qx + qy * qy + qz * qz + qw * qw)
  if not math.isfinite(norm) or norm < 1e-8:
    raise ValueError(
        f"Quaternion norm must be finite and non-zero (got norm={norm}). "
        "Initialize qw=1.0 for identity."
    )
  return (qx / norm, qy / norm, qz / norm, qw / norm)


def compute_angular_distance_radians(
    q1: Sequence[float], q2: Sequence[float]
) -> float:
  """Computes relative rotation angle in radians between two quaternions.

  Handles antipodal equivalence (+q == -q) and automatically normalizes inputs.

  Raises:
      ValueError: If either sequence does not have 4 elements or has zero norm.
  """
  if len(q1) != 4 or len(q2) != 4:
    raise ValueError("Quaternions must contain exactly 4 components [x, y, z, w].")
  norm1 = math.sqrt(sum(x * x for x in q1))
  norm2 = math.sqrt(sum(x * x for x in q2))
  if not math.isfinite(norm1) or norm1 < 1e-8 or not math.isfinite(norm2) or norm2 < 1e-8:
    raise ValueError("Quaternion norms must be finite and non-zero.")
  dot_product = abs(sum(a * b for a, b in zip(q1, q2))) / (norm1 * norm2)
  clamped_dot = min(1.0, max(-1.0, dot_product))
  return 2.0 * math.acos(clamped_dot)


def check_mesh_bounding_volume(
    vertices: np.ndarray,
    volume_limit_m3: float = 200.0,
) -> Tuple[bool, float]:
  """Checks whether mesh vertices exceed the large volume threshold.

  Supports both (N, 3) vertex matrices and flat (3N,) coordinate buffers.

  Args:
      vertices: Array of shape (N, 3) or flat buffer of shape (3N,).
      volume_limit_m3: Threshold in cubic meters (default 200.0 m³).

  Returns:
      Tuple of (is_safe, volume_m3). If is_safe is False, scale by 0.001.

  Raises:
      ValueError: If vertex array shape is incompatible with 3D coordinates.
  """
  verts = np.asarray(vertices, dtype=float)
  if verts.size == 0:
    return (True, 0.0)
  if verts.ndim == 1:
    if verts.size % 3 != 0:
      raise ValueError(
          f"Flat vertex buffer length ({verts.size}) must be a multiple of 3."
      )
    verts = verts.reshape(-1, 3)
  elif verts.ndim != 2 or verts.shape[1] != 3:
    raise ValueError(f"Vertices array must have shape (N, 3), got {verts.shape}.")
  min_coords = np.min(verts, axis=0)
  max_coords = np.max(verts, axis=0)
  extents = max_coords - min_coords
  volume = float(extents[0] * extents[1] * extents[2])
  is_safe = volume <= volume_limit_m3
  return (is_safe, volume)
```
