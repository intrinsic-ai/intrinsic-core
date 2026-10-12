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

load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//intrinsic:public_api_users"])

proto_library(
    name = "process_asset_proto",
    srcs = ["process_asset.proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/executive/proto:behavior_tree_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_proto",
    ],
)

py_proto_library(
    name = "process_asset_py_pb2",
    deps = [":process_asset_proto"],
)

go_proto_library(
    name = "process_asset_go_proto",
    importpath = "intrinsic/assets/processes/proto/process_asset_go_proto",
    protos = [":process_asset_proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
    ],
)

proto_library(
    name = "process_manifest_proto",
    srcs = ["process_manifest.proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/executive/proto:behavior_tree_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_proto",
        "@intrinsic_apis//intrinsic/assets/proto:documentation_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/assets/proto:vendor_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_proto",
    ],
)

py_proto_library(
    name = "process_manifest_py_pb2",
    deps = [":process_manifest_proto"],
)

go_proto_library(
    name = "process_manifest_go_proto",
    importpath = "intrinsic/assets/processes/proto/process_manifest_go_proto",
    protos = [":process_manifest_proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:documentation_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:vendor_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
    ],
)
