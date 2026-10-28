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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "execution_context_proto",
    srcs = ["execution_context.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

go_proto_library(
    name = "execution_context_go_proto",
    importpath = "intrinsic/conductor/proto/execution_context_go_proto",
    protos = [":execution_context_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
    ],
)

cc_proto_library(
    name = "execution_context_cc_proto",
    deps = [":execution_context_proto"],
)

proto_library(
    name = "save_status_proto",
    srcs = ["save_status.proto"],
    deps = [
        "@com_google_protobuf//:timestamp_proto",
    ],
)

go_proto_library(
    name = "save_status_go_proto",
    importpath = "intrinsic/conductor/proto/save_status_go_proto",
    protos = [":save_status_proto"],
)

cc_proto_library(
    name = "save_status_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":save_status_proto"],
)

proto_library(
    name = "wrapper_bridge_proto",
    srcs = ["wrapper_bridge.proto"],
    deps = [
        "//intrinsic/resources/proto:geometric_resource_data_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
        "@intrinsic_apis//intrinsic/util/status:status_proto",
    ],
)

go_proto_library(
    name = "wrapper_bridge_go_proto",
    importpath = "intrinsic/conductor/proto/wrapper_bridge_go_proto",
    protos = [":wrapper_bridge_proto"],
    deps = [
        "//intrinsic/resources/proto:geometric_resource_data_go_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
        "@intrinsic_apis//intrinsic/util/status:status_go_proto",
    ],
)

cc_proto_library(
    name = "wrapper_bridge_cc_proto",
    deps = [":wrapper_bridge_proto"],
)
