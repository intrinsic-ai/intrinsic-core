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

"""Bazel-compatible versions of aspect-based Flatbuffer rules"""

load("@bazel_skylib//lib:dicts.bzl", "dicts")
load("@bazel_skylib//lib:paths.bzl", "paths")
load("@bazel_skylib//lib:types.bzl", "types")
load("@bazel_tools//tools/cpp:toolchain_utils.bzl", "find_cpp_toolchain", "use_cpp_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")
load("@rules_python//python:py_info.bzl", "PyInfo")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:migration_locations.bzl", "GOOGLE3_OR_IOC_LOCATIONS")  

def _init_flatbuffers_info(*, direct_sources = [], direct_schemas = [], transitive_sources = depset(), transitive_schemas = depset()):
    """_init_flatbuffers_info is a public constructor for FlatBuffersInfo."""
    if not types.is_list(direct_sources):
        fail("direct_sources must be a list (got %s)" % type(direct_sources))

    if not types.is_list(direct_schemas):
        fail("direct_schemas must be a list (got %s)" % type(direct_schemas))

    if not types.is_depset(transitive_sources):
        fail("transitive_sources must be a depset (got %s)" % type(transitive_sources))

    if not types.is_depset(transitive_schemas):
        fail("transitive_schemas must be a depset (got %s)" % type(transitive_schemas))

    return {
        "direct_schemas": direct_schemas,
        "direct_sources": direct_sources,
        "transitive_schemas": transitive_schemas,
        "transitive_sources": transitive_sources,
    }

FlatBuffersInfo, _ = provider(
    doc = "Encapsulates information provided by flatbuffers_library.",
    fields = {
        "direct_schemas": "The binary serialized schema files (i.e. .bfbs) of the direct sources.",
        "direct_sources": "FlatBuffers sources (i.e. .fbs) from the \"srcs\" attribute that contain text-based schema.",
        "transitive_schemas": "A set of binary serialized schema files (i.e. .bfbs) for this and all its dependent FlatBuffers targets.",
        "transitive_sources": "FlatBuffers sources (i.e. .fbs) for this and all its dependent FlatBuffers targets.",
    },
    init = _init_flatbuffers_info,
)

def _create_flatbuffers_info(*, srcs, schemas, deps = None):
    deps = deps or []
    return FlatBuffersInfo(
        direct_schemas = schemas,
        direct_sources = srcs,
        transitive_schemas = depset(
            direct = schemas,
            transitive = [dep[FlatBuffersInfo].transitive_schemas for dep in deps],
        ),
        transitive_sources = depset(
            direct = srcs,
            transitive = [dep[FlatBuffersInfo].transitive_sources for dep in deps],
        ),
    )

def _merge_flatbuffers_infos(infos):
    return FlatBuffersInfo(
        direct_sources = [src for info in infos for src in info.direct_sources],
        transitive_schemas = depset(
            transitive = [info.transitive_schemas for info in infos],
        ),
        transitive_sources = depset(
            transitive = [info.transitive_sources for info in infos],
        ),
    )

def _init_flatbuffers_binary_info(*, src, deps = []):
    return {
        "schema": _merge_flatbuffers_infos([dep[FlatBuffersInfo] for dep in deps]),
        "src": src,
    }

FlatBuffersBinaryInfo, _flatbuffers_binary_info = provider(
    doc = "Provides information about a binary FlatBuffers file.",
    fields = {
        "schema": "A FlatBuffersInfo with the schema for the file.",
        "src": "A binary FlatBuffers file.",
    },
    init = _init_flatbuffers_binary_info,
)

_flatc = {
    "_flatc": attr.label(
        cfg = "exec",
        default = Label("@com_github_google_flatbuffers//:flatc"),
        executable = True,
    ),
}

