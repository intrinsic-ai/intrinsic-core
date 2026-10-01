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

"""
This module contains build rules to construct helm charts for Intrinsic applications.
"""

load("@bazel_lib//lib:stamping.bzl", "STAMP_ATTRS", "maybe_stamp")
load("//intrinsic/util/path_resolver:paths.bzl", "WRAPPER_HEADER", "to_rlocation_path")
load("//intrinsic/util/wrapped_executable:wrapped_executable.bzl", "wrapped_binary")

HelmChartInfo = provider(
    doc = "Details of the helm chart created by helm_chart()",
    fields = {
        "chart": "The compressed archive (tgz) for the chart",
        "chart_assignment": "The yaml file defining the chart",
        "image_list": "A text file with the images that must be pushed for the chart",
    },
)

def _check_image_usage(ctx, name, image_check, templates = []):
    """Test if the image is used in the yaml templates."""

    # TODO(ensonic): the output is not nice, it would be nicer to fail('images', error_message)
    template_files = ""
    if templates:
        template_files = " ".join([t.path for t in templates])
    else:
        template_files = "/dev/null"
    images = ctx.attr.images or {}

    cmd = "missing=\"\"; mkdir -p $(dirname {out});".format(out = image_check.path)
    for value in images.keys():
        cmd += "egrep -q '.Values.images.{image}[ }}]' {templates} || missing=\"$missing {image}\";".format(image = value, templates = template_files)
    if ctx.attr.error_on_unused_images:
        cmd += """
if [[ -n "$missing" ]]; then
    echo -e >&2 "\\e[1m\\e[31mERROR:\\e[0m Unused image declarations, check templates: $missing";
    echo $missing >{out};
    exit 1;
else
    touch {out};
fi
    """.format(out = image_check.path)
    else:
        cmd += """
if [[ -n "$missing" ]]; then
    echo $missing >>{out};
else
    touch {out};
fi
    """.format(out = image_check.path)
    ctx.actions.run_shell(
        inputs = templates,
        outputs = [image_check],
        command = cmd,
        mnemonic = "CheckImageUsage",
        progress_message = "Checking image usage for {}".format(name),
    )

# This function executes digester to find the Docker image's digest.
def _assemble_image_digest(ctx, image, output_digest):
    ctx.actions.run(
        outputs = [output_digest],
        inputs = [image],
        executable = ctx.executable._digester,
        arguments = [
            "--dst=%s" % output_digest.path,
            "--tarball=%s" % image.path,
        ],
        mnemonic = "ImageDigest",
        progress_message = "Extracting image digest",
        toolchain = None,
    )

def assemble_image_list(
        ctx,
        images,
        out_file):
    """Writes a file with a list of images to push.

    Args:
      ctx: The context from the invoking rule.
      images: Dict. dict from Image tarballs to image name.
      out_file: file. The file to write the list of images to push to.

    Returns:
      Dict. dict from image name to a (digest file, image tarball) tuple for the
      internal images.
    """
    if not images:
        ctx.actions.write(out_file, "")
        return {}

    cmd = ""
    path_digest_files = []
    image_digests = {}
    for image_name, tar_target in images.items():
        files = tar_target.files.to_list()
        if len(files) != 1:
            fail("internal error: expected one tarball per listed image")
        tar_file = files[0]

        digest_file = ctx.actions.declare_file("{}-{}-digest".format(ctx.label.name, tar_file.short_path.replace("/", "-")))
        _assemble_image_digest(ctx, tar_file, digest_file)
        image_digests[image_name] = (digest_file, tar_file)

        # We want to create a file with image name, path to the tarball, and the digest. However,
        # the digest is written to a _file_ in the previous step. Starlark is not supposed to do file IO.
        # Hence, we construct the final output file line-by-line using those temporary files containing
        # tarball path and digest and `cat` them in the end.
        path_digest_file = ctx.actions.declare_file("{}-{}-path-digest-file".format(ctx.label.name, image_name))

        # TODO(freese): inline once this is no longer run conditionally
        # internally, but unconditionally externally.
        def write_file_action():
            ctx.actions.run(
                mnemonic = "ImageList",
                executable = ctx.executable._imagelistgen,
                outputs = [path_digest_file],
                inputs = [digest_file],
                arguments = [
                    "-image",
                    image_name,
                    "-tarball",
                    tar_file.short_path,
                    "-digest",
                    digest_file.path,
                    "-output",
                    path_digest_file.path,
                ],
                toolchain = None,
            )

        write_file_action()
        path_digest_files.append(path_digest_file)
        cmd += "cat \"{input}\" >> '{out}'\n".format(input = path_digest_file.path, out = out_file.path)

    ctx.actions.run_shell(
        mnemonic = "AssembleImageList",
        outputs = [out_file],
        inputs = path_digest_files,
        command = cmd,
    )

    return image_digests

