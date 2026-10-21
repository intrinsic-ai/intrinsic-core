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

load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

go_library(
    name = "skillbundle",
    srcs = ["skillbundle.go"],
    importpath = "intrinsic/skills/skillbundle",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":skillmanifest",
        ":skillvalidate",
        "//intrinsic_sdk/intrinsic/assets:imageutils",
        "//intrinsic_sdk/intrinsic/assets:ioutils",
        "//intrinsic_sdk/intrinsic/util/archive:tartooling",
        "@com_github_google_safearchive//tar",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_library(
    name = "skillmanifest",
    srcs = ["skillmanifest.go"],
    importpath = "intrinsic/skills/skillmanifest",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/proto:sourcecodeinfoview",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_library(
    name = "skillvalidate",
    srcs = ["skillvalidate.go"],
    importpath = "intrinsic/skills/skillvalidate",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "//intrinsic_sdk/intrinsic/assets:metadatautils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:platform",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils",
        "//intrinsic_sdk/intrinsic/assets/errors:report",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
    ],
)

go_library(
    name = "skillfix",
    srcs = ["skillfix.go"],
    importpath = "intrinsic/skills/skillfix",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/util/proto:descriptorcompatibility",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
    ],
)
