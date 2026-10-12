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

package(default_visibility = ["//intrinsic:public_api_users"])

go_library(
    name = "processbundle",
    srcs = ["processbundle.go"],
    importpath = "intrinsic/assets/processes/processbundle",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":processmanifest",
        ":processvalidate",
        "//intrinsic_sdk/intrinsic/assets:ioutils",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_manifest_go_proto",
        "//intrinsic_sdk/intrinsic/util/archive:tartooling",
        "//intrinsic_sdk/intrinsic/util/proto:fieldbehavior",
        "@com_github_google_safearchive//tar",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
    ],
)

go_library(
    name = "processfix",
    srcs = ["processfix.go"],
    importpath = "intrinsic/assets/processes/processfix",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/util/proto:descriptorcompatibility",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_manifest_go_proto",
    ],
)

go_library(
    name = "processmanifest",
    srcs = ["processmanifest.go"],
    importpath = "intrinsic/assets/processes/processmanifest",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
    ],
)

go_library(
    name = "processvalidate",
    srcs = ["processvalidate.go"],
    importpath = "intrinsic/assets/processes/processvalidate",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:metadatautils",
        "//intrinsic_sdk/intrinsic/assets/errors:report",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_manifest_go_proto",
        "//intrinsic_sdk/intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:documentation_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
