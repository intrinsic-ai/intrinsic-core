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

"""Build rules for creating Skill artifacts."""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("@rules_python//python:py_binary.bzl", "py_binary")
load("@rules_python//python:py_info.bzl", "PyInfo")
load("//bazel:cc_macros.bzl", "cc_binary")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")
load("//bazel:container.bzl", "container_image")
load("//bazel:python_oci_image.bzl", "python_oci_image")
load("//intrinsic_sdk/intrinsic/assets/build_defs:asset.bzl", "AssetInfo", "AssetLocalInfo")
load(
    "//intrinsic_sdk/intrinsic/skills/build_defs:manifest.bzl",
    "SkillManifestInfo",
    _skill_manifest = "skill_manifest",
)

skill_manifest = _skill_manifest

# Directory in container where user code is put.
# Use ':' in directory name so that it can't match a Bazel packagage to workaround
# https://github.com/bazelbuild/rules_pkg/issues/905
_SKILL_USER_DIR = "/::skills::"

def _gen_cc_skill_service_main_impl(ctx):
    output_file = ctx.actions.declare_file(ctx.label.name + ".cc")
    file_descriptor_set_out = ctx.actions.declare_file(ctx.label.name + "_augmented_filedescriptor.pbbin")
    manifest_pbbin_out = ctx.actions.declare_file(ctx.label.name + "_augmented_manifest.pbbin")

    manifest_pbbin_file = ctx.attr.manifest[SkillManifestInfo].manifest_binary_file
    file_descriptor_set_file = ctx.attr.manifest[SkillManifestInfo].file_descriptor_set
    deps_headers = []
    for dep in ctx.attr.deps:
        deps_headers += dep[CcInfo].compilation_context.direct_public_headers
    header_paths = [header.short_path for header in deps_headers]

    args = ctx.actions.args().add(
        "--manifest",
        manifest_pbbin_file,
    ).add(
        "--file_descriptor_set",
        file_descriptor_set_file,
    ).add(
        "--out",
        output_file,
    ).add(
        "--manifest_out",
        manifest_pbbin_out,
    ).add(
        "--file_descriptor_set_out",
        file_descriptor_set_out,
    ).add_joined(
        "--cc_headers",
        header_paths,
        join_with = ",",
    ).add(
        "--lang",
        "cpp",
    )

    outputs = [output_file, manifest_pbbin_out, file_descriptor_set_out]
    ctx.actions.run(
        outputs = outputs,
        executable = ctx.executable._skill_service_gen,
        inputs = [manifest_pbbin_file, file_descriptor_set_file],
        arguments = [args],
    )

    return [
        DefaultInfo(files = depset(outputs)),
        SkillManifestInfo(
            manifest_binary_file = manifest_pbbin_out,
            file_descriptor_set = file_descriptor_set_out,
        ),
    ]

_gen_cc_skill_service_main = rule(
    implementation = _gen_cc_skill_service_main_impl,
    doc = "Generates a file containing a main function for a skill's services.",
    attrs = {
        "deps": attr.label_list(
            doc = "The cpp deps for the skill. This is normally the cc_proto_library target for the skill's schema, and the skill cc_library where skill interface is implemented.",
            providers = [CcInfo],
        ),
        "manifest": attr.label(
            mandatory = True,
            providers = [SkillManifestInfo],
        ),
        "_skill_service_gen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/skills/generator:skill_service_generator"),
            doc = "The skill_service_generator executable to invoke for the code generation action.",
            executable = True,
            cfg = "exec",
        ),
    },
)

