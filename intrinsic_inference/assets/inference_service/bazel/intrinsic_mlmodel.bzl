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

"""Bazel rules and macros for packaging ML models as Intrinsic data assets."""

load("@bazel_lib//lib:paths.bzl", "relative_file")
load("//intrinsic_sdk/intrinsic/assets/data/build_defs:data.bzl", "intrinsic_data")

def _intrinsic_mlmodel_manifest_impl(ctx):
    output_file = ctx.actions.declare_file(ctx.label.name + ".manifest.textproto")

    args = ctx.actions.args()
    args.add("--config", ctx.file.config)
    args.add("--backend", ctx.attr.backend)
    args.add("--id", ctx.attr.id)
    args.add("--display_name", ctx.attr.display_name)
    args.add("--description", ctx.attr.description)
    if ctx.attr.source_project:
        args.add("--source_project", ctx.attr.source_project)
    if ctx.attr.vendor:
        args.add("--vendor", ctx.attr.vendor)
    args.add("--output", output_file)

    if ctx.attr.version:
        args.add("--version", ctx.attr.version)

    inputs = [ctx.file.config]

    # process local bazel files
    for label, target_path in ctx.attr.local_model_files.items():
        files = label.files.to_list()
        if len(files) != 1:
            fail("local_model_files keys must resolve to a single file, got %s for %s" % (files, target_path))
        f = files[0]
        inputs.append(f)

        # Compute the relative path from the generated manifest to the model file
        rel_path = relative_file(f.short_path, output_file.short_path)

        args.add("--model_file", "%s:file://%s" % (target_path, rel_path))

    # process remote references
    for uri, target_path in ctx.attr.remote_model_files.items():
        args.add("--model_file", "%s:%s" % (target_path, uri))

    ctx.actions.run(
        inputs = inputs,
        outputs = [output_file],
        executable = ctx.executable._generator,
        arguments = [args],
        mnemonic = "MLModelManifestGen",
        progress_message = "Generating MLModel manifest for %s" % ctx.label.name,
    )

    return [DefaultInfo(files = depset([output_file]))]

_intrinsic_mlmodel_manifest = rule(
    implementation = _intrinsic_mlmodel_manifest_impl,
    attrs = {
        "backend": attr.string(
            default = "triton",
            values = ["triton"],
            doc = "The backend type.",
        ),
        "config": attr.label(
            allow_single_file = True,
            mandatory = True,
            doc = "Model backend config file.",
        ),
        "description": attr.string(
            mandatory = True,
            doc = "Documentation describing what this model does.",
        ),
        "display_name": attr.string(
            mandatory = True,
            doc = "Human-readable display name for the model.",
        ),
        "id": attr.string(
            mandatory = True,
            doc = "The unique asset ID, e.g. 'ai.intrinsic.my_model'.",
        ),
        "local_model_files": attr.label_keyed_string_dict(
            allow_empty = True,
            allow_files = True,
        ),
        "remote_model_files": attr.string_dict(
            allow_empty = True,
        ),
        "source_project": attr.string(
            mandatory = False,
            doc = "File references (intcas://) must be uploaded to this project.",
        ),
        "vendor": attr.string(
            default = "Intrinsic",
            doc = "The vendor of the model.",
        ),
        "version": attr.string(
            default = "0.0.1",
            doc = "The model version.",
        ),
        "_generator": attr.label(
            default = Label("//intrinsic_inference/assets/inference_service/bazel:mlmodel_manifest_generator"),
            cfg = "exec",
            executable = True,
        ),
    },
)

def intrinsic_mlmodel(
        name,
        id,
        display_name,
        description,
        config,
        model_files,
        source_project = None,
        backend = "triton",
        vendor = "Intrinsic",
        version = "0.0.1",
        **kwargs):
    """Packages a ML model as an Intrinsic Data Asset.

    This macro automatically parses the serialized backend config, generates
    the platform-agnostic MLModel configuration, wraps it into an Intrinsic
    Data Asset manifest that is built using the intrinsic_data rule.

    Args:
        name: The target name.
        id: The unique asset ID (e.g., 'ai.intrinsic.my_model').
        display_name: Human-readable display name.
        description: Documentation description.
        config: The backend config.pbtxt file.
        model_files: A dictionary mapping source files to their target paths in
          the model directory (e.g., { "model.py": "1/model.py" }).
        backend: The backend name used to select the config file parser.
        source_project: File references (intcas://) must be uploaded to this project.
          Required when intcas:// file references are used.
        vendor: The vendor name (defaults to 'Intrinsic').
        version: The version string (defaults to '0.0.1').
        **kwargs: Additional arguments forwarded to intrinsic_data.
    """
    manifest_name = name + "_manifest"

    local_model_files = {}
    remote_model_files = {}
    for src, target_path in model_files.items():
        if src.startswith("file://"):
            local_model_files[src[len("file://"):]] = target_path
        elif "://" in src:
            remote_model_files[src] = target_path
        else:
            local_model_files[src] = target_path

    for src in remote_model_files:
        if src.startswith("intcas://") and not source_project:
            fail("In target '%s': 'source_project' must be specified when intcas:// file references are used." % name)

    _intrinsic_mlmodel_manifest(
        name = manifest_name,
        id = id,
        display_name = display_name,
        description = description,
        vendor = vendor,
        version = version,
        config = config,
        local_model_files = local_model_files,
        remote_model_files = remote_model_files,
        backend = backend,
        source_project = source_project,
    )

    # Collect only local source files that need to be packaged via intrinsic_data
    data_files = [config] + list(local_model_files.keys())

    user_deps = kwargs.pop("deps", [])
    default_deps = [
        Label("//intrinsic_inference/assets/inference_service/v1:ml_model_asset_proto"),
    ]
    if backend == "triton":
        default_deps.append(Label("@triton-inference-server-common//protobuf:common_proto"))

    intrinsic_data(
        name = name,
        manifest = ":" + manifest_name,
        data = data_files,
        deps = default_deps + user_deps,
        **kwargs
    )
