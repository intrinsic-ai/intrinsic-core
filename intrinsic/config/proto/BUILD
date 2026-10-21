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

# These protos describe all user-accessible configuration of an Intrinsic application.
#
# See go/intrinsic-workcell-ppr for more details.

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "common_proto",
    srcs = ["common.proto"],
    deps = [
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "common_cc_proto",
    deps = [":common_proto"],
)

go_proto_library(
    name = "common_go_proto",
    importpath = "intrinsic/config/proto/common_go_proto",
    protos = [":common_proto"],
)

py_proto_library(
    name = "common_py_pb2",
    deps = [":common_proto"],
)

proto_library(
    name = "application_proto",
    srcs = ["application.proto"],
    deps = [
        ":common_proto",
        ":process_proto",
        ":resource_set_proto",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_proto",
    ],
)

cc_proto_library(
    name = "application_cc_proto",
    deps = [":application_proto"],
)

go_proto_library(
    name = "application_go_proto",
    importpath = "intrinsic/config/proto/application_go_proto",
    protos = [":application_proto"],
    deps = [
        ":common_go_proto",
        ":process_go_proto",
        ":resource_set_go_proto",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
    ],
)

py_proto_library(
    name = "application_py_pb2",
    deps = [":application_proto"],
)

proto_library(
    name = "process_proto",
    srcs = ["process.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_proto",
    ],
)

proto_library(
    name = "process_internal_proto",
    srcs = ["process_internal.proto"],
    visibility = ["//visibility:private"],
    deps = [
        ":process_proto",
    ],
)

py_proto_library(
    name = "process_internal_py_pb2",
    visibility = ["//visibility:private"],
    deps = [":process_internal_proto"],
)

cc_proto_library(
    name = "process_cc_proto",
    deps = [":process_proto"],
)

go_proto_library(
    name = "process_go_proto",
    importpath = "intrinsic/config/proto/process_go_proto",
    protos = [":process_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
    ],
)

go_proto_library(
    name = "process_internal_go_proto",
    importpath = "intrinsic/config/proto/process_internal_go_proto",
    protos = [":process_internal_proto"],
    visibility = [
        "//intrinsic/assets/conversion:__pkg__",
        "//intrinsic/config:__pkg__",
    ],
    deps = [":process_go_proto"],
)

py_proto_library(
    name = "process_py_pb2",
    deps = [":process_proto"],
)

proto_library(
    name = "cluster_proto",
    srcs = ["cluster.proto"],
    deps = [
        ":application_proto",
        ":common_proto",
    ],
)

py_proto_library(
    name = "cluster_py_pb2",
    deps = [":cluster_proto"],
)

cc_proto_library(
    name = "cluster_cc_proto",
    deps = [":cluster_proto"],
)

go_proto_library(
    name = "cluster_go_proto",
    importpath = "intrinsic/config/proto/cluster_go_proto",
    protos = [":cluster_proto"],
    deps = [
        ":application_go_proto",
        ":common_go_proto",
    ],
)

cc_proto_library(
    name = "resource_set_cc_proto",
    deps = [":resource_set_proto"],
)

py_proto_library(
    name = "resource_set_py_pb2",
    deps = [":resource_set_proto"],
)

proto_library(
    name = "resource_set_proto",
    srcs = ["resource_set.proto"],
    deps = [
        "//intrinsic/resources/proto:resource_instance_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
    ],
)

go_proto_library(
    name = "resource_set_go_proto",
    importpath = "intrinsic/config/proto/resource_set_go_proto",
    protos = [":resource_set_proto"],
    deps = [
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
    ],
)

proto_library(
    name = "any_wrapper_proto",
    srcs = ["any_wrapper.proto"],
    visibility = ["//intrinsic/config:__pkg__"],
)

cc_proto_library(
    name = "any_wrapper_cc_proto",
    visibility = ["//intrinsic/config:__pkg__"],
    deps = [":any_wrapper_proto"],
)

py_proto_library(
    name = "any_wrapper_py_pb2",
    deps = [":any_wrapper_proto"],
)

go_proto_library(
    name = "any_wrapper_go_proto",
    importpath = "intrinsic/config/proto/any_wrapper_go_proto",
    protos = [":any_wrapper_proto"],
    visibility = ["//intrinsic:intrinsic_internal"],
)

proto_library(
    name = "version_proto",
    srcs = ["version.proto"],
)

go_proto_library(
    name = "version_go_proto",
    importpath = "intrinsic/config/proto/version_go_proto",
    protos = [":version_proto"],
)