def _build_helm_chart(ctx, name, chart, files, templates, values, global_values, common_templates, out):
    """Starlark function that builds a helm chart.

    Args:
      ctx: The context from the invoking rule.
      name: string. Must match the name in Chart.yaml.
      chart: file. The Chart.yaml file.
      files: list of non-template files to put in files/.
      templates: list of template files.
      values: file. The values.yaml file.
      global_values: file. global-values.yaml.template, a Helm template that can mutate $.Values
        before it is used by other templates.
      common_templates: list of common template files automatically injected into all charts.
      out: file. The file that the chart is built to.
    """

    cmd = """
      set -o errexit
      mkdir -p {name} {name}/templates/global-values
      cp {chart} {name}/Chart.yaml
      cp {values} {name}/values.yaml
      """.format(name = name, chart = chart.path, values = values.path)

    # global-values.yaml is copied in at a deeper level than other templates so that Helm executes
    # it first. An example of the sort order for Helm v2.17 is shown here:
    #   https://github.com/helm/helm/blob/v2.17.0/pkg/engine/engine_test.go#L31
    # and it's "explained" here:
    #   https://github.com/helm/helm/blob/v2.17.0/pkg/engine/engine.go#L255
    # (calling it zzzzz-global-values.yaml would also work)
    cmd += "cp {global_values} {name}/templates/global-values/global-values.yaml\n".format(name = name, global_values = global_values.path)
    for t in common_templates:
        cmd += "cp {path} {name}/templates/global-values/{filename}\n".format(
            filename = t.basename,
            name = name,
            path = t.path,
        )

    if templates:
        template_files = " ".join([t.path for t in templates])

        # Use a single cp invocation to detect filename clashes.
        cmd += "cp {templates} {name}/templates\n".format(name = name, templates = template_files)
    if files:
        cmd += "mkdir {name}/files\n".format(name = name)
        files_locations = " ".join([f.path for f in files])

        # Check files are not too large, otherwise the resulting chart may be
        # bigger than the max Kubernetes resource size (1MiB). Add blank lines
        # before and after the message to make it stand out from the unhelpful
        # Bazel error output.
        cmd += r"""
too_big="$(find -L {files} -size +512k | sed 's/^/    /')"
if [[ -n "$too_big" ]]; then
    echo -e >&2 "\n\e[1m\e[31mERROR:\e[0m helm_chart() was given files larger than 512KiB:"
    echo -e >&2 "$too_big"
    echo -e >&2 "Contact intrinsic-cloud@ for ideas on using large configuration files.\n"
    exit 1
fi
        """.format(files = files_locations)

        # Use a single cp invocation to detect filename clashes.
        cmd += "cp {files} {name}/files\n".format(name = name, files = files_locations)

    # Since the local filesystem is inaccessible from a run_shell we defer
    # linting to runtime (i.e. lint.sh).
    # Note: the extra tar flags are use to make builds deterministic, see
    #  go/bazel-nondeterminism
    cmd += """
      tar --owner=root --group=root --numeric-owner --mtime="2010-01-01" --create {name} \
      | gzip --no-name > {output};
      rm -rf {name}""".format(name = name, output = out.path)
    ctx.actions.run_shell(
        mnemonic = "HelmChart",
        inputs = [chart, values, global_values] + common_templates + templates + (files or []),
        outputs = [out],
        command = cmd,
    )

