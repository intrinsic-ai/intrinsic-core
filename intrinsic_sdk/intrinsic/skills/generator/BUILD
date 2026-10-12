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

# Provides generators for creating skill source code.

load("//bazel:go_macros.bzl", "go_binary", "go_library")
load("//bazel:python.bzl", "py_library")
load(
    "//intrinsic_sdk/intrinsic/util/proto/build_defs:descriptor_set.bzl",
    "proto_source_code_info_transitive_descriptor_set",
)

package(
    default_visibility = [
        "//intrinsic/skills:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills:__subpackages__",
    ],
)

go_library(
    name = "gen",
    srcs = ["gen.go"],
    embedsrcs = [
        "skill_service_main.py.tmpl",
        "skill_service_main_tmpl.cc",
    ],
    importpath = "intrinsic/skills/generator/gen",
    visibility = [
        "//intrinsic/skills:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
    ],
    deps = ["@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto"],
)

proto_source_code_info_transitive_descriptor_set(
    name = "skill_services_provided_to_platform",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = ["//intrinsic_sdk/intrinsic/skills/proto:skill_service_proto"],
)

go_binary(
    name = "skill_service_generator",
    srcs = ["skill_service_generator.go"],
    embedsrcs = [":skill_services_provided_to_platform"],
    importpath = "intrinsic/skills/generator/skill_service_generator",
    visibility = ["//visibility:public"],
    deps = [
        ":gen",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/skills:skillmanifest",
        "//intrinsic_sdk/intrinsic/util/proto:descriptor",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

py_library(
    name = "app",
    srcs = ["app.py"],
    visibility = ["//visibility:public"],
    deps = ["@com_google_absl_py//absl:app"],
)
