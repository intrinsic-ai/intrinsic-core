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

load("//bazel:go_macros.bzl", "go_binary", "go_library")

package(default_visibility = [
    "//intrinsic/skills:__subpackages__",
    "//intrinsic_sdk/intrinsic/skills:__subpackages__",
])

go_binary(
    name = "skillmanifestgen",
    srcs = ["skillmanifestgen.go"],
    importpath = "intrinsic/skills/build_defs/skillmanifestgen",
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
    ],
)

go_library(
    name = "skillgen",
    srcs = ["skillgen.go"],
    importpath = "intrinsic/skills/build_defs/skillgen",
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/skills:skillbundle",
        "//intrinsic_sdk/intrinsic/skills:skillfix",
        "//intrinsic_sdk/intrinsic/skills:skillvalidate",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_binary(
    name = "skillgen_main",
    srcs = ["skillgen_main.go"],
    importpath = "intrinsic/skills/build_defs/skillgen_main",
    visibility = ["//visibility:public"],
    deps = [
        ":skillgen",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/skills:skillbundle",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_library(
    name = "skillserviceconfiggen",
    srcs = ["skillserviceconfiggen.go"],
    importpath = "intrinsic/skills/build_defs/skillserviceconfiggen",
    visibility = [
        "//intrinsic/skills:__subpackages__",
        "//intrinsic/skills/build_defs:__pkg__",
        "//intrinsic_sdk/intrinsic/skills:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/util/proto:sourcecodeinfoview",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_service_config_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_binary(
    name = "skillserviceconfiggen_main",
    srcs = ["skillserviceconfiggen_main.go"],
    importpath = "intrinsic/skills/build_defs/skillserviceconfiggen_main",
    visibility = ["//visibility:public"],
    deps = [
        ":skillserviceconfiggen",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)