def _cc_skill_service(name, deps, manifest, **kwargs):
    """Generate a C++ binary that serves a single skill over gRPC.

    Args:
      name: The name of the target.
      deps: The C++ dependencies of the skill service specific to this skill.
            This is normally the cc_proto_library target for the skill's protobuf
            schema and the cc_library target that declares the skill's create method,
            which is specified in the skill's manifest.
      manifest: The manifest target for the skill. Must provide a SkillManifestInfo.
      **kwargs: Extra arguments passed to the cc_binary target for the skill service.
    """
    gen_main_name = "_%s_main" % name
    _gen_cc_skill_service_main(
        name = gen_main_name,
        manifest = manifest,
        deps = deps,
        testonly = kwargs.get("testonly"),
        visibility = ["//visibility:private"],
        tags = ["manual", "avoid_dep"],
    )

    cc_binary(
        name = name,
        srcs = [gen_main_name],
        deps = deps + [
            Label("//intrinsic_sdk/intrinsic/connect/cc/grpc:channel"),
            Label("//intrinsic_sdk/intrinsic/skills/internal:runtime_data"),
            Label("//intrinsic_sdk/intrinsic/skills/internal:single_skill_factory"),
            Label("//intrinsic_sdk/intrinsic/skills/internal:skill_init"),
            Label("//intrinsic_sdk/intrinsic/skills/internal:skill_service_config_utils"),
            Label("//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic"),
            Label("//intrinsic/stats:opencensus"),  
            Label("//intrinsic_sdk/intrinsic/util/status:status_specs"),
            Label("@abseil-cpp//absl/flags:flag"),
            Label("@abseil-cpp//absl/log:check"),
            Label("@abseil-cpp//absl/time"),
            # This is needed when using grpc_cli.
            Label("@com_github_grpc_grpc//:grpc++_reflection"),
        ],
        **kwargs
    )

def _gen_py_skill_service_main_impl(ctx):
    output_file = ctx.actions.declare_file(ctx.label.name + ".py")
    file_descriptor_set_out = ctx.actions.declare_file(ctx.label.name + "_augmented_filedescriptor.pbbin")
    manifest_pbbin_out = ctx.actions.declare_file(ctx.label.name + "_augmented_manifest.pbbin")

    manifest_pbbin_file = ctx.attr.manifest[SkillManifestInfo].manifest_binary_file
    file_descriptor_set_file = ctx.attr.manifest[SkillManifestInfo].file_descriptor_set

    args = ctx.actions.args().add(
        "--manifest",
        manifest_pbbin_file,
    ).add(
        "--file_descriptor_set",
        file_descriptor_set_file,
    ).add(
        "--out",
        output_file,
    ).add(
        "--manifest_out",
        manifest_pbbin_out,
    ).add(
        "--file_descriptor_set_out",
        file_descriptor_set_out,
    ).add(
        "--lang",
        "python",
    )

    outputs = [output_file, manifest_pbbin_out, file_descriptor_set_out]
    ctx.actions.run(
        outputs = outputs,
        executable = ctx.executable._skill_service_gen,
        inputs = [manifest_pbbin_file, file_descriptor_set_file],
        arguments = [args],
    )

    return [
        DefaultInfo(files = depset(outputs)),
        SkillManifestInfo(
            manifest_binary_file = manifest_pbbin_out,
            file_descriptor_set = file_descriptor_set_out,
        ),
    ]

_gen_py_skill_service_main = rule(
    implementation = _gen_py_skill_service_main_impl,
    doc = "Generates a file containing a main function for a skill's services.",
    attrs = {
        "deps": attr.label_list(
            doc = "The python deps for the skill. This is normally the py_proto_library target for the skill's schema, and the skill py_library where skill interface is implemented.",
            providers = [PyInfo],
        ),
        "manifest": attr.label(
            mandatory = True,
            providers = [SkillManifestInfo],
        ),
        "_skill_service_gen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/skills/generator:skill_service_generator"),
            doc = "The skill_service_generator executable to invoke for the code generation action.",
            executable = True,
            cfg = "exec",
        ),
    },
)