def _emit_compile(*, ctx, srcs, deps = None):
    """Emits an action that triggers the compilation of the provided .fbs files.

    Args:
        ctx: Starlark context that is used to emit actions.
        srcs: a list of .fbs files to compile.
        deps: an optional list of targets that provide FlatBuffersInfo.

    Returns:
        FlatBuffersInfo that contains the result of compiling srcs.
    """
    deps = deps or []
    transitive_sources = depset(
        direct = srcs,
        transitive = [dep[FlatBuffersInfo].transitive_sources for dep in deps],
    )

    generated_schemas = []
    for src in srcs:
        schema = ctx.actions.declare_file(paths.replace_extension(src.basename, "") + ".bfbs")
        generated_schemas.append(schema)

    args = ctx.actions.args()
    args.add("--binary")
    args.add("--schema")
    args.add("-I", paths.join(".", ctx.label.workspace_root))
    args.add("-I", ".")


    # Add each Google3 and IOC root location as an include directory (`-I`) so `flatc`
    # can resolve cross-location schema imports (e.g. `include "intrinsic/..."`) consistently.
    for location in GOOGLE3_OR_IOC_LOCATIONS:
        args.add("-I", location)
        if ctx.label.workspace_root:
            args.add("-I", paths.join(".", ctx.label.workspace_root, location))


    args.add("-I", ctx.label.package)
    args.add("-I", ctx.bin_dir.path)
    args.add("-I", paths.join(ctx.bin_dir.path, ctx.label.workspace_root))
    args.add("-I", ctx.genfiles_dir.path)
    args.add("-o", paths.join(ctx.bin_dir.path, ctx.label.workspace_root, ctx.label.package))
    args.add_all(srcs)

    ctx.actions.run(
        arguments = [args],
        executable = ctx.executable._flatc,
        inputs = transitive_sources,
        outputs = generated_schemas,
        progress_message = "Generating schemas for {0}".format(ctx.label),
    )

    return _create_flatbuffers_info(
        srcs = srcs,
        schemas = generated_schemas,
        deps = deps,
    )

def _flatbuffers_library_impl(ctx):
    flatbuffers_info = _emit_compile(
        srcs = ctx.files.srcs,
        ctx = ctx,
        deps = ctx.attr.deps,
    )

    return [
        flatbuffers_info,
        DefaultInfo(
            files = depset(flatbuffers_info.direct_schemas),
            runfiles = ctx.runfiles(files = flatbuffers_info.direct_schemas),
        ),
    ]

flatbuffers_library = rule(
    attrs = dicts.add(
        {
            "deps": attr.label_list(
                providers = [FlatBuffersInfo],
            ),
            "srcs": attr.label_list(
                allow_files = [".fbs"],
            ),
        },
        _flatc,
    ),
    doc = """\
Use `flatbuffers_library` to define libraries of FlatBuffers which may be used from multiple
languages. A `flatbuffers_library` may be used in `deps` of language-specific rules, such as
`cc_flatbuffers_library`.

A `flatbuffers_library` can also be used in `data` for any supported target. In this case, the
binary serialized schema (i.e. `.bfbs`) for files directly mentioned by a `flatbuffers_library`
target will be provided to the target at runtime.

The code should be organized in the following way:

-  one `flatbuffers_library` target per `.fbs` file;
-  a file named `foo.fbs` should be the only source for a target named `foo_fbs`, which is located
   in the same package;
-  a `[language]_flatbuffers_library` that wraps a `flatbuffers_library` named `foo_fbs` should be
   called `foo_[language]_fbs`, and be located in the same package.

Example:

```build
load("//intrinsic_sdk/intrinsic/platform:flatbuffers.bzl", "cc_flatbuffers_library", "flatbuffers_library")

flatbuffers_library(
    name = "bar_fbs",
    srcs = ["bar.fbs"],
)

flatbuffers_library(
    name = "foo_fbs",
    srcs = ["foo.fbs"],
    deps = [":bar_fbs"],
)

cc_flatbuffers_library(
    name = "foo_cc_fbs",
    deps = [":foo_fbs"],
)
```

The following rules provide language-specific implementation of FlatBuffers:

-  `cc_flatbuffers_library`
-  `cc_lite_flatbuffers_library`""",
    provides = [FlatBuffersInfo],
    implementation = _flatbuffers_library_impl,
)

