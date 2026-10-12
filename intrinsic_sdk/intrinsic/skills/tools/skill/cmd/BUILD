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
    name = "cmd",
    srcs = ["root.go"],
    importpath = "intrinsic/skills/tools/skill/cmd/cmd",
    deps = [
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd/create",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd/install",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd/list",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
    ],
)

go_library(
    name = "solutionutil",
    srcs = ["solutionutil.go"],
    importpath = "intrinsic/skills/tools/skill/cmd/solutionutil",
    deps = [
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/get",
        "@intrinsic_apis//intrinsic/frontend/cloud/api/v1:clusterdiscovery_api_go_proto",
        "@org_golang_google_grpc//:go_default_library",
    ],
)

# NOTE: Do not use this library in new code. Use auth.NewCloudConnection instead.
go_library(
    name = "dialerutil",
    srcs = ["dialerutil.go"],
    importpath = "intrinsic/skills/tools/skill/cmd/dialerutil",
    visibility = [
        "//intrinsic/skills:__subpackages__",
        "//intrinsic/tools/inctl/cmd:__pkg__",
        "//intrinsic/tools/inctl/cmd/cluster:__pkg__",
        "//intrinsic/tools/inctl/cmd/logs:__pkg__",
        "//intrinsic_sdk/intrinsic/skills:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/cluster:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/device:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/logs:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/process:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/get:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/start:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/stop:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:baseclientutils",
        "//intrinsic_sdk/intrinsic/kubernetes/acl:identity",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
    ],
)

go_library(
    name = "waitforskill",
    srcs = ["waitforskill.go"],
    importpath = "intrinsic/skills/tools/skill/cmd/waitforskill",
    deps = [
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_library(
    name = "listutil",
    srcs = ["listutil.go"],
    importpath = "intrinsic/skills/tools/skill/cmd/listutil",
    deps = ["@intrinsic_apis//intrinsic/skills/proto:skills_go_proto"],
)