def _py_skill_service(name, deps, manifest, **kwargs):
    """Generate a Python binary that serves a single skill over gRPC.

    Args:
      name: The name of the target.
      deps: The Python dependencies of the skill service specific to this skill.
            This is normally the py_proto_library target for the skill's protobuf
            schema and the py_library target that declares the skill's create method.
      manifest: The manifest target for the skill. Must provide a SkillManifestInfo.
      **kwargs: Extra arguments passed to the py_binary target for the skill service.
    """
    gen_main_name = "_%s_main" % name
    _gen_py_skill_service_main(
        name = gen_main_name,
        manifest = manifest,
        deps = deps,
        testonly = kwargs.get("testonly"),
        visibility = ["//visibility:private"],
        tags = ["manual", "avoid_dep"],
    )

    py_binary(
        name = name,
        srcs = [gen_main_name],
        main = gen_main_name + ".py",
        deps = deps + [
            Label("//intrinsic_sdk/intrinsic/skills/internal:runtime_data_py"),
            Label("//intrinsic_sdk/intrinsic/skills/internal:single_skill_factory_py"),
            Label("//intrinsic_sdk/intrinsic/skills/internal:skill_init_py"),
            Label("//intrinsic_sdk/intrinsic/skills/internal:skill_service_config_utils_py"),
            Label("//intrinsic_sdk/intrinsic/skills/generator:app"),
            Label("//intrinsic_sdk/intrinsic/util/status:status_specs_py"),
            Label("@com_google_absl_py//absl/flags"),
            Label("@intrinsic_apis//intrinsic/skills/proto:skill_service_config_py_pb2"),
        ],
        **kwargs
    )

def _skill_service_config_manifest_impl(ctx):
    manifest_pbbin_file = ctx.attr.manifest[SkillManifestInfo].manifest_binary_file
    proto_desc_fileset_file = ctx.attr.manifest[SkillManifestInfo].file_descriptor_set
    outputfile = ctx.actions.declare_file(ctx.label.name + ".pbbin")

    arguments = ctx.actions.args().add(
        "--manifest_pbbin_filename",
        manifest_pbbin_file,
    ).add(
        "--proto_descriptor_filename",
        proto_desc_fileset_file,
    ).add(
        "--output_config_filename",
        outputfile,
    )
    ctx.actions.run(
        outputs = [outputfile],
        executable = ctx.executable._skill_service_config_gen,
        inputs = [manifest_pbbin_file, proto_desc_fileset_file],
        arguments = [arguments],
    )

    return DefaultInfo(
        files = depset([outputfile]),
        runfiles = ctx.runfiles(files = [outputfile]),
    )

_skill_service_config_manifest = rule(
    implementation = _skill_service_config_manifest_impl,
    attrs = {
        "manifest": attr.label(
            mandatory = True,
            providers = [SkillManifestInfo],
        ),
        "_skill_service_config_gen": attr.label(
            executable = True,
            default = Label("//intrinsic_sdk/intrinsic/skills/build_defs:skillserviceconfiggen_main"),
            cfg = "exec",
        ),
    },
)

SkillInfo = provider(
    "provided by intrinsic_skill() rule",
    fields = ["bundle_tar"],
)