def _cc_flatbuffers_aspect_impl(target, ctx):
    if CcInfo in target:  # target already provides CcInfo.
        return []

    filename_suffix = ("." + ctx.attr._suffix if ctx.attr._suffix else "") + ".fbs"

    hdrs = [
        ctx.actions.declare_file(paths.replace_extension(src.basename, "") + filename_suffix + ".h")
        for src in target[FlatBuffersInfo].direct_sources
    ]

    args = ctx.actions.args()
    args.add("-c")
    args.add("-I", paths.join(".", ctx.label.workspace_root))
    args.add("-I", ".")


    # Add each Google3 and IOC root location as an include directory (`-I`) so `flatc`
    # can resolve cross-location schema imports (e.g. `include "intrinsic/..."`) consistently.
    for location in GOOGLE3_OR_IOC_LOCATIONS:
        args.add("-I", location)
        if ctx.label.workspace_root:
            args.add("-I", paths.join(".", ctx.label.workspace_root, location))


    args.add("-I", ctx.label.package)
    args.add("-I", ctx.bin_dir.path)
    args.add("-I", paths.join(ctx.bin_dir.path, ctx.label.workspace_root))
    args.add("-I", ctx.genfiles_dir.path)
    args.add("-o", paths.join(ctx.bin_dir.path, ctx.label.workspace_root, ctx.label.package))
    args.add("--filename-suffix", filename_suffix)
    args.add("--cpp-std", "C++17")
    args.add("--no-union-value-namespacing")
    args.add("--keep-prefix")
    args.add("--cpp-static-reflection")

    if ctx.attr._generate_object_api:
        args.add("--gen-object-api")
    if ctx.attr._generate_compare:
        args.add("--gen-compare")
    if ctx.attr._generate_mutable:
        args.add("--gen-mutable")
    if ctx.attr._generate_reflection:
        args.add("--reflect-names")

    args.add_all(target[FlatBuffersInfo].direct_sources)

    ctx.actions.run(
        arguments = [args],
        executable = ctx.executable._flatc,
        inputs = target[FlatBuffersInfo].transitive_sources,
        outputs = hdrs,
        progress_message = "Generating FlatBuffers C++ code for {0}".format(ctx.label),
    )

    cc_toolchain = find_cpp_toolchain(ctx)
    feature_configuration = cc_common.configure_features(
        cc_toolchain = cc_toolchain,
        ctx = ctx,
        requested_features = ctx.features + ["parse_headers"],
        unsupported_features = ctx.disabled_features,
    )

    deps = getattr(ctx.rule.attr, "deps", [])
    compilation_contexts = [dep[CcInfo].compilation_context for dep in [ctx.attr._runtime, ctx.attr._flatbuffers_headers] + deps if CcInfo in dep]

    compile_kwargs = {}


    # Strip the root location prefix so generated C++ headers can be included using
    # consistent paths (e.g. `#include "intrinsic/..."`) across Google3 and IOC roots.
    pkg = ctx.label.package
    for location in GOOGLE3_OR_IOC_LOCATIONS:
        if pkg.startswith(location + "/") or pkg == location:
            compile_kwargs["strip_include_prefix"] = "/" + location
            break



    compilation_context, _ = cc_common.compile(
        name = ctx.label.name + ("_" + ctx.attr._suffix if ctx.attr._suffix else ""),
        actions = ctx.actions,
        cc_toolchain = cc_toolchain,
        compilation_contexts = compilation_contexts,
        feature_configuration = feature_configuration,
        public_hdrs = hdrs,
        **compile_kwargs
    )

    return [
        CcInfo(
            compilation_context = compilation_context,
        ),
        DefaultInfo(
            files = depset(hdrs),
        ),
    ]

