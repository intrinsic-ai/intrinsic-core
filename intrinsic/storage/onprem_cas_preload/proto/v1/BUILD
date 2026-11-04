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

"""Build definitions for the onprem CAS preload service protos."""

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

proto_library(
    name = "ocp_service_proto",
    srcs = ["ocp_service.proto"],
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
    ],
    deps = [
        ":preload_set_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
    ],
)

go_proto_library(
    name = "ocp_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/storage/onprem_cas_preload/proto/v1/ocp_service_go_proto",
    protos = [":ocp_service_proto"],
    visibility = [
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/storage/onprem_cas:__subpackages__",
    ],
    deps = [
        ":preload_set_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
    ],
)

cc_proto_library(
    name = "ocp_service_cc_proto",
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
    ],
    deps = [":ocp_service_proto"],
)

cc_grpc_library(
    name = "ocp_service_cc_grpc_proto",
    srcs = [":ocp_service_proto"],
    generate_mocks = False,
    grpc_only = True,
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
    ],
    deps = [":ocp_service_cc_proto"],
)

py_proto_library(
    name = "ocp_service_py_pb2",
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
    ],
    deps = [":ocp_service_proto"],
)

py_grpc_library(
    name = "ocp_service_py_pb2_grpc",
    srcs = [":ocp_service_proto"],
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
    ],
    deps = [":ocp_service_py_pb2"],
)

proto_library(
    name = "preload_set_proto",
    srcs = ["preload_set.proto"],
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
    ],
    deps = [
        "@com_google_protobuf//:timestamp_proto",
    ],
)

go_proto_library(
    name = "preload_set_go_proto",
    importpath = "intrinsic/storage/onprem_cas_preload/proto/v1/preload_set_go_proto",
    protos = [":preload_set_proto"],
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/storage/onprem_cas:__subpackages__",
    ],
)

cc_proto_library(
    name = "preload_set_cc_proto",
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
    ],
    deps = [":preload_set_proto"],
)

py_proto_library(
    name = "preload_set_py_pb2",
    visibility = [
        # Please don't use __subpackages__, specify the exact __pkg__!
    ],
    deps = [":preload_set_proto"],
)