def _intrinsic_skill_rule_impl(ctx):
    image_files = ctx.attr.image.files.to_list()
    if len(image_files) != 1:
        fail("image does not contain exactly 1 tar file")
    manifest = ctx.attr.manifest[SkillManifestInfo].manifest_binary_file
    fds = ctx.attr.manifest[SkillManifestInfo].file_descriptor_set

    inputs = depset([manifest, fds], transitive = [ctx.attr.image.files])
    bundle_output = ctx.outputs.bundle_out

    args = ctx.actions.args().add(
        "--manifest",
        manifest,
    ).add(
        "--image_tar",
        image_files[0],
    ).add(
        "--file_descriptor_set",
        fds,
    ).add(
        "--output_bundle",
        bundle_output,
    )

    ctx.actions.run(
        inputs = inputs,
        outputs = [bundle_output],
        executable = ctx.executable._skillgen,
        arguments = [args],
        mnemonic = "Skillbundle",
        progress_message = "Skill bundle %s" % bundle_output.short_path,
    )

    asset_info_output = ctx.actions.declare_file(ctx.label.name + ".asset_info.binpb")
    local_info_args = ctx.actions.args().add(
        "--manifest",
        manifest,
    ).add(
        "--asset_type",
        "ASSET_TYPE_SKILL",
    ).add(
        "--file_descriptor_set",
        fds,
    ).add(
        "--output_asset_info",
        asset_info_output,
    )
    ctx.actions.run(
        inputs = depset([manifest, fds]),
        outputs = [asset_info_output],
        executable = ctx.executable._assetlocalinfogen,
        arguments = [local_info_args],
        mnemonic = "AssetLocalInfo",
        progress_message = "Writing asset info %{output} for %{label}",
    )

    return [
        DefaultInfo(
            executable = bundle_output,
            runfiles = ctx.runfiles(
                transitive_files = inputs,
            ),
        ),
        SkillInfo(
            bundle_tar = bundle_output,
        ),
        AssetInfo(
            asset_info = asset_info_output,
        ),
        AssetLocalInfo(
            bundle_path = bundle_output,
        ),
    ]

_intrinsic_skill_rule = rule(
    implementation = _intrinsic_skill_rule_impl,
    attrs = {
        "image": attr.label(
            mandatory = True,
            allow_single_file = [".tar"],
            doc = "The image tarball of the skill.",
        ),
        "manifest": attr.label(
            mandatory = True,
            providers = [SkillManifestInfo],
        ),
        "_assetlocalinfogen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/assets/build_defs:assetlocalinfogen"),
            cfg = "exec",
            executable = True,
        ),
        "_skillgen": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/skills/build_defs:skillgen_main"),
            cfg = "exec",
            executable = True,
        ),
    },
    outputs = {
        "bundle_out": "%{name}.bundle.tar",
    },
    provides = [SkillInfo, AssetInfo, AssetLocalInfo],
)

def _intrinsic_skill(name, image, manifest, **kwargs):
    """Creates a skill bundle.

    Generates the following targets:
    * a skill container image target named 'name'.

    Args:
      name: The name of the skill to build
      image: Skill service image.
      manifest: A target that provides a SkillManifestInfo provider for the skill. This is normally
                a skill_manifest() target.
      **kwargs: additional arguments passed to the container_image rule, such as visibility.
    """

    # TODO(b/399032827): Remove this once all consumers have been removed.
    image_name = "%s_image" % name
    container_image(
        name = image_name,
        base = image,
        **kwargs
    )

    _intrinsic_skill_rule(
        name = name,
        image = image_name + ".tar",
        manifest = manifest,
        visibility = kwargs.get("visibility"),
        testonly = kwargs.get("testonly"),
    )