# Warning: app_start.go and lint.sh both hardcode the -0.0.1.yaml or -0.0.1.tgz
# suffix, so they'll need updating if you change this.
_DEFAULT_VERSION = "0.0.1"

def _lfs_paths():
    paths = []

    return paths

def _impl(ctx):
    # Deny a list of image names which are extremely generic and could cause problems if two charts
    # happen to have an image with the same name, were our disambiguation logic to fail.
    image_name_denylist = ["service"]
    for denylist_entry in image_name_denylist:
        if denylist_entry in ctx.attr.images:
            fail("Image name '%s' is not allowed." % denylist_entry)

    chart_yaml = ctx.actions.declare_file(ctx.label.name + "-chart.yaml")
    chart = ctx.actions.declare_file("{}-{}.tgz".format(ctx.label.name, _DEFAULT_VERSION))
    chart_assignment = ctx.actions.declare_file("{}-{}.yaml".format(ctx.label.name, _DEFAULT_VERSION))
    image_list = ctx.actions.declare_file(ctx.label.name + ".image_list.txt")
    image_check = ctx.actions.declare_file(ctx.label.name + ".image_check")
    templates = ctx.files.templates

    action_inputs = [chart]
    ctx.actions.expand_template(
        template = ctx.file._chart_yaml_template,
        output = chart_yaml,
        substitutions = {"${name}": ctx.label.name, "${version}": _DEFAULT_VERSION},
    )

    _check_image_usage(
        ctx,
        name = ctx.label.name,
        image_check = image_check,
        templates = templates,
    )

    images = ctx.attr.images or {}

    image_deps = images.values()

    digests = assemble_image_list(
        ctx,
        images,
        image_list,
    )

    values_yaml = ctx.actions.declare_file(ctx.label.name + "-values.yaml")
    values = ctx.files.values
    values_paths = [value.path for value in values]
    ctx.actions.run(
        outputs = [values_yaml],
        inputs = values + [v[0] for v in digests.values()] + ctx.files._common_values,
        executable = ctx.executable._valuesgen,
        arguments = [
            "-output=%s" % values_yaml.path,
            "-common_values=%s" % ctx.file._common_values.path,
        ] + [
            "-values=%s" % value
            for value in values_paths
        ] + [
            "-image={name}%{digest_file}%{image_tarball}".format(
                name = k,
                digest_file = v[0].path,
                image_tarball = v[1].short_path,
            )
            for k, v in digests.items()
        ],
        mnemonic = "BuildValues",
        progress_message = values_yaml.short_path,
        toolchain = None,
    )

    _build_helm_chart(
        ctx,
        name = ctx.label.name,
        chart = chart_yaml,
        values = values_yaml,
        templates = templates,
        files = ctx.files.files,
        global_values = ctx.file._global_values_template,
        common_templates = ctx.files._common_templates,
        out = chart,
    )

    if ctx.label.repo_name:
        build_target = "@%s//%s:%s" % (ctx.label.repo_name, ctx.label.package, ctx.label.name)
    else:
        build_target = "//%s:%s" % (ctx.label.package, ctx.label.name)
    ca_gen_args = [
        "-output=%s" % chart_assignment.path,
        "-build_target=%s" % build_target,
        "-chart=%s" % chart.path,
        "-chart_name=%s" % ctx.label.name,
        "-image_list=%s" % image_list.short_path,
    ] + [
        "-embed_chart_file=%s" % f[HelmChartInfo].chart_assignment.short_path
        for f in ctx.attr.embed
    ] + [
        "-lfs_path=%s" % p
        for p in _lfs_paths()
    ]

    stamp = maybe_stamp(ctx)
    if stamp:
        action_inputs.append(stamp.stable_status_file)
        cmd = """
        VERSION=$( (grep "^BUILD_EMBED_LABEL " "{info_file}" || true) | cut -d' ' -f2- )
        if [[ -z "$VERSION" ]]; then
            VERSION="local-build"
        fi
        "{tool}" -version="$VERSION" {args}
        """.format(
            info_file = stamp.stable_status_file.path,
            tool = ctx.executable._chartassignmentgen.path,
            args = " ".join(ca_gen_args),
        )
    else:
        # Completely bypasses info_file dependency - guarantees 100% local cache hits!
        cmd = """
        "{tool}" -version="local-build" {args}
        """.format(
            tool = ctx.executable._chartassignmentgen.path,
            args = " ".join(ca_gen_args),
        )

    ctx.actions.run_shell(
        outputs = [chart_assignment],
        inputs = action_inputs,
        tools = [ctx.executable._chartassignmentgen],
        command = cmd,
        mnemonic = "ChartAssignment",
        progress_message = "ChartAssignment %s" % chart_assignment.short_path,
        toolchain = None,
    )

    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(
        out,
        is_executable = True,
        content = """{header}
exec "$(rlocation "{target}")" start "{chart}" "{chart_assignment}" "{image_list}" "$@"
""".format(
            header = WRAPPER_HEADER,
            target = to_rlocation_path(ctx, ctx.executable._chart_executable),
            chart = to_rlocation_path(ctx, chart),
            chart_assignment = to_rlocation_path(ctx, chart_assignment),
            image_list = to_rlocation_path(ctx, image_list),
        ),
    )

    return [
        # Both DefaultInfo and HelmChartInfo are needed. The DefaultInfo is
        # used to provide basic file information to FilesetEntry and helm_chart.
        # HelmChartInfo is used by workcell_spec to get the specific files it
        # needs, since there are multiple provided in DefaultInfo.files.
        DefaultInfo(
            files = depset([out, chart, chart_assignment, image_list, values_yaml]),
            executable = out,
            runfiles = ctx.runfiles(
                files = [chart, chart_assignment, image_list],
                transitive_files = depset(
                    transitive = [
                        image[DefaultInfo].files
                        for image in image_deps
                    ],
                ),
            ).merge_all(
                [
                    ctx.attr._runfiles_dep[DefaultInfo].default_runfiles,
                    ctx.attr._chart_executable[DefaultInfo].default_runfiles,
                ] + [
                    embed_deps[DefaultInfo].default_runfiles
                    for embed_deps in ctx.attr.embed
                ],
            ),
        ),
        HelmChartInfo(
            chart = chart,
            chart_assignment = chart_assignment,
            image_list = image_list,
        ),
        OutputGroupInfo(_validation = depset([image_check])),
    ]

