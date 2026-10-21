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

load("//bazel:cc_macros.bzl", "cc_binary")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")
load("//intrinsic_sdk/intrinsic/util/proto/build_defs:proto_data.bzl", "proto_data")

package(default_visibility = ["//visibility:public"])

proto_data(
    name = "status_specs_message",
    src = "//intrinsic/executive:executive.status_specs.pbtxt",
    proto_deps = [
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_proto",
    ],
    proto_name = "intrinsic_proto.assets.StatusSpecs",
)

cc_binary(
    name = "executive",
    srcs = ["executive_main.cc"],
    data = [
        ":status_specs_message",
        "//intrinsic/executive/clips",
        "//intrinsic_sdk/intrinsic/executive/proto:world_query_proto_descriptor_set",
    ],
    deps = [
        "//intrinsic/executive/engine:clips_executive_service",
        "//intrinsic/executive/engine:clips_metrics",
        "//intrinsic/executive/engine:extended_status_codes",
        "//intrinsic/executive/proto:process_tree_cc_proto",  # fixdeps: keep
        "//intrinsic/stats:opencensus",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "//intrinsic_sdk/intrinsic/util/status:return",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_specs",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
    ],
)

# Executive service
cc_oci_image(
    name = "executive-image",
    binary = ":executive",
    data_path = "/",
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
