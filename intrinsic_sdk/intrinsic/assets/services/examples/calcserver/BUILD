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

load("//bazel:cc_macros.bzl", "cc_binary", "cc_library")
load("//bazel:container.bzl", "container_image")
load("//bazel:python.bzl", "py_binary", "py_library")
load("//bazel:python_oci_image.bzl", "python_oci_image")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

package(default_visibility = [
    "//visibility:public",
])

exports_files(
    srcs = [
        "calc_default_config.textproto",
        "calc_server.proto",
    ],
    visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "calc_server",
    srcs = ["calc_server.cc"],
    hdrs = ["calc_server.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_cc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/log",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)

cc_binary(
    name = "calc_server_main",
    srcs = ["calc_server_main.cc"],
    deps = [
        ":calc_server",
        "//intrinsic_control/intrinsic/icon/release:file_helpers",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/util/proto:any",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)

container_image(
    name = "calculator-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    cmd = ["intrinsic/assets/services/examples/calcserver/calc_server_main"],
    data_path = "/",
    files = [":calc_server_main"],
)

intrinsic_service(
    name = "calculator_service",
    default_config = ":calc_default_config.textproto",
    images = [
        ":calculator-image.tar",
    ],
    manifest = ":calc_manifest.textproto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_proto",
    ],
)

py_library(
    name = "calc_server_py",
    srcs = ["calc_server.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils_py",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_py_pb2",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_py_pb2_grpc",
    ],
)

py_binary(
    name = "calc_server_main_py",
    srcs = ["calc_server_main.py"],
    main = "calc_server_main.py",
    deps = [
        ":calc_server_py",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl:app",
        "@com_google_absl_py//absl/flags",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_py_pb2",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_py_pb2",
    ],
)

python_oci_image(
    name = "calc_server_py_image",
    binary = ":calc_server_main_py",
    data_path = "/",
)

intrinsic_service(
    name = "calculator_py",
    default_config = ":calc_default_config.textproto",
    images = [
        ":calc_server_py_image.tar",
    ],
    manifest = "calc_manifest_py.textproto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_proto",
    ],
)