def cc_skill(
        name,
        deps,
        manifest,
        base_image = None,
        malloc = None,
        **kwargs):
    """Creates cpp skill targets.

    Generates the following targets:
    * a skill container image target named 'name'.

    Args:
      name: The name of the skill to build
      deps: The C++ dependencies of the skill service specific to this skill.
            This is normally the cc_proto_library target for the skill's protobuf
            schema and the cc_library target that declares the skill's create method,
            which is specified in the skill's manifest.
      manifest: A target that provides a SkillManifestInfo provider for the skill. This is normally
                a skill_manifest() target.
      base_image: The base container_image target to use for the skill service image.
      malloc: Override the default dependency on malloc. Optional. Refer to the cc_binary docs for
              more information.
      **kwargs: additional arguments passed to the container_image rule, such as visibility.
    """
    binary_name = "_%s_binary" % name
    _cc_skill_service(
        name = binary_name,
        deps = deps,
        manifest = manifest,
        testonly = kwargs.get("testonly"),
        malloc = malloc,
        visibility = ["//visibility:private"],
        tags = ["manual", "avoid_dep"],
    )

    # This is the label of the target that generates the augmented manifest.
    augmented_manifest_provider_target = ":_%s_main" % binary_name

    skill_service_config_name = "_%s_skill_service_config" % name
    _skill_service_config_manifest(
        name = skill_service_config_name,
        manifest = augmented_manifest_provider_target,
        testonly = kwargs.get("testonly"),
        visibility = ["//visibility:private"],
        tags = ["manual", "avoid_dep"],
    )

    service_image_name = "_%s_service_image" % name
    cc_oci_image(
        name = service_image_name,
        base = base_image,
        binary = binary_name,
        directory = _SKILL_USER_DIR,
        data_path = "/",
        files = [
            skill_service_config_name,
        ],
        symlinks = {
            "/skills/skill_service": paths.join(_SKILL_USER_DIR, native.package_name(), binary_name),
            "/skills/skill_service_config.proto.bin": paths.join(_SKILL_USER_DIR, native.package_name(), skill_service_config_name + ".pbbin"),
        },
        workdir = "/",
        compatible_with = kwargs.get("compatible_with"),
        visibility = ["//visibility:private"],
        testonly = kwargs.get("testonly"),
    )

    _intrinsic_skill(
        name = name,
        image = service_image_name,
        manifest = augmented_manifest_provider_target,
        **kwargs
    )

def py_skill(
        name,
        manifest,
        deps,
        base_image = None,
        **kwargs):
    """Creates python skill targets.

    Generates the following targets:
    * a skill container image target named 'name'.

    Args:
      name: The name of the skill to build
      manifest: A target that provides a SkillManifestInfo provider for the skill. This is normally
                a skill_manifest() target.
      deps: The Python library dependencies of the skill. This is normally at least the python
            proto library for the skill and the skill implementation.
      base_image: The base container_image target to use for the skill service image.
      **kwargs: additional arguments passed to the container_image rule, such as visibility.
    """
    binary_name = "_%s_binary" % name
    _py_skill_service(
        name = binary_name,
        deps = deps,
        manifest = manifest,
        python_version = "PY3",
        testonly = kwargs.get("testonly"),
        visibility = ["//visibility:private"],
        tags = ["manual", "avoid_dep"],
    )

    # This is the label of the target that generates the augmented manifest.
    augmented_manifest_provider_target = ":_%s_main" % binary_name

    skill_service_config_name = "_%s_skill_service_config" % name
    _skill_service_config_manifest(
        name = skill_service_config_name,
        manifest = augmented_manifest_provider_target,
        testonly = kwargs.get("testonly"),
        visibility = ["//visibility:private"],
        tags = ["manual", "avoid_dep"],
    )

    service_image_name = "_%s_service_image" % name
    python_oci_image(
        name = service_image_name,
        base = base_image,
        binary = binary_name,
        directory = _SKILL_USER_DIR,
        data_path = "/",
        files = [
            skill_service_config_name,
        ],
        symlinks = {
            "/skills/skill_service": paths.join(_SKILL_USER_DIR, native.package_name(), binary_name),
            # TODO(b/379017440): Support this in a nicer way, presumably directly in python_oci_image.
            "/skills/skill_service.runfiles": paths.join(_SKILL_USER_DIR, native.repo_name(), native.package_name(), binary_name + ".runfiles"),
            "/skills/skill_service_config.proto.bin": paths.join(_SKILL_USER_DIR, native.package_name(), skill_service_config_name + ".pbbin"),
        },
        workdir = "/",
        compatible_with = kwargs.get("compatible_with"),
        visibility = ["//visibility:private"],
        testonly = kwargs.get("testonly"),
    )

    _intrinsic_skill(
        name = name,
        image = service_image_name,
        manifest = augmented_manifest_provider_target,
        **kwargs
    )