_helm_chart_backend = rule(
    executable = True,
    implementation = _impl,
    attrs = {
        # TODO(b/326921974): Remove once default charts have been migrated to intrinsic-base.
        "embed": attr.label_list(
            allow_empty = True,
            doc = "Embed other charts into the values of this chart.",
            providers = [HelmChartInfo],
        ),
        # TODO(b/206874651): Remove flag
        "error_on_unused_images": attr.bool(
            default = True,
            doc = """Set to false if the image / template usage check should
                     not result in an error. This can be useful if an image
                     referenced is defined in values.yaml. Normally, this
                     should be set to false.""",
        ),
        "files": attr.label_list(
            allow_empty = True,
            allow_files = True,
            default = [],
            doc = "Extra non-template files for the chart's files/ directory.",
        ),
        "images": attr.string_keyed_label_dict(
            allow_empty = True,
            allow_files = True,
            doc = "Image tarballs referenced by the chart.",
        ),
        "templates": attr.label_list(
            allow_empty = True,
            allow_files = True,
            default = [],
            doc = "Files for the chart's templates/ directory.",
        ),
        "values": attr.label(
            allow_single_file = True,
            doc = "The values.yaml file.",
        ),
        "_chart_executable": attr.label(
            default = Label("//intrinsic/kubernetes:chart_executable"),
            cfg = "target",
            executable = True,
        ),
        "_chart_yaml_template": attr.label(
            default = Label("//intrinsic/kubernetes:Chart.yaml.template"),
            allow_single_file = True,
        ),
        "_chartassignmentgen": attr.label(
            default = Label("//intrinsic/kubernetes/workcell_spec:chartassignmentgen"),
            cfg = "exec",
            executable = True,
        ),
        "_common_templates": attr.label_list(
            default = [Label("//intrinsic/kubernetes/templates:common_templates")],
            allow_files = True,
            doc = "Common Helm template helpers automatically included in all charts.",
        ),
        "_common_values": attr.label(
            default = Label("//intrinsic/kubernetes:common-values.yaml"),
            allow_single_file = True,
        ),
        "_digester": attr.label(
            default = Label("//intrinsic/kubernetes/workcell_spec:digester"),
            cfg = "exec",
            executable = True,
        ),
        "_global_values_template": attr.label(
            default = Label("//intrinsic/kubernetes:global_values_template"),
            allow_single_file = True,
        ),
        "_imagelistgen": attr.label(
            default = Label("//intrinsic/kubernetes/workcell_spec:imagelistgen"),
            cfg = "exec",
            executable = True,
        ),
        "_runfiles_dep": attr.label(
            default = Label("@rules_shell//shell/runfiles"),
        ),
        "_valuesgen": attr.label(
            default = Label("//intrinsic/kubernetes/workcell_spec:valuesgen"),
            cfg = "exec",
            executable = True,
        ),
    } | STAMP_ATTRS,
)

