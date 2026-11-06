<!--
Copyright 2026 Intrinsic Innovation LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# NVIDIA Pose Estimator

The **NVIDIA Pose Estimator** perception service integrates **SAM 3** (zero-shot promptable
segmentation), **FoundationStereo** (metric stereo depth estimation), and **FoundationPose**
(model-based 6D pose estimation).

- **Pure TensorRT 10.16 runtime** — PyTorch is not a dependency of the service.
- **Deterministic warmup** — staged FoundationPose → FoundationStereo → SAM 3 at startup, so engine
  compilation does not happen mid-request. If warmup fails the service still starts and the first
  request compiles whatever is missing while holding the GPU lock.
- **gRPC interface** — implements `intrinsic_proto.perception.v1.PoseEstimationService`.
- **Debug artifacts on demand** — off by default; set `debug.artifact_dir` to dump pipeline intermediates, see [§2.5](#25-debug-artifacts-and-visualization-publishing).

Every command below uses:

```bash
PKG=//intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator
```

---

## 1. Setup

**Bazel fetches and builds everything except the SAM 3 weights.** The upstream sources, the
FoundationPose C++ library, the TensorRT and CUDA runtimes, and the FoundationPose /
FoundationStereo weights all come from pinned Bazel repositories. [§5](#5-how-the-build-works) has
the details if you need them; you do not need them to build or deploy.

### 1.1. Export the SAM 3 Weights

`facebook/sam3` is gated (`gated: "manual"`), so a build cannot fetch it. Accept the Meta SAM
License on [HuggingFace](https://huggingface.co/facebook/sam3), authenticate locally
(`hf auth login`), then run (uses a local CUDA GPU if available, otherwise exports on CPU):

```bash
./intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/scripts/prepare_sam3_models.sh
```

This downloads `sam3.pt`, exports four ONNX graphs plus the BPE vocabulary, and stages five files
into `intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models`
(overridable via `$NVIDIA_POSE_ESTIMATOR_MODELS_DIR` or `--output-dir`):

| File | Size | Description |
| :--- | ---: | :--- |
| `sam3_vision_encoder.onnx` | 1.73 GB | ViT-Huge vision backbone (1008 × 1008) |
| `sam3_text_encoder.onnx` | 1.35 GB | Open-vocabulary text encoder |
| `sam3_mask_decoder.onnx` | 111.3 MB | Cross-attention mask decoder |
| `sam3_box_decoder.onnx` | 124.0 MB | Box decoder |
| `bpe_simple_vocab_16e6.txt.gz` | 1.3 MB | BPE vocabulary |

The `sam3_model_data_asset` target is tagged `manual`, so `bazel build //...` neither trips over
missing weights nor spends time producing a 3.3 GB bundle. Without a complete export, building it
logs a warning naming what is missing and skips building the bundle. Build and install it in
[§2](#2-build--deploy) along with everything else.

`bpe_simple_vocab_16e6.txt.gz` is SAM Materials under the SAM License — **not** the MIT-licensed
CLIP vocabulary of the same name.

---

## 2. Build & Deploy

### 2.1. Build

```bash
# Everything a cluster deployment needs: three Data assets plus the service.
bazel build --config=intrinsic \
  --$PKG:build_full_image=True \
  --$PKG:download_weights=True \
  $PKG:foundationpose_model_data_asset \
  $PKG:foundationstereo_model_data_asset \
  $PKG:sam3_model_data_asset \
  $PKG:nvidia_pose_estimator_service_asset
```

Both flags default to `False` so CI and presubmit jobs do not waste bandwidth and runner time
pulling multi-gigabyte images or model weights:

- `build_full_image=True` bases the image on `nvcr.io/nvidia/tensorrt` (~5.8 GB) and bundles
  `libfoundation_pose_nvidia.so`. Without it the service image is built on the lightweight Python
  base for fast structural tests and is not deployable.
- `download_weights=True` pulls the FoundationPose and FoundationStereo ONNX weights (~480 MB from
  NGC). Without it building either data asset logs a warning and skips building the `.bundle.tar`.

The three data-asset targets are tagged `manual` and must be named explicitly; `$PKG:all` will not
pick them up. `sam3_model_data_asset` requires [§1.1](#11-export-the-sam-3-weights) to have run (if
not staged yet, building it also logs a warning and skips building the bundle).

For local container testing you only need the image (still with `build_full_image=True` so the
TensorRT base image and `libfoundation_pose_nvidia.so` are included):

```bash
bazel build --config=intrinsic \
  --$PKG:build_full_image=True \
  $PKG:nvidia_pose_estimator_image.tar
```

Outputs land in `bazel-bin/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/`.

### 2.2. Deploy

Each model group is packaged as an Intrinsic Data asset carrying an
`intrinsic_proto.perception.v1.PerceptionModel`, uploaded to the cluster's content-addressable
storage and streamed by the service at startup:

| Asset | Contents | Built from |
| :--- | :--- | :--- |
| `ai.intrinsic.nvidia_pose_estimator.foundationpose_models` | `refiner_net.onnx`, `score_net.onnx` | `http_file`, hermetic |
| `ai.intrinsic.nvidia_pose_estimator.foundationstereo_models` | `deployable_foundation_stereo_s_dynamic_v2.0.onnx` | `http_file`, hermetic |
| `ai.intrinsic.nvidia_pose_estimator.sam3_models` | 4 SAM 3 ONNX graphs + BPE vocabulary | your local export, see [§1.1](#11-export-the-sam-3-weights) |

Install all three, then the service, then create the instance:

```bash
inctl asset install --org=$ORG --cluster=$VM \
  bazel-bin/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/foundationpose_model_data_asset.bundle.tar

inctl asset install --org=$ORG --cluster=$VM \
  bazel-bin/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/foundationstereo_model_data_asset.bundle.tar

inctl asset install --org=$ORG --cluster=$VM \
  bazel-bin/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/sam3_model_data_asset.bundle.tar

inctl asset install --org=$ORG --cluster=$VM \
  bazel-bin/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/nvidia_pose_estimator_service_asset.bundle.tar

inctl service add --org=$ORG --cluster=$VM ai.intrinsic.nvidia_pose_estimator_service
```

`inctl asset install` uploads the model files from the bundle to CAS and rewrites the manifest
references to `intcas://`. `sam3_model_data_asset.bundle.tar` is 3.3 GB and dominates the upload
time.

> [!IMPORTANT]
> Install the data assets **before** `inctl service add`. `configs/default_config.textproto` binds
> them by name, and the platform resolves those names to asset ids only once, when the instance is
> created. Reinstalling an asset afterwards does not re-resolve — delete and re-add the instance:
>
> ```bash
> inctl service delete --org=$ORG --cluster=$VM ai.intrinsic.nvidia_pose_estimator_service
> inctl service add --org=$ORG --cluster=$VM ai.intrinsic.nvidia_pose_estimator_service
> ```

The service fails to start if any model asset dependency is unresolved.

### 2.3. TensorRT Plan Cache

Compiling the seven TensorRT engines (two FoundationPose, one FoundationStereo, four SAM 3) takes
tens of minutes and produces roughly 3.2 GB of plans.

**Where the cache lives.** `service_manifest.textproto` mounts
`/var/lib/file-storage/service_volumes/intrinsic/nvidia_pose_estimator` at `/models`, so plans built
on first boot (`/models/engine_cache`) and downloaded weights (`/models/weights`) survive pod
restarts on the host node.

**The cache is namespaced by the resolved model versions.** `ModelPaths.preferred()` matches
`{model}__*.plan` by filename prefix without re-hashing the ONNX, so at startup the service hashes
the `(asset id, version, per-file digest)` tuple of all three model assets down to 16 hex characters
and uses `/models/engine_cache/<key>/` as the cache directory, symlinked into place as
`/models/weights/<key>/engine_cache`. Weights are downloaded into `/models/weights/<key>/`, and only
missing files are fetched. Install a newer model asset and you get a new namespace, so a plan
compiled against the previous ONNX can never be picked up. Two namespaces of each are kept; older
ones are pruned by last use.

**Shipping prebuilt plans.** Drop them into `engine_cache_bundle/` under the staging directory and
the corresponding model Data asset picks them up automatically — `sam3_*` plans go into the SAM 3
asset, `refine_*`/`score_*` into FoundationPose, `deployable_foundation_stereo_*` into
FoundationStereo. Rebuild the asset and install it; the plans arrive alongside the ONNX and land in
that asset's namespace.

```bash
STAGING=${NVIDIA_POSE_ESTIMATOR_MODELS_DIR:-intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models}
mkdir -p "$STAGING/engine_cache_bundle"
cp -l /var/lib/file-storage/service_volumes/intrinsic/nvidia_pose_estimator/engine_cache/*/*.plan \
      "$STAGING/engine_cache_bundle/"   # -l hardlinks

bazel build --config=intrinsic \
  --$PKG:download_weights=True \
  $PKG:foundationpose_model_data_asset \
  $PKG:foundationstereo_model_data_asset \
  $PKG:sam3_model_data_asset
```

Ship the `.plan.json` next to each `.plan` if you have it. The `.lock` files are runtime scratch and
are skipped.

> [!CAUTION]
> **Plans are not portable across GPUs or TensorRT versions.** TensorRT serializes for a specific
> compute capability, and `preferred()` resolves by glob without checking which GPU the fingerprint
> encodes. Ship A5000 plans and run elsewhere and `deserialize_cuda_engine` returns null, which
> `TRTEngine` turns into a `RuntimeError` — there is no fallback to rebuilding from ONNX. If you
> publish an asset carrying plans, encode the target in its version, for example
> `1.0.0+a5000-trt10`. Keep **exactly one plan per model**; two matches are ambiguous and silently
> fall back to recompiling.

**First boot with no plans.** Warmup stages 1–3 build each model group's engines in isolation
(FoundationPose → FoundationStereo → SAM 3), calling `_unload_models()` between stages so the
builder workspaces need not coexist on the GPU. Expect tens of minutes and a log that looks idle.
The final resident stage runs a synthetic FoundationPose pass (`estimator.register`) and a dummy
SAM 3 pass (`_run_sam3_dummy_pass`) to leave all engines loaded in GPU memory. Subsequent starts
hit the cache.

### 2.4. Run the Container

```bash
docker load -i bazel-bin/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/nvidia_pose_estimator_image.tar

IMAGE=intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/nvidia_pose_estimator_image:latest
MODELS_DIR=${NVIDIA_POSE_ESTIMATOR_MODELS_DIR:-$PWD/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models}
```

Outside a cluster there is no asset service or CAS, so mount a local directory containing all eight
weight files (the five SAM 3 files from [§1.1](#11-export-the-sam-3-weights) plus `refiner_net.onnx`,
`score_net.onnx`, and `deployable_foundation_stereo_s_dynamic_v2.0.onnx`) and point `MODELS_DIR` at
it:

**With the NVIDIA Container Toolkit:**

```bash
docker run -p 50051:50051 --gpus all \
  -v "$MODELS_DIR":/models -e MODELS_DIR=/models \
  "$IMAGE"
```

**Plain `runc` with host driver mounts:**

```bash
LIBCUDA=$(readlink -f /usr/lib/x86_64-linux-gnu/libcuda.so.1)
LIBPTX=$(readlink -f /usr/lib/x86_64-linux-gnu/libnvidia-ptxjitcompiler.so.1)
docker run --name nvidia_pose_service --runtime=runc --rm \
  --device /dev/nvidia0 --device /dev/nvidiactl --device /dev/nvidia-uvm --device /dev/nvidia-uvm-tools \
  -v "$LIBCUDA":/usr/lib/x86_64-linux-gnu/libcuda.so.1:ro -v "$LIBCUDA":"$LIBCUDA":ro \
  -v "$LIBPTX":/usr/lib/x86_64-linux-gnu/libnvidia-ptxjitcompiler.so.1:ro -v "$LIBPTX":"$LIBPTX":ro \
  -v "$MODELS_DIR":/models -e MODELS_DIR=/models \
  -p 50051:50051 "$IMAGE"
```

The container builds any missing `.plan` engines on the target GPU, completes warmup, and listens
on `50051`:

```text
[Warmup 1/3] FoundationPose warmup successful (XX.XXXXs)!
[Warmup 2/3] FoundationStereo warmup successful (X.XXXXs)!
[Warmup 3/3] SAM 3 warmup successful (XX.XXXXs)!
[Warmup Resident] Staged resident warmup completed in XX.XXXXs.
--------------------------------------------------
-- Server listening at port : [::]:50051 ...
--------------------------------------------------
```

### 2.5. Debug Artifacts and Visualization Publishing

`DebugConfig.publish_visualization` (**enabled by default** in `configs/default_config.textproto`)
publishes the rendered SAM 3 detection overlay (`sam3_proposals.png`) and CAD pose overlay
(`cad_overlay.png`) as `ImageBuffer` messages (`PIXEL_INTENSITY`) to the Intrinsic PubSub topics
`ioc_pose_estimator/detections` (`in/ioc_pose_estimator/detections` on Zenoh) and
`ioc_pose_estimator/poses` (`in/ioc_pose_estimator/poses` on Zenoh).

`DebugConfig.artifact_dir` controls a per-request disk dump of pipeline intermediates, implemented in
`util/debug_utils.py`. **It is unset by default**, which disables disk dumping (only the in-memory
overlays are rendered for PubSub when `publish_visualization` is true). Set it in
`configs/default_config.textproto` to dump every request, or per request as shown below. Each
request writes a timestamped subdirectory (`<YYYYMMDD-HHMMSS>/`):

| Artifact | Contents |
| :--- | :--- |
| `summary.json` | target name, prompt, confidence threshold, depth source, sensor metadata, depth stats, per-candidate `selection_results`, and final `estimates` (`score` and raw `fp_score`) |
| `detection_view_rgb.png` | the RGB detection frame fed to SAM 3 |
| `sensors/sensor_<id>_rgb.png` | intensity/RGB image for each sensor in the request |
| `sensors/sensor_<id>_depth.npy`, `sensors/sensor_<id>_depth_vis.png` | raw float32 depth (meters) and TURBO colormap preview for each `PIXEL_DEPTH` sensor |
| `depth_metric.npy`, `depth_vis.png` | final depth map aligned/reprojected into the detection camera frame, as raw float32 meters and TURBO colormap preview |
| `sam3_proposals.png`, `sam3_mask_{i}.png` | raw SAM 3 bounding-box proposals and per-instance binary masks |
| `cad_overlay.png` | all candidate poses rendered over the detection frame with 3D axes and `[KEPT]` (green) / `[REJ]` (orange) labels showing `score` and `fp_score` |
| `target_cad.ply` | a copy of the target CAD mesh used |

A per-request override takes precedence over the service default, so a single call can be dumped
without redeploying. A per-request `artifact_dir` must be under `/tmp`; anything else is rejected
with `INVALID_ARGUMENT`.

```python
cfg = nvidia_service_config_pb2.NvidiaPoseEstimatorServiceConfig()
cfg.debug.artifact_dir = "/tmp/one_off"
request.pose_estimation_run_config.inference_parameters.Pack(cfg)
```

`save_debug_artifacts` never raises; failures are logged and swallowed so that a debugging aid
cannot fail a pose estimation request.

> [!NOTE]
> Setting `debug.artifact_dir` in the service config writes artifacts on every request, which
> costs disk and latency. Prefer the per-request override.

---

## 3. Configuration

The service is configured by `NvidiaPoseEstimatorServiceConfig`
([`proto/nvidia_service_config.proto`](proto/nvidia_service_config.proto)); the defaults are in
[`configs/default_config.textproto`](configs/default_config.textproto).

- **Per instance:** pass a binary-serialized `google.protobuf.Any` of the config to
  `inctl service add --config=<file>.binpb`. It is merged over the defaults, but it must keep
  `intrinsic_runtime` and the three `*_model` fields, which the platform resolves when the instance
  is created.
- **Per request:** pack a partial config into `PoseEstimationRunConfig.inference_parameters`, as in
  [§2.5](#25-debug-artifacts-and-visualization-publishing). Only the fields marked ✓ in
  [§3.2](#32-options) apply per request; the rest are read once at startup.

### 3.1. Depth Source and GPU Memory

`sensor_routing.depth_source` decides whether FoundationStereo is loaded, and with it how much GPU
memory the service needs:

| `depth_source` | Depth comes from | FoundationStereo | GPU memory |
| :--- | :--- | :--- | :--- |
| `DEPTH_SOURCE_HARDWARE` (default) | the `PIXEL_DEPTH` sensor (`hardware_depth_sensor`) | not loaded | **16 GB** |
| `DEPTH_SOURCE_STEREO` | FoundationStereo over `stereo_sensors` | loaded | **24 GB** |
| `DEPTH_SOURCE_AUTO` | the depth sensor if the request has one, otherwise stereo | loaded | **24 GB** |

`DEPTH_SOURCE_UNSPECIFIED` behaves as `AUTO`.

> [!IMPORTANT]
> The default, `DEPTH_SOURCE_HARDWARE`, fits a 16 GB GPU. `STEREO` and `AUTO` need 24 GB: `AUTO`
> loads FoundationStereo at startup even if every request carries a depth image, because it cannot
> know that in advance.

### 3.2. Options

| Field | Default | Per request | Description |
| :--- | :--- | :---: | :--- |
| `intrinsic_runtime` | `intrinsic_runtime` | | Looks up installed assets: the model assets below, and each request's prompt and CAD model. |
| `foundationpose_model`, `foundationstereo_model`, `sam3_model` | `ai.intrinsic.nvidia_pose_estimator.*_models` | | Model weight Data assets, downloaded at startup ([§2.2](#22-deploy)). |
| `sensor_routing.detection_sensor` | `"1"` | | Sensor SAM 3 and FoundationPose run on; all depth is reprojected into its frame. |
| `sensor_routing.depth_source` | `DEPTH_SOURCE_HARDWARE` | | See [§3.1](#31-depth-source-and-gpu-memory). |
| `sensor_routing.hardware_depth_sensor` | first `PIXEL_DEPTH` sensor | | Depth sensor for `HARDWARE` and `AUTO`. |
| `sensor_routing.stereo_sensors` | `"2"`, `"3"` | | Stereo sensors, base first. With three or more, the best horizontal partner for the base is chosen. A non-empty list replaces the default and needs at least two names. |
| `stereo.min_depth_m`, `stereo.max_depth_m` | `0.30`, `0.80` | | Expected working range, used for partner ranking (`min_depth_m`) and disparity pre-shift (both). Set either to `0` to disable pre-shift (`max_depth_m: 0` leaves partner ranking unaffected); a field left out of a `--config` override keeps its default. |
| `stereo.width`, `stereo.height` | `640`, `416` | | FoundationStereo input size, in multiples of 32. The pair is scaled to `width`; if it is then taller than `height`, its bottom rows are cropped and get no depth. A user-supplied `.plan` with a fixed shape overrides both. |
| `stereo.clahe_clip_limit`, `stereo.clahe_detail_boost` | `3.0`, `1.5` | | Contrast enhancement before matching; `0` disables it. |
| `sam3_detection.prompt` | `"object"` | ✓ | Text prompt. |
| `sam3_detection.confidence_threshold` | `0.2` | ✓ | Minimum SAM 3 score, in [0, 1]. |
| `sam3_detection.resolution` | `1008` | | SAM 3 input size; the exported graphs only accept 1008. |
| `sam3_refinement.policy` | `REPLACE_MID_NMS06` | ✓ | `NONE` skips the CAD box-prompt refinement and the second FoundationPose pass. |
| `sam3_refinement.low_miou`, `.high_miou` | `0.2`, `0.8` | ✓ | Proposals whose render-mask IoU falls in this range are replaced by the refined mask. |
| `sam3_refinement.nms_threshold` | `0.6` | ✓ | Mask-NMS threshold after replacement. |
| `foundation_pose.n_refine` | `5` | ✓ | Refinement iterations. |
| `foundation_pose.n_hypotheses` | `252` | ✓ | Pose hypotheses per proposal; must be a multiple of `batch_size`. |
| `foundation_pose.batch_size` | `42` | | TensorRT mini-batch size. |
| `selection.policy` | `SOFT_GLOBAL_V1` | ✓ | `KEEP_ALL` keeps every proposal; `SOFT_GLOBAL_V1` reranks by a weighted score and drops those below `cutoff`. |
| `selection.cutoff` | `3.0` | ✓ | Minimum reranked score. |
| `selection.weight_sam_score`, `.weight_render_mask_iou`, `.weight_render_box_iou` | `2.0`, `4.0`, `1.0` | ✓ | Rerank weights. |
| `selection.fp_score_divisor` | `100.0` | ✓ | Scales the FoundationPose score in the rerank; must be > 0. |
| `debug.artifact_dir` | unset | ✓ | Dumps each request's intermediates ([§2.5](#25-debug-artifacts-and-visualization-publishing)). A per-request value must be under `/tmp`. |
| `debug.publish_visualization` | `true` | ✓ | Publishes the detection and pose overlays to PubSub. |

- Sensor names are the stringified `sensor_config.id` (Orbbec: `"1"` RGB, `"2"`/`"3"` IR, `"4"`
  depth). An unset or unmatched selector falls back to choosing by pixel type.
- The prompt and confidence threshold are taken from the request override first, then the target
  asset's `FoundationPoseEstimatorParams`, then this config.

---

## 4. Tests

```bash
bazel test --config=intrinsic $PKG:all $PKG:nvidia_pose_estimator_service_test
```

`nvidia_pose_estimator_service_test` is tagged `manual` so must be named explicitly alongside
`$PKG:all`.

---

## 5. How the Build Works

Reference only — nothing in this section requires action.

### 5.1. Upstream Sources

Commit-pinned `http_archive` repos declared in `//bazel:non_module_deps.bzl`, with BUILD overlays
in `//intrinsic/production/external`:

| Repo | Upstream |
| :--- | :--- |
| `@foundation_pose_inference_library` | <https://github.com/intrinsic-opensource/foundation-pose-inference-library> |
| `@foundationpose_perception_pipeline` | <https://github.com/intrinsic-opensource/foundationpose_perception_pipeline> |

### 5.2. The FoundationPose C++ Library

`libfoundation_pose_nvidia.so.0.1.0` is built by Bazel from upstream sources. From
`foundation_pose_inference_library.BUILD.bazel`:

- `cuda_library(name = "foundation_pose_cuda")` compiles the three `.cu` translation units through
  `@rules_cuda`.
- `cc_binary(name = "libfoundation_pose_nvidia.so.0.1.0", linkshared = True)` links those with the
  five `.cpp` sources, setting `-Wl,-soname,libfoundation_pose_nvidia.so.1`.
- `:tensorrt_headers` aliases `@tensorrt_headers//:tensorrt_headers`, the Bazel Central Registry
  (BCR) module (`bazel_dep(name = "tensorrt_headers", version = "10.16")` in `MODULE.bazel`)
  packaging the open-source [NVIDIA/TensorRT](https://github.com/NVIDIA/TensorRT) C++ headers
  (v10.16, Apache-2.0). Both `:foundation_pose_cuda` and `:libfoundation_pose_nvidia.so.0.1.0`
  depend on `:tensorrt_headers`, so compiling the library never touches the TensorRT OCI image.
- `libnvinfer.so.10` and `libnvonnxparser.so.10` are satisfied at link time by generated **stubs**
  returning `nullptr`; the real implementations resolve at runtime from the `@tensorrt` base image.
  This keeps the link hermetic without redistributable TensorRT libraries at build time.
- `pkg_tar(name = "nvidia_pose_estimator_fp_library_layer")` — in this package's `BUILD`, stages
  the result into `/usr/lib/x86_64-linux-gnu` with the conventional `.so.1` and `.so` symlinks.

```text
====================================================================================================
                  1. EXTERNAL INPUTS (MODULE.bazel & bazel/non_module_deps.bzl)
====================================================================================================

  @tensorrt_headers                      @foundation_pose_inference_library    @cuda          @libpng
  (BCR module: tensorrt_headers 10.16,   (http_archive from GitHub)            (rules_cuda)   (BCR 1.6.54)
   NVIDIA/TensorRT OSS C++ headers)           │                  │               │       │        │
       │                                      │ 3 .cu files      │ 5 .cpp files  │       │        │
       │ NvInfer*.h, NvOnnxParser.h           │ + .h/.hpp/.cuh   │ + .h/.hpp     │       │        │
       ▼                                      │                  │               │       │        │
==============================================│==================│===============│=======│========│=
  2. TARGETS IN foundation_pose_inference_library.BUILD.bazel                    │       │        │
==============================================│==================│===============│=======│========│=
                                              │                  │               │       │        │
  ┌────────────────────────────────────────┐  │                  │               │       │        │
  │ alias:                                 │  │                  │               │       │        │
  │ :tensorrt_headers                      │  │                  │               │       │        │
  │ actual =                               │  │                  │               │       │        │
  │  "@tensorrt_headers//:tensorrt_headers"│  │                  │               │       │        │
  │ (from BCR)                             │  │                  │               │       │        │
  └─────────┬──────────────────────┬───────┘  │                  │               │       │        │
            │                      │          ▼                  │               ▼       │        │
            │                      │   ┌──────────────────────┐  │ (@cuda//:cuda_headers)│        │
            │                      └──>│ cuda_library:        │<─┼───────────────────────┘        │
            │                          │ :foundation_pose_cuda│  │                                │
            │                          │                      │  │                                │
            │                          │ Compiles with Clang: │  │                                │
            │                          │  • builtin_raster_   │  │                                │
            │                          │    renderer.cu       │  │                                │
            │                          │  • cuda_pipeline.cu  │  │                                │
            │                          │  • tensorrt_runner.cu│  │                                │
            │                          └──────────┬───────────┘  │                                │
            │                                     │              │                                │
            │                       Produces:     │              │                                │
            │                       libfoundation_pose_cuda.lo   │                                │
            │                       (CPU wrappers + GPU code)    │                                │
            │                                     │              │                                │
            │  ┌───────────────────────────────┐  │              │                                │
            │  │ Linker Stubs (for DT_NEEDED): │  │              │                                │
            │  │                               │  │              │                                │
            │  │ write_file(:nvinfer_stub_src) │  │              │                                │
            │  │ &                             │  │              │                                │
            │  │ write_file(:nvonnxparser_...) │  │              │                                │
            │  │                               │  │              │                                │
            │  │ Satisfies -lnvinfer &         │  │              │                                │
            │  │ -lnvonnxparser at link time.  │  │              │                                │
            │  └──────────────┬────────────────┘  │              │                                │
            │                 │                   ▼              ▼                                │
            │                 │  ┌─────────────────────────────────────────────────────────────┐  │
            │                 │  │ cc_binary:                                                  │  │
            │                 │  │ :libfoundation_pose_nvidia.so.0.1.0                         │  │
            │                 │  │                                                             │  │
            │                 │  │ 1. Compiles 5 C++ files -> 5 .pic.o files:                  │  │
            │                 │  │    • c_api.cpp, device_buffer.cpp, foundation_pose.cpp,     │  │
            │                 │  │      mesh.cpp, pipeline_utils.cpp                           │  │
            │                 │  │                                                             │  │
            │                 │  │ 2. Links everything together:                               │  │
            └─────────────────┼─>│    • :tensorrt_headers (@tensorrt_headers//:tensorrt_headers)│  │
                              │  │    • :foundation_pose_cuda                                  │  │
                              │  │    • @cuda//:cuda_runtime_static  <─────────────────────────┼──┘
                              │  │    • @libpng  <─────────────────────────────────────────────┼──┘
                              └──┼──> • :nvinfer_stub & :nvonnxparser_stub                     │
                                 └──────────────────────────────┬──────────────────────────────┘
                                                                │
                                                Outputs:        │
                                                libfoundation_pose_nvidia.so.0.1.0
                                                                │
================================================================│===================================
  3. CONSUMERS IN incode/.../nvidia_pose_estimator/BUILD        │
     (gated on --$PKG:build_full_image=True)                    │
================================================================│===================================
                                                                │
                        ┌───────────────────────────────────────┴─────────────────────────┐
                        ▼                                                                 ▼
  ┌───────────────────────────────────────────────────┐             ┌────────────────────────────────────────┐
  │ pkg_tar:                                          │             │ py_library:                            │
  │ :nvidia_pose_estimator_fp_library_layer           │             │ :nvidia_pose_estimator_env_lib         │
  │                                                   │             │ (data = select(:build_full_image...))  │
  │ Places into container tar layer:                  │             │                                        │
  │  /usr/lib/x86_64-linux-gnu/                       │             │ Makes the .so available in Bazel       │
  │   └── libfoundation_pose_nvidia.so.0.1.0          │             │ runfiles during `bazel test` on host.  │
  │   └── + .so.1 / .so symlinks                      │             └────────────────────────────────────────┘
  └───────────────────────────────────────────────────┘
```

The Bazel build never compiles upstream's optional nvdiffrast backend (CMake option
`FOUNDATION_POSE_WITH_NVDIFFRAST`): nvdiffrast is under the NVIDIA Source Code License rather than
Apache-2.0, and upstream keeps it in a separate `libfoundation_pose_nvdiffrast.so` so the license
boundary falls at the shared-library edge.

### 5.3. Runtime Libraries and Python Bindings

- TensorRT and the CUDA C++ runtime come from the base image `nvcr.io/nvidia/tensorrt:26.05-py3`,
  pinned by digest in `MODULE.bazel`.
- `libpng` (`@libpng`, BCR 1.6.54) is linked directly into `libfoundation_pose_nvidia.so.0.1.0` at
  build time to read mesh textures, so no extra Debian shared-library layer is needed in the
  container image.
- Python bindings come from the pip lock (`tensorrt-cu13-bindings`, `cuda-bindings`,
  `cuda-pathfinder`), pinned in `google3/requirements.in`.
- `tensorrt-cu13-bindings` installs its module as `tensorrt_bindings` while the pipeline imports
  `tensorrt`, so `:tensorrt_shim` generates a two-line re-export.

> The pinned `tensorrt-cu13-bindings` version must match `TRT_VERSION` in the base image (currently
> 10.16.1.11). Do not bump one without the other, and do not move to a base image carrying
> TensorRT 11.x — a major version break invalidates serialized plans and changes the bindings ABI.

### 5.4. Model Weights Fetched by Bazel

sha256-pinned `http_file` targets in `//bazel:non_module_deps.bzl` (gated behind
`--$PKG:download_weights=True`), fetched from NGC's public unauthenticated endpoints (with
HuggingFace fallback mirrors for FoundationPose):

| File | Size | Description |
| :--- | ---: | :--- |
| `refiner_net.onnx` | 68,158,609 B | FoundationPose iterative SE(3) refiner (`nvidia/tao/foundationpose:deployable_v1.0`, NCHW) |
| `score_net.onnx` | 63,910,103 B | FoundationPose hypothesis scoring network (same package) |
| `deployable_foundation_stereo_s_dynamic_v2.0.onnx` | 346,531,163 B | FoundationStereo disparity backbone (`nvidia/tao/foundationstereo`) |

These feed `foundationpose_model_data_asset` and `foundationstereo_model_data_asset`. SAM 3 has no
equivalent target because its weights are gated — see [§1.1](#11-export-the-sam-3-weights).

The staging directory (`intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models`,
or `$NVIDIA_POSE_ESTIMATOR_MODELS_DIR`) is surfaced to Bazel by the `@nvidia_pose_estimator_models`
repository rule in `//bazel:nvidia_pose_estimator_models.bzl`, which symlinks staged files into the
external repo and classifies plans by filename prefix. An absent or incomplete staging directory is
not an error: building `sam3_model_data_asset` logs a warning naming what is missing and skips
building the bundle.

### 5.5. Running Outside a Container

A local, non-container run has no base image to inherit the runtime libraries from, so stage them
by hand and point `FP_INFERENCE_LIB_PATH` at the result:

```bash
STAGE_DIR=/tmp/fp_libs
mkdir -p "${STAGE_DIR}"
CID=$(docker create nvcr.io/nvidia/tensorrt:26.05-py3)

# -L copies the real files rather than dangling symlinks.
for lib in libnvinfer.so.10 libnvonnxparser.so.10 libnvinfer_plugin.so.10 \
           libnvinfer_vc_plugin.so.10; do
  docker cp -L "${CID}:/usr/lib/x86_64-linux-gnu/${lib}" "${STAGE_DIR}/"
done
# libnvrtc dlopens libnvrtc-builtins at JIT time; environment.py preloads both.
for lib in libcudart.so.13 libnvrtc.so.13 libnvrtc-builtins.so; do
  docker cp -L "${CID}:/usr/local/cuda/lib64/${lib}" "${STAGE_DIR}/"
done
docker rm "${CID}" > /dev/null

export FP_INFERENCE_LIB_PATH="${STAGE_DIR}"
```

Builder resources are per-architecture; copy the `libnvinfer_builder_resource*` matching your GPU
as well. libpng and zlib are already present on any normal Linux host.

**Model weights.** Outside a cluster there is no asset registry and no CAS, so `model_asset_utils`
is skipped entirely. Put every weight file in one directory and point `MODELS_DIR` at it:

```bash
export MODELS_DIR=$PWD/intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models
```

Plans are written to `$MODELS_DIR/engine_cache`; there is no version namespacing in this mode, so
delete the directory yourself after swapping an ONNX.