def _make_cc_flatbuffers_aspect(*, suffix = "", generate_object_api, generate_compare, generate_mutable, generate_reflection):
    """Create a new cc_flatbuffers_aspect.

    Args:
        suffix: str, an optional suffix used in all targets that this aspect is applied to.
        generate_object_api: bool, whether to generate additional object-based API.
        generate_compare: bool, whether to generate operator== for object-based API types.
        generate_mutable: bool, whether to generate accessors that can mutate buffers in-place.
        generate_reflection: bool, whether to add minimal type/name reflection.

    Return:
        A new cc_flatbuffers_aspect with the provided configuration.
    """
    return aspect(
        attr_aspects = ["deps"],
        attrs = dicts.add({
            "_flatbuffers_headers": attr.label(
                default = Label("@com_github_google_flatbuffers//:flatbuffers"),
                providers = [CcInfo],
            ),
            "_generate_compare": attr.bool(
                default = generate_compare,
                doc = "Whether to generate operator== for object-based API types.",
            ),
            "_generate_mutable": attr.bool(
                default = generate_mutable,
                doc = "Whether to generate accessors that can mutate buffers in-place.",
            ),
            "_generate_object_api": attr.bool(
                default = generate_object_api,
                doc = "Whether to generate additional object-based API.",
            ),
            "_generate_reflection": attr.bool(
                default = generate_reflection,
                doc = "Whether to add minimal type/name reflection.",
            ),
            "_runtime": attr.label(
                default = Label("@com_github_google_flatbuffers//:runtime_cc"),
                providers = [CcInfo],
            ),
            "_suffix": attr.string(
                default = suffix,
                doc = "An optional suffix used in all targets that this aspect is applied to.",
            ),
        }, _flatc),
        fragments = ["google_cpp", "cpp"],
        required_aspect_providers = [FlatBuffersInfo],
        required_providers = [FlatBuffersInfo],
        toolchains = use_cpp_toolchain(),
        implementation = _cc_flatbuffers_aspect_impl,
    )

_cc_flatbuffers_aspect = _make_cc_flatbuffers_aspect(
    generate_compare = True,
    generate_mutable = True,
    generate_object_api = True,
    generate_reflection = True,
)

_cc_lite_flatbuffers_aspect = _make_cc_flatbuffers_aspect(
    generate_compare = False,
    generate_mutable = False,
    generate_object_api = False,
    generate_reflection = False,
    suffix = "lite",
)

def _cc_flatbuffers_library_impl(ctx):
    # Collect CcInfo for compilation from all dependencies
    direct_cc_infos = [dep[CcInfo] for dep in ctx.attr.deps]

    # Collect the generated headers from the new DefaultInfo provider
    all_files = []
    for dep in ctx.attr.deps:
        if DefaultInfo in dep:
            all_files.append(dep[DefaultInfo].files)

    return [
        # Return the merged CcInfo for downstream compilation
        cc_common.merge_cc_infos(
            direct_cc_infos = direct_cc_infos,
        ),
        # Return a merged DefaultInfo to make headers available as outputs
        DefaultInfo(files = depset(transitive = all_files)),
    ]

def _make_cc_flatbuffers_library(*, doc, aspect):
    return rule(
        attrs = {
            "deps": attr.label_list(
                aspects = [aspect],
                providers = [FlatBuffersInfo],
            ),
        },
        doc = doc,
        implementation = _cc_flatbuffers_library_impl,
    )