def _helm_chart_release_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(
        out,
        content = """{header}
exec "$(rlocation "{target}")" release "{chart_assignment}" "$@"
""".format(
            header = WRAPPER_HEADER,
            target = to_rlocation_path(ctx, ctx.executable._chart_executable),
            chart_assignment = to_rlocation_path(ctx, ctx.attr.chart[HelmChartInfo].chart_assignment),
        ),
        is_executable = True,
    )
    return [
        DefaultInfo(
            executable = out,
            runfiles = ctx.runfiles(
                files = [out],
            ).merge_all([
                ctx.attr._runfiles_dep[DefaultInfo].default_runfiles,
                ctx.attr._chart_executable[DefaultInfo].default_runfiles,
                ctx.attr.chart[DefaultInfo].default_runfiles,
            ]),
        ),
    ]

helm_chart_release = rule(
    implementation = _helm_chart_release_impl,
    attrs = {
        "chart": attr.label(
            mandatory = True,
            providers = [HelmChartInfo],
        ),
        "_chart_executable": attr.label(
            default = Label("//intrinsic/kubernetes:chart_executable"),
            cfg = "target",
            executable = True,
        ),
        "_runfiles_dep": attr.label(
            default = Label("@rules_shell//shell/runfiles"),
        ),
    },
    executable = True,
)

def helm_chart(
        name,
        values = None,
        templates = None,
        files = None,
        images = None,
        error_on_unused_images = None,  # TODO(b/206874651): Remove flag
        embed = None,  # TODO(b/326921974): Remove once default charts have been migrated to intrinsic-base.
        lint_test = True,
        **kwargs):
    """Macro for a standard Intrinsic helm chart.

    Args:
      name: string name for the chart.
      values: file. The values.yaml file.
      templates: list of files. Files for the chart's templates/ directory.
      files: list of files. Extra non-template files for the chart's files/ directory.
      images: dict. Images referenced by the chart.
      error_on_unused_images: Set to false if the image / template usage check should
                              not result in an error. This can be useful if an image
                              referenced is defined in values.yaml. Normally, this
                              should be set to false.
      embed: list of label. Embed other charts into the values of this chart. This must only be used by intrinsic-base and will be removed in b/326921974.
      lint_test: bool. Whether to automatically generate a helm_chart_lint_test for this chart. Defaults to True.
      **kwargs: Additional arguments to pass to all of the generated targets.
    """

    # TODO(b/326921974): Remove once default charts have been migrated to intrinsic-base.
    if embed and name != "intrinsic-base":
        fail("embed is only supported for intrinsic-base")

    images = images or {}

    _helm_chart_backend(
        name = name,
        values = values,
        templates = templates,
        files = files,
        images = images,
        error_on_unused_images = error_on_unused_images,
        embed = embed,
        **kwargs
    )

    wrapped_binary(
        name = name + ".stop",
        target = Label("//intrinsic/kubernetes:chart_executable"),
        args = ["stop", str(native.package_relative_label(name))],
        # This target is only intended to be used by human users. Do not depend on it from other targets.
        visibility = ["//visibility:private"],
    )

    if lint_test:
        helm_chart_lint_test(
            name = name + "_helm_lint_test",
            chart = ":" + name,
            tags = kwargs.get("tags"),
            visibility = kwargs.get("visibility"),
        )

