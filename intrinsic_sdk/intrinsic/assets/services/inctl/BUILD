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
    name = "service",
    srcs = ["service.go"],
    importpath = "intrinsic/assets/services/inctl/service",
    deps = [
        ":add",
        ":delete",
        "//intrinsic_sdk/intrinsic/assets/services/inctl/create",
        "//intrinsic_sdk/intrinsic/assets/services/inctl/state",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
    ],
)

go_library(
    name = "add",
    srcs = ["add.go"],
    importpath = "intrinsic/assets/services/inctl/add",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:version",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "delete",
    srcs = ["delete.go"],
    importpath = "intrinsic/assets/services/inctl/delete",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:agents",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:color",
        "//intrinsic_sdk/intrinsic/util/status:extstatus",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
    ],
)
