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

"""Implements gen_http_bridge Bazel rule."""

load("@io_bazel_rules_go//go:def.bzl", "GoInfo")

def _gen_http_bridge_impl(ctx):
    output_path = ctx.actions.declare_file(ctx.label.name + "/main.go")

    openapi_file = ctx.file.openapi_path

    args = ctx.actions.args()
    args.add("httpjson", "generatemain")
    args.add("--openapi_path", openapi_file.path)
    args.add("--output", output_path)

    for go_proto_target, grpc_service in ctx.attr.services.items():
        go_info = go_proto_target[GoInfo]
        service_go_importpath = go_info.importpath
        args.add("--http_service", "%s:%s" % (grpc_service, service_go_importpath))

    ctx.actions.run(
        outputs = [output_path],
        inputs = [openapi_file],
        executable = ctx.executable._inbuild,
        arguments = [args],
        mnemonic = "GenHttpBridge",
        progress_message = "Generating HTTP bridge files for %s" % ctx.label.name,
    )

    return [
        DefaultInfo(files = depset([output_path])),
    ]

# gen_http_bridge calls `inbuild httpjson generatemain` for the intrinsic_http_image Bazel macro.
gen_http_bridge = rule(
    implementation = _gen_http_bridge_impl,
    doc = "Generate files name/main.go needed to build an HTTP bridge service asset",
    attrs = {
        "openapi_path": attr.label(
            mandatory = True,
            allow_single_file = True,
            doc = "The path to an OpenAPI file whose content will be inserted into main.go, and which will be returned from the /openapi.yaml endpoint.",
        ),
        "services": attr.label_keyed_string_dict(
            mandatory = True,
            providers = [GoInfo],
            doc = "A map from go_proto_library targets to their gRPC service FQNs.",
        ),
        "_inbuild": attr.label(
            default = Label("//intrinsic_sdk/intrinsic/tools/inbuild:inbuild"),
            doc = "The inbuild executable.",
            executable = True,
            cfg = "exec",
        ),
    },
)