_cc_flatbuffers_library = _make_cc_flatbuffers_library(
    aspect = _cc_flatbuffers_aspect,
    doc = """\
`cc_flatbuffers_library` generates C++ code from `.fbs` files.

`cc_flatbuffers_library` does:

-  generate additional object-based API;
-  generate operator== for object-based API types;
-  generate accessors that can mutate buffers in-place;
-  add minimal type/name reflection.

If those things aren't needed, consider using `cc_lite_flatbuffers_library` instead.

Example:

```build
load("//intrinsic/platform/flatbuffers:flatbuffers.bzl", "cc_flatbuffers_library", "flatbuffers_library")

flatbuffers_library(
    name = "foo_fbs",
    srcs = ["foo.fbs"],
)

cc_flatbuffers_library(
    name = "foo_cc_fbs",
    deps = [":foo_fbs"],
)

# An example library that uses the generated C++ code from `foo_cc_fbs`.
cc_library(
    name = "foo_user",
    hdrs = ["foo_user.h"],
    deps = [":foo_cc_fbs"],
)
```

Where `foo_user.h` would include the generated code via: `#include "path/to/project/foo.fbs.h"`.
""",
)

def cc_flatbuffers_library(name, deps, **kwargs):
    _cc_flatbuffers_library(
        name = name + "_internal",
        deps = deps,
        **kwargs
    )
    cc_library(
        name = name,
        hdrs = [name + "_internal"],
        deps = [name + "_internal"],
        **kwargs
    )

_cc_lite_flatbuffers_library = _make_cc_flatbuffers_library(
    aspect = _cc_lite_flatbuffers_aspect,
    doc = """\
`cc_lite_flatbuffers_library` generates C++ code from `.fbs` files.

Unlike `cc_flatbuffers_library`, `cc_lite_flatbuffers_library` does NOT:

-  generate additional object-based API;
-  generate operator== for object-based API types;
-  generate accessors that can mutate buffers in-place;
-  add minimal type/name reflection.

Example:

```build
load("//intrinsic/platform/flatbuffers:flatbuffers.bzl", "cc_lite_flatbuffers_library", "flatbuffers_library")

flatbuffers_library(
    name = "foo_fbs",
    srcs = ["foo.fbs"],
)

cc_lite_flatbuffers_library(
    name = "foo_cc_fbs",
    deps = [":foo_fbs"],
)

# An example library that uses the generated C++ code from `foo_cc_fbs`.
cc_library(
    name = "foo_user",
    hdrs = ["foo_user.h"],
    deps = [":foo_cc_fbs"],
)
```

Where `foo_user.h` would include the generated code via: `#include "path/to/project/foo.lite.fbs.h"`.
```""",
)

# TODO b/477580650: Remove after folders moved
def cc_lite_flatbuffers_library(name, deps, **kwargs):
    _cc_lite_flatbuffers_library(
        name = name + "_internal",
        deps = deps,
        **kwargs
    )
    cc_library(
        name = name,
        hdrs = [name + "_internal"],
        deps = [name + "_internal"],
        **kwargs
    )

############
## Python ##
############

_PyFlatbuffersInfo = provider(
    doc = "An internal provider used to pass information from py_flatbuffers_aspect to py_flatbuffers_library.",
    fields = {
        "direct_py_sources": "A list of all .py files generated for this flatbuffers_library.",
        "direct_pyi_sources": "A list of all .pyi files generated for this flatbuffers_library.",
        "transitive_py_sources": "A transitive closure of all .py files generated for this library all all its dependencies.",
        "transitive_pyi_sources": "A transitive closure of all .pyi files generated for this library all all its dependencies.",
    },
)

