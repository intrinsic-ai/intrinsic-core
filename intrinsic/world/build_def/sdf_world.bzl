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

"""Custom rules for creating World from SDF."""

load("//intrinsic_sdk/bazel:sh_macros.bzl", "sh_test")
load("//bazel:xacro.bzl", "xacro_file")

def _sdf_world_impl(ctx):
    # Determine output files up front.
    if ctx.attr.updates_pbtxts:
        # If updates are provided, then output the gzf that has updates applied to it.
        raw_gzf = ctx.actions.declare_file(ctx.label.name + "__raw.gzf")
        updated_gzf = ctx.actions.declare_file(ctx.label.name + ".gzf")
        output_gzf = updated_gzf
    else:
        # If updates are not provided, then output the raw gzf.
        raw_gzf = ctx.actions.declare_file(ctx.label.name + ".gzf")
        updated_gzf = None
        output_gzf = raw_gzf

    ## Convert full the SDF to GZF.
    raw_gzf_args = ctx.actions.args()
    raw_gzf_args.add("--world_sdf_file", ctx.file.src.path)
    raw_gzf_args.add("--output_world_gz_file", raw_gzf.path)
    if ctx.attr.generate_group_ids:
        raw_gzf_args.add("--generate_group_ids")

    if ctx.attr.large_mesh_checks_max_mesh_diagonal:
        raw_gzf_args.add("--large_mesh_checks_max_mesh_diagonal")

    if ctx.attr.model_resource_names.items():
        raw_gzf_args.add("--models_as_resources", "|".join([k + "," + v for k, v in ctx.attr.model_resource_names.items()]))

    if len(ctx.files.sdf_assets) > 0:
        raw_gzf_args.add("--additional_sdf_assets", "|".join([sdf_asset.path for sdf_asset in ctx.files.sdf_assets]))

    ctx.actions.run(
        outputs = [raw_gzf],
        inputs = [ctx.file.src],
        executable = ctx.file._sdf_to_world,
        arguments = [raw_gzf_args],
        tools = ctx.files.sdf_assets,
        mnemonic = "SdfToWorld",
    )

    ## Apply world updates to the GZF.
    if ctx.attr.updates_pbtxts:
        update_args = ctx.actions.args()
        update_args.add("--world_gzf_filename", raw_gzf.path)
        update_args.add_joined("--input_updates_proto_filenames", ctx.files.updates_pbtxts, join_with = ",")
        update_args.add("--output_world_filename", updated_gzf)
        update_args.add("--sdf_to_object_world_authoring_hints")

        ctx.actions.run(
            outputs = [updated_gzf],
            inputs = [raw_gzf] + ctx.files.updates_pbtxts,
            arguments = [update_args],
            mnemonic = "UpdateWorld",
            executable = ctx.file._update_world,
        )

    validation_outputs = []

    ## Check for object-world compatibility as part of the build chain.
    if ctx.attr.enable_object_world:
        # Since the check_objects tool returns success/fail via exit code, we write a file from the same commandline to signal success. That file is put into the special _validation output group to indicate that it must be built even if it's not depended on. (See https://bazel.build/extending/rules#validation_actions.)
        check_objects_out = ctx.actions.declare_file(ctx.label.name + "_check_objects_out")
        ctx.actions.run_shell(
            inputs = [output_gzf],
            tools = [ctx.file._check_objects],
            outputs = [check_objects_out],
            mnemonic = "CheckObjects",
            command = "%s --nocolor --sdf_to_object_world_authoring_hints --world_gzf_filename %s && echo 'OK' > %s" % (ctx.file._check_objects.path, output_gzf.path, check_objects_out.path),
        )

        validation_outputs = [check_objects_out]

    return [
        DefaultInfo(files = depset([output_gzf]), runfiles = ctx.runfiles(files = [output_gzf])),
        OutputGroupInfo(_validation = depset(validation_outputs)),
    ]

_sdf_world = rule(
    implementation = _sdf_world_impl,
    attrs = {
        "enable_object_world": attr.bool(),
        "generate_group_ids": attr.bool(),
        "large_mesh_checks_max_mesh_diagonal": attr.bool(),
        "model_resource_names": attr.string_dict(
            doc = """
              A dictionary whose keys are model names and whose values are the
              resource names set in the corresponding world objects.
            """,
        ),
        "sdf_assets": attr.label_list(allow_files = True),
        "src": attr.label(allow_single_file = True),
        "updates_pbtxts": attr.label_list(allow_files = True),
        "_check_objects": attr.label(
            default = Label(
                "//intrinsic/world/tools:check_objects",
            ),
            cfg = "exec",
            allow_single_file = True,
            executable = True,
        ),
        "_sdf_to_world": attr.label(
            default = Label("//intrinsic/world/conversion/sdf:sdf_to_world"),
            cfg = "exec",
            allow_single_file = True,
            executable = True,
        ),
        "_update_world": attr.label(
            default = Label("//intrinsic/world/tools:update_world"),
            cfg = "exec",
            allow_single_file = True,
            executable = True,
        ),
    },
)

