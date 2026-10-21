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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

proto_library(
    name = "asset_info_internal_proto",
    srcs = ["asset_info_internal.proto"],
    visibility = [
        "//intrinsic/conductor:__subpackages__",
        "//intrinsic/executive:__subpackages__",
        "//intrinsic/resources/proto:__pkg__",
    ],
    deps = [
        "//intrinsic/resources/proto:geometric_resource_data_proto",
        "//intrinsic/resources/proto:resource_type_runtime_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

cc_proto_library(
    name = "asset_info_internal_cc_proto",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/conductor:__subpackages__",
        "//intrinsic/executive:__subpackages__",
    ],
    deps = [":asset_info_internal_proto"],
)

cc_grpc_library(
    name = "asset_info_internal_cc_grpc",
    srcs = [":asset_info_internal_proto"],
    generate_mocks = True,
    grpc_only = True,
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/conductor:__subpackages__",
        "//intrinsic/executive:__subpackages__",
    ],
    deps = [":asset_info_internal_cc_proto"],
)

go_proto_library(
    name = "asset_info_internal_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/assets/proto/v1alpha1/asset_info_internal_go_proto",
    protos = [":asset_info_internal_proto"],
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/resources/proto:__pkg__",
        "//intrinsic/resources/service:__pkg__",
    ],
    deps = [
        "//intrinsic/resources/proto:geometric_resource_data_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
    ],
)
