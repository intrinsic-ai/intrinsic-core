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

"""Build definitions for hot shared state service protos."""

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

proto_library(
    name = "cluster_service_proto",
    srcs = ["cluster_service.proto"],
    deps = [
        "//intrinsic/config/proto:cluster_proto",
    ],
)

py_proto_library(
    name = "cluster_service_py_pb2",
    visibility = ["//intrinsic/storage/hot_shared_state:__subpackages__"],
    deps = [":cluster_service_proto"],
)

py_grpc_library(
    name = "cluster_service_py_pb2_grpc",
    srcs = [":cluster_service_proto"],
    visibility = ["//intrinsic/storage/hot_shared_state:__subpackages__"],
    deps = [":cluster_service_py_pb2"],
)

go_proto_library(
    name = "cluster_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/storage/hot_shared_state/proto/v1/cluster_service_go_proto",
    protos = [":cluster_service_proto"],
    visibility = [
        "//intrinsic/assets/deploy:__subpackages__",
        "//intrinsic/kubernetes/data_store:__subpackages__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/resources/service:__pkg__",
        "//intrinsic/storage/hot_shared_state:__subpackages__",
    ],
    deps = ["//intrinsic/config/proto:cluster_go_proto"],
)

cc_proto_library(
    name = "cluster_service_cc_proto",
    visibility = ["//intrinsic/storage/hot_shared_state:__subpackages__"],
    deps = [":cluster_service_proto"],
)

cc_grpc_library(
    name = "cluster_service_cc_grpc_proto",
    srcs = [":cluster_service_proto"],
    generate_mocks = False,  # Please use FakeHSS instead.
    grpc_only = True,
    visibility = ["//intrinsic/storage/hot_shared_state:__subpackages__"],
    deps = [":cluster_service_cc_proto"],
)

proto_library(
    name = "resource_set_service_proto",
    srcs = ["resource_set_service.proto"],
    deps = [
        "//intrinsic/config/proto:resource_set_proto",
    ],
)

py_proto_library(
    name = "resource_set_service_py_pb2",
    visibility = ["//visibility:private"],
    deps = [":resource_set_service_proto"],
)

py_grpc_library(
    name = "resource_set_service_py_pb2_grpc",
    srcs = [":resource_set_service_proto"],
    visibility = ["//intrinsic/storage/hot_shared_state:__subpackages__"],
    deps = [":resource_set_service_py_pb2"],
)

go_proto_library(
    name = "resource_set_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/storage/hot_shared_state/proto/v1/resource_set_service_go_proto",
    protos = [":resource_set_service_proto"],
    visibility = [
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/conductor:__subpackages__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/resources/service:__pkg__",
        "//intrinsic/storage/hot_shared_state:__subpackages__",
    ],
    deps = ["//intrinsic/config/proto:resource_set_go_proto"],
)

cc_proto_library(
    name = "resource_set_service_cc_proto",
    visibility = [
        "//intrinsic/executive/engine:__subpackages__",
        "//intrinsic/world/service:__subpackages__",
    ],
    deps = [":resource_set_service_proto"],
)

cc_grpc_library(
    name = "resource_set_service_cc_grpc_proto",
    srcs = [":resource_set_service_proto"],
    generate_mocks = False,  # Please use FakeHSS instead.
    grpc_only = True,
    visibility = [
        "//intrinsic/conductor:__subpackages__",
        "//intrinsic/executive/engine:__subpackages__",
        "//intrinsic/storage/hot_shared_state:__subpackages__",
    ],
    deps = [":resource_set_service_cc_proto"],
)