def sdf_world(
        name,
        src,
        sdf_assets = [],
        updates_pbtxts = None,
        xacro_data = [],
        xacro_args = {},
        enable_object_world = True,
        large_mesh_checks_max_mesh_diagonal = None,
        generate_group_ids = False,  # TODO(b/283374158): Remove this option
        model_resource_names = {},
        compatible_with = [],
        target_compatible_with = [],
        testonly = False,
        visibility = None):
    """Creates World from SDF file and optionally WorldUpdates proto file.

    Args:
      name: The name of the World to be created.
      src: Source SDF world file, may contain xacro macros.
      sdf_assets: Optional. Additional assets needed to convert the SDF.
      updates_pbtxts: list of labels, Optional. Text proto files (supported
        types: intrinsic_proto.world.WorldUpdates,
        intrinsic_proto.world.ObjectWorldUpdates) that specify
        post-processings of the SDF converted World.
      xacro_data: include files for xacro processing
      xacro_args: additional arguments for xacro, in particular arguments
        for conditionals, e.g., 'myflag:=true'.
      enable_object_world: Whether to ensure that the world is compatible with
        the object based-view and, e.g., can be accessed through the object
        world service. This might involve automated world edits and will add a
        test that checks compatibility.
      large_mesh_checks_max_mesh_diagonal: The maximum allowable size for the
        diagonal of any single mesh. The conversion will stop with an error if
        any mesh has a bounding box that exceeds this value. Setting to a value
        < 0 disables these checks completely.
      generate_group_ids: If set, the output world will have group ids generated for the models.
      model_resource_names: A dictionary whose keys are model names and whose
        values are the resource names set in the corresponding world objects.
      compatible_with: The list of environments this target can be built for,
        in addition to default-supported environments.
      target_compatible_with: The list of target platforms that are compatible
        with this target.
      testonly: If true, only testonly targets can depend on this target.
      visibility: Visibility of the build rule.
    """

    if updates_pbtxts and type(updates_pbtxts) != "list":
        fail("Expected updates_pbtxts to be of type list")

    # Create the base world from xacro if input file is a xacro
    if src.endswith(".xacro"):
        xacro_file(
            name = name + "__xacro",
            out = name + ".sdf",
            data = xacro_data,
            testonly = testonly,
            src = src,
            arguments = xacro_args,
            compatible_with = compatible_with,
            target_compatible_with = target_compatible_with,
            visibility = ["//visibility:private"],
        )
        sdf_src = name + ".sdf"
    else:
        if xacro_data or xacro_args:
            fail("xacro_data or xacro_args are provided for a non xacro src.")
        sdf_src = src

    if len(model_resource_names) > 0 and (not testonly):
        fail("'model_resource_names' is only provided for test targets. Otherwise please use intrinsic_resource")

    # Actually generate the gzf.
    _sdf_world(
        name = name,
        src = sdf_src,
        sdf_assets = sdf_assets,
        updates_pbtxts = updates_pbtxts,
        enable_object_world = enable_object_world,
        large_mesh_checks_max_mesh_diagonal = large_mesh_checks_max_mesh_diagonal,
        generate_group_ids = generate_group_ids,
        model_resource_names = model_resource_names,
        compatible_with = compatible_with,
        target_compatible_with = target_compatible_with,
        testonly = testonly,
        visibility = visibility,
    )

    # Create a .gzf target to maintain compatibility with legacy macro callers.
    native.alias(
        name = name + ".gzf",
        actual = name,
        testonly = testonly,
        visibility = visibility,
    )

    # Add a separate unit test that can be used when editing the SDF to run quick object-world
    # compatibility checks with full verbosity.
    if enable_object_world:
        sh_test(
            name = name + "_objects_test",
            srcs = ["//intrinsic/world/tools:check_objects_test.sh"],
            data = [
                "//intrinsic/world/tools:check_objects",
                name,
            ],
            args = [
                "--world_gzf_filename=$(location %s)" % name,
                "--additional_check_objects_args='--sdf_to_object_world_authoring_hints'",
            ],
            compatible_with = compatible_with,
            target_compatible_with = target_compatible_with,
            visibility = ["//visibility:private"],
        )