def _py_flatbuffers_aspect_impl(target, ctx):
    if PyInfo in target:  # target already provides PyInfo.
        return []

    py_srcs = [
        ctx.actions.declare_file(paths.replace_extension(src.basename, "_fbs.py"))
        for src in target[FlatBuffersInfo].direct_sources
    ]

    args = ctx.actions.args()
    args.add("-c")
    args.add("-I", paths.join(".", ctx.label.workspace_root))
    args.add("-I", ".")


    # Add each Google3 and IOC root location as an include directory (`-I`) so `flatc`
    # can resolve cross-location schema imports (e.g. `include "intrinsic/..."`) consistently.
    for location in GOOGLE3_OR_IOC_LOCATIONS:
        args.add("-I", location)
        if ctx.label.workspace_root:
            args.add("-I", paths.join(".", ctx.label.workspace_root, location))


    args.add("-I", ctx.label.package)
    args.add("-I", ctx.bin_dir.path)
    args.add("-I", paths.join(ctx.bin_dir.path, ctx.label.workspace_root))
    args.add("-I", ctx.genfiles_dir.path)
    args.add("-o", paths.join(ctx.bin_dir.path, ctx.label.workspace_root, ctx.label.package))
    args.add("--gen-onefile")
    args.add("--gen-compare")
    args.add("--gen-object-api")
    args.add("--filename-suffix", "_fbs")
    args.add("--python")
    args.add("--python-typing")

    args.add("--python-version", "3")
    args.add("--no-python-gen-numpy")

    args.add_all(target[FlatBuffersInfo].direct_sources)

    ctx.actions.run(
        arguments = [args],
        executable = ctx.executable._flatc,
        inputs = target[FlatBuffersInfo].transitive_sources,
        outputs = py_srcs,
        progress_message = "Generating FlatBuffers Python code for {0}".format(ctx.label),
    )

    deps = getattr(ctx.rule.attr, "deps", [])

    transitive_py_sources = []

    for dep in deps:
        transitive_py_sources.append(dep[_PyFlatbuffersInfo].transitive_py_sources)

    for dep in ctx.attr._deps:
        transitive_py_sources.append(dep[PyInfo].transitive_sources)

    return [_PyFlatbuffersInfo(
        direct_py_sources = py_srcs,
        transitive_py_sources = depset(
            direct = py_srcs,
            transitive = transitive_py_sources,
        ),
    )]

_py_flatbuffers_aspect = aspect(
    attr_aspects = ["deps"],
    attrs = dicts.add(
        {
            "_deps": attr.label_list(
                default = [Label("@com_github_google_flatbuffers//python/flatbuffers")],
                providers = [PyInfo],
            ),
        },
        _flatc,
    ),
    implementation = _py_flatbuffers_aspect_impl,
)

def _py_flatbuffers_library_impl(ctx):
    if len(ctx.attr.deps) != 1:
        fail("deps requires exactly one target (got %d)" % len(ctx.attr.deps))

    py_flatbuffers_info = ctx.attr.deps[0][_PyFlatbuffersInfo]
    return [
        PyInfo(
            transitive_sources = py_flatbuffers_info.transitive_py_sources,
        ),
        DefaultInfo(
            files = py_flatbuffers_info.transitive_py_sources,
            runfiles = ctx.runfiles(
                collect_default = True,
                transitive_files = py_flatbuffers_info.transitive_py_sources,
            ),
        ),
    ]

py_flatbuffers_library = rule(
    attrs = {
        "deps": attr.label_list(
            aspects = [_py_flatbuffers_aspect],
            providers = [FlatBuffersInfo],
        ),
    },
    doc = """\
`py_flatbuffers_library` generates Python code from `.fbs` files.

Example:

```build
load("//intrinsic/platform/flatbuffers:flatbuffers.bzl", "py_flatbuffers_library", "flatbuffers_library")

flatbuffers_library(
    name = "foo_fbs",
    srcs = ["foo.fbs"],
)

py_flatbuffers_library(
    name = "foo_py_fbs",
    deps = [":foo_fbs"],
)

# An example library that uses the generated Python code from `foo_py_fbs`.
py_library(
    name = "foo_user",
    hdrs = ["foo_user.py"],
    deps = [":foo_py_fbs"],
)
```

Where `foo_user.py` would include the generated code via: `from path.to.project import foo_fbs`.
""",
    provides = [PyInfo],
    implementation = _py_flatbuffers_library_impl,
)