def _helm_chart_lint_test_impl(ctx):
    if HelmChartInfo in ctx.attr.chart:
        chart_file = ctx.attr.chart[HelmChartInfo].chart
    else:
        tgz_files = [f for f in ctx.files.chart if f.path.endswith(".tgz")]
        if not tgz_files:
            fail("Expected chart target to provide HelmChartInfo or a .tgz file in DefaultInfo.files")
        chart_file = tgz_files[0]
    lint_tool = ctx.attr._lint_tool[DefaultInfo].files_to_run.executable
    lint_env = ctx.attr._lint_tool[RunEnvironmentInfo].environment

    values_file = ctx.actions.declare_file(ctx.label.name + "_lint_values.yaml")
    ctx.actions.write(
        output = values_file,
        content = "{%s}" % ctx.attr.values,
    )

    script = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.write(
        output = script,
        content = """{header}
{exports}

exec $(rlocation "{lint_tool}") $(rlocation "{chart}") {context} $(rlocation "{values}")
""".format(
            header = WRAPPER_HEADER,
            exports = "\n".join(['export %s="%s"' % (key, value) for key, value in lint_env.items()]),
            lint_tool = to_rlocation_path(ctx, lint_tool),
            chart = to_rlocation_path(ctx, chart_file),
            context = ctx.attr.context,
            values = to_rlocation_path(ctx, values_file),
        ),
        is_executable = True,
    )

    # Do not include ctx.attr.chart[DefaultInfo].default_runfiles here because
    # the chart target transitively includes all container image tarballs in
    # its runfiles. Staging those tarballs into the test environment adds
    # significant overhead, and the linter only needs the chart archive itself.
    runfiles_files = [values_file, chart_file]
    return [
        DefaultInfo(
            executable = script,
            runfiles = ctx.runfiles(
                files = runfiles_files,
            ).merge_all([
                ctx.attr._runfiles_dep[DefaultInfo].default_runfiles,
                ctx.attr._lint_tool[DefaultInfo].default_runfiles,
            ]),
        ),
    ]

_helm_chart_lint_test = rule(
    implementation = _helm_chart_lint_test_impl,
    test = True,
    doc = """Creates a test that utilizes the helm binary to check the validity
             of a helm chart target""",
    attrs = {
        "chart": attr.label(
            doc = "The helm chart to be linted",
            allow_files = True,
        ),
        "context": attr.string(
            doc = "The kube-context parameter that should be passed to helm",
            default = "minikube",
        ),
        "values": attr.string(
            doc = """An alternative to values_file. A string with Helm values to
                     lint with. The provided string will be written to a file (with
                     wrapping) and then passed to the lint test.  This cannot be
                     used with values_file""",
        ),
        "_lint_tool": attr.label(
            default = Label("//intrinsic/kubernetes:lint"),
            executable = True,
            cfg = "exec",
        ),
        "_runfiles_dep": attr.label(
            default = Label("@rules_shell//shell/runfiles"),
        ),
    },
)

def helm_chart_lint_test(name, chart, context = None, values = None, **kwargs):
    """Macro for creating a Helm chart lint test target."""
    _helm_chart_lint_test(
        name = name,
        chart = chart,
        context = context,
        values = values,
        tags = kwargs.get("tags"),
        testonly = kwargs.get("testonly"),
        visibility = kwargs.get("visibility"),
    )
