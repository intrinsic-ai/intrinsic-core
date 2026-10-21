# Copyright 2026 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Repository rule exposing locally staged NVIDIA pose estimator model files.

Exposes SAM 3 ONNX graphs, the BPE vocabulary, and optional precompiled 
TensorRT plans.

This rule symlinks the staging directory into a Bazel repository so the files
can be referenced as labels, which `intrinsic_data` requires.

The staging directory defaults to
`intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models`
within the workspace (overridable via `$NVIDIA_POSE_ESTIMATOR_MODELS_DIR`).

Expected layout:

    intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models/
      sam3_vision_encoder.onnx
      sam3_text_encoder.onnx
      sam3_mask_decoder.onnx
      sam3_box_decoder.onnx
      bpe_simple_vocab_16e6.txt.gz
      engine_cache_bundle/*.plan          # optional
      engine_cache_bundle/*.plan.json     # optional

An absent directory will result in warnings and no asset getting built.
"""

_ENV_VAR = "NVIDIA_POSE_ESTIMATOR_MODELS_DIR"
_DEFAULT_RELPATH = "intrinsic_perception/intrinsic/perception/service/nvidia_pose_estimator/models"

# Required SAM 3 files exported by scripts/prepare_sam3_models.sh.
_SAM3_WEIGHTS = [
    "sam3_vision_encoder.onnx",
    "sam3_text_encoder.onnx",
    "sam3_mask_decoder.onnx",
    "sam3_box_decoder.onnx",
    "bpe_simple_vocab_16e6.txt.gz",
]

_PLAN_SUBDIR = "engine_cache_bundle"

# Maps each model data asset to its TensorRT plan filename prefixes.
_PLAN_OWNERS = [
    ("sam3", ["sam3_"]),
    ("foundationstereo", ["deployable_foundation_stereo_"]),
    ("foundationpose", ["refine_", "score_"]),
]

def _staging_dir(ctx):
    """Returns the configured staging directory."""
    override = ctx.os.environ.get(_ENV_VAR)
    if override:
        return override
    workspace_root = str(ctx.path(Label("//:MODULE.bazel")).dirname)
    return workspace_root + "/" + _DEFAULT_RELPATH

def _list_dir(ctx, path):
    """Returns the filenames directly under `path`, or [] when it is absent."""
    if not ctx.path(path).exists:
        return []

    result = ctx.execute(["ls", "-A", path])
    if result.return_code != 0:
        return []
    return [line for line in result.stdout.splitlines() if line]

def _classify_plans(names):
    """Buckets staged plan filenames by the data asset that owns them."""
    buckets = {owner: [] for owner, _ in _PLAN_OWNERS}
    unclaimed = []
    for name in names:
        if not (name.endswith(".plan") or name.endswith(".plan.json")):
            continue
        owner_found = None
        for owner, prefixes in _PLAN_OWNERS:
            for prefix in prefixes:
                if name.startswith(prefix):
                    owner_found = owner
                    break
            if owner_found:
                break
        if owner_found:
            buckets[owner_found].append(name)
        else:
            unclaimed.append(name)
    return buckets, unclaimed

def _render_list(name, values):
    if not values:
        return "%s = []\n" % name
    lines = ["%s = [" % name]
    for value in sorted(values):
        lines.append('    "%s",' % value)
    lines.append("]\n")
    return "\n".join(lines)

def _nvidia_pose_estimator_models_impl(ctx):
    staging = _staging_dir(ctx)
    if hasattr(ctx, "watch"):
        ctx.watch(staging)
        ctx.watch(staging + "/" + _PLAN_SUBDIR)

    exported = []
    sam3_weights = []
    plan_buckets = {owner: [] for owner, _ in _PLAN_OWNERS}
    unclaimed = []
    missing = ""

    if not ctx.path(staging).exists:
        missing = (
            "staging directory %s does not exist; run scripts/prepare_sam3_models.sh to export the SAM 3 ONNX graphs"
        ) % staging
    else:
        absent = [
            name
            for name in _SAM3_WEIGHTS
            if not ctx.path(staging + "/" + name).exists
        ]
        if absent:
            missing = (
                "staging directory %s is missing %s; run scripts/prepare_sam3_models.sh --force to re-export"
            ) % (staging, ", ".join(absent))
        else:
            for name in _SAM3_WEIGHTS:
                ctx.symlink(staging + "/" + name, name)
                sam3_weights.append(name)
                exported.append(name)

        plan_buckets, unclaimed = _classify_plans(
            _list_dir(ctx, staging + "/" + _PLAN_SUBDIR),
        )
        for names in plan_buckets.values():
            for name in names:
                rel = _PLAN_SUBDIR + "/" + name
                ctx.symlink(staging + "/" + rel, rel)
                exported.append(rel)

    if unclaimed:
        # buildifier: disable=print
        print(
            "nvidia_pose_estimator_models: ignoring staged plan(s) with no known owner prefix: %s" %
            ", ".join(sorted(unclaimed)),
        )

    build = [
        "# Generated by //bazel:nvidia_pose_estimator_models.bzl. Do not edit.",
        'package(default_visibility = ["//visibility:public"])',
        "",
    ]
    if exported:
        build.append("exports_files(%s)" % _render_list("_", exported).split(" = ", 1)[1].strip())
    ctx.file("BUILD.bazel", "\n".join(build) + "\n")

    files_bzl = [
        "# Generated by //bazel:nvidia_pose_estimator_models.bzl. Do not edit.",
        '"""Filenames staged in the models directory, resolved at repository fetch time."""',
        "",
        _render_list("SAM3_WEIGHTS", sam3_weights),
        _render_list("SAM3_PLANS", plan_buckets["sam3"]),
        _render_list("FOUNDATIONPOSE_PLANS", plan_buckets["foundationpose"]),
        _render_list("FOUNDATIONSTEREO_PLANS", plan_buckets["foundationstereo"]),
        'PLAN_SUBDIR = "%s"' % _PLAN_SUBDIR,
        "",
        'SAM3_MISSING_REASON = "%s"' % missing,
        "",
    ]
    ctx.file("files.bzl", "\n".join(files_bzl))

nvidia_pose_estimator_models = repository_rule(
    implementation = _nvidia_pose_estimator_models_impl,
    environ = [_ENV_VAR],
    doc = "Symlinks staged NVIDIA pose estimator model files.",
)
