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

"""Implements intrinsic_http_service Bazel macro."""

load("@bazel_lib//lib:copy_file.bzl", "copy_file")
load("@rules_oci//oci:defs.bzl", "oci_image", "oci_load")
load("@rules_pkg//:mappings.bzl", "pkg_attributes", "pkg_files", "strip_prefix")
load("@rules_pkg//:pkg.bzl", "pkg_tar")
load("//bazel:go_macros.bzl", "go_binary")
load("//intrinsic/httpjson/openapi:protoc_gen_openapi.bzl", "protoc_gen_openapi")
load("//intrinsic/httpjson/private:gen_http_bridge.bzl", _gen_http_bridge = "gen_http_bridge")

_attrs = {
    "go_proto": attr.label(
        default = None,
        doc = "The go_grpc_http_library of the gRPC service (deprecated - use services)",
        configurable = False,
    ),
    "grpc_service": attr.string(
        default = "",
        doc = "The fully qualified name of an annotated gRPC service (deprecated - use services)",
        configurable = False,
    ),
    "openapi_yaml": attr.label(
        default = None,
        allow_single_file = True,
        doc = "An OpenAPI specification file or target (see protoc_gen_openapi)",
        configurable = False,
    ),
    "proto": attr.label(
        default = None,
        doc = "The proto_library target of the gRPC service (deprecated - use openapi_yaml)",
        configurable = False,
    ),
    "services": attr.label_keyed_string_dict(
        default = {},
        doc = "A dictionary mapping go_proto_library targets to gRPC service FQNs",
        configurable = False,
    ),
}

def _intrinsic_http_image_impl(
        name,
        visibility,
        grpc_service,
        proto,
        go_proto,
        services,
        openapi_yaml):
    # Backwards compatibility normalization
    if grpc_service and go_proto:
        services = {go_proto: grpc_service}

    openapi_name = name + "_openapi"

    if proto:
        # Generate openapi.yaml for backwards compatibility
        protoc_gen_openapi(
            name = openapi_name,
            protos = [proto],
        )
        openapi_yaml = ":" + openapi_name
    elif not openapi_yaml:
        fail("Either 'openapi_yaml' or deprecated 'proto' must be specified.")

    gen_name = name + "_generate"
    gobin_name = name + "_gobin"
    binfiles_name = gobin_name + "_files"
    tarbin_name = name + "_tarbin"
    ociimage_name = name + "_ociimage"
    ocitarball_name = name + "_tarball"
    ocitar_name = ocitarball_name + ".tar"

    # Generate main.go using `inbuild httpservice generate`
    _gen_http_bridge(
        name = gen_name,
        services = services,
        openapi_path = openapi_yaml,
    )

    go_binary(
        name = gobin_name,
        srcs = [":" + gen_name],
        embedsrcs = [openapi_yaml],
        deps = list(services.keys()) + [
            Label("//intrinsic/httpjson/openapi:handlers"),
            Label("//intrinsic/httpjson/any"),
            Label("//intrinsic/httpjson/serialization"),
            Label("@intrinsic_apis//intrinsic/resources/proto:runtime_context_go_proto"),
            Label("@org_golang_google_grpc//credentials/insecure"),
            Label("//intrinsic/util/proto:protoio"),
            Label("@org_golang_google_grpc//:grpc"),
            Label("@com_github_grpc_ecosystem_grpc_gateway_v2//runtime"),
        ],
    )

    pkg_files(
        name = binfiles_name,
        srcs = [":" + gobin_name],
        attributes = pkg_attributes(mode = "0555"),  # all: Read + Execute
        prefix = "/opt/intrinsic",
        strip_prefix = strip_prefix.from_pkg(),
        include_runfiles = True,
    )

    pkg_tar(
        name = tarbin_name,
        srcs = [":" + binfiles_name],
        compressor = Label("//bazel:zstd"),
        compressor_args = "-3 -q",
        extension = "tar.zst",
    )

    oci_image(
        name = ociimage_name,
        base = Label("@distroless_base"),
        entrypoint = ["/opt/intrinsic/" + gobin_name],
        tars = [":" + tarbin_name],
    )

    oci_load(
        name = ocitarball_name,
        image = ":" + ociimage_name,
        repo_tags = [ocitarball_name + ":latest"],
    )

    native.filegroup(
        name = ocitar_name,
        srcs = [":" + ocitarball_name],
        output_group = "tarball",
    )

    # Must rename file because intrinsic_service() only looks at an image's basename.
    copy_file(
        name = name,
        src = ":" + ocitar_name,
        out = name + ".tar",
        allow_symlink = True,
        visibility = visibility,
    )

intrinsic_http_image = macro(
    doc = "Generate an OCI image that offers HTTP/JSON endpoints for one or more gRPC services in the same Service Asset.",
    implementation = _intrinsic_http_image_impl,
    attrs = _attrs,
)
