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
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

proto_library(
    name = "application_service_proto",
    srcs = ["application_service.proto"],
    deps = [
        "//intrinsic/config/proto:application_proto",
    ],
)

cc_proto_library(
    name = "application_service_cc_proto",
    visibility = [
        "//intrinsic/simulation/service:__subpackages__",
        "//intrinsic/storage/hot_shared_state:__subpackages__",
    ],
    deps = [":application_service_proto"],
)

cc_grpc_library(
    name = "application_service_cc_grpc_proto",
    srcs = [":application_service_proto"],
    grpc_only = True,
    visibility = [
        "//intrinsic/simulation/service:__subpackages__",
        "//intrinsic/storage/hot_shared_state:__subpackages__",
    ],
    deps = [":application_service_cc_proto"],
)

py_proto_library(
    name = "application_service_py_pb2",
    visibility = [
        "//intrinsic/storage/hot_shared_state:__subpackages__",
    ],
    deps = [":application_service_proto"],
)

py_grpc_library(
    name = "application_service_py_pb2_grpc",
    srcs = [":application_service_proto"],
    visibility = [
        "//intrinsic/storage/hot_shared_state:__subpackages__",
    ],
    deps = [":application_service_py_pb2"],
)

go_proto_library(
    name = "application_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/storage/hot_shared_state/proto/application_service_go_proto",
    protos = [":application_service_proto"],
    visibility = [
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/assets/instances:__pkg__",
        "//intrinsic/assets/internal:__pkg__",
        "//intrinsic/assets/services:__pkg__",
        "//intrinsic/frontend/solution_service:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/resources/service:__pkg__",
        "//intrinsic/storage/hot_shared_state:__subpackages__",
        "//intrinsic/tools/inctl/cmd:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:__pkg__",
    ],
    deps = ["//intrinsic/config/proto:application_go_proto"],
)
