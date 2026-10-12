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
    name = "get",
    srcs = ["get.go"],
    importpath = "intrinsic/assets/services/inctl/state/get",
    deps = [
        ":stateutils",
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:system_service_state_go_proto",
    ],
)

go_library(
    name = "list",
    srcs = ["list.go"],
    importpath = "intrinsic/assets/services/inctl/state/list",
    deps = [
        ":stateutils",
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:system_service_state_go_proto",
    ],
)

go_library(
    name = "stateutils",
    srcs = ["stateutils.go"],
    importpath = "intrinsic/assets/services/inctl/state/stateutils",
    deps = [
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:system_service_state_go_proto",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
        "@org_golang_x_text//cases:go_default_library",
        "@org_golang_x_text//language:go_default_library",
    ],
)

go_library(
    name = "state",
    srcs = ["state.go"],
    importpath = "intrinsic/assets/services/inctl/state/state",
    deps = [
        ":disable",
        ":enable",
        ":get",
        ":list",
        ":restart",
        "@com_github_spf13_cobra//:go_default_library",
    ],
)

go_library(
    name = "disable",
    srcs = ["disable.go"],
    importpath = "intrinsic/assets/services/inctl/state/disable",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:system_service_state_go_proto",
    ],
)

go_library(
    name = "enable",
    srcs = ["enable.go"],
    importpath = "intrinsic/assets/services/inctl/state/enable",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:system_service_state_go_proto",
    ],
)

go_library(
    name = "restart",
    srcs = ["restart.go"],
    importpath = "intrinsic/assets/services/inctl/state/restart",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:system_service_state_go_proto",
    ],
)
