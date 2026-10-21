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

load("//bazel:cc_macros.bzl", "cc_library")

# Validation functionalities for behavior trees
package(default_visibility = ["//intrinsic/executive:__subpackages__"])

cc_library(
    name = "validate_behavior_tree",
    srcs = ["validate_behavior_tree.cc"],
    hdrs = ["validate_behavior_tree.h"],
    deps = [
        "//intrinsic/executive/cc:behavior_tree_visitor",
        "//intrinsic/executive/clips/cc:skill_info",
        "//intrinsic/executive/clips_cpp",
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/executive/engine:skill_client_generator",
        "//intrinsic_runtime/intrinsic/proto_tools/compatibility:proto_compat",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)
