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
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/intrinsic/skills/build_defs:skill.bzl", "cc_skill", "skill_manifest")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "adder_skill_cc",
    srcs = ["adder_skill.cc"],
    hdrs = ["adder_skill.h"],
    deps = [
        ":adder_skill_cc_proto",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/skills/cc:skill_interface",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
    ],
)

skill_manifest(
    name = "adder_skill_manifest",
    src = "adder_skill_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    deps = [":adder_skill_proto"],
)

cc_skill(
    name = "adder_skill",
    manifest = ":adder_skill_manifest",
    deps = [
        ":adder_skill_cc",
        ":adder_skill_cc_proto",
    ],
)

proto_library(
    name = "adder_skill_proto",
    srcs = ["adder_skill.proto"],
)

cc_proto_library(
    name = "adder_skill_cc_proto",
    deps = [":adder_skill_proto"],
)
