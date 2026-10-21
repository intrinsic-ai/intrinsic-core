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

load("//bazel:go_macros.bzl", "go_binary")

package(default_visibility = ["//visibility:public"])

go_binary(
    name = "inctl_external",
    srcs = ["inctl_external.go"],
    importpath = "intrinsic/tools/inctl/inctl_external",
    deps = [
        "//intrinsic_sdk/intrinsic/assets/inctl:assetcmd",
        "//intrinsic_sdk/intrinsic/assets/services/inctl:service",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:gpio",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:icon",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:markdown",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/auth",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/bazel",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/cluster",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/device",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/doctor",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/ethercat",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/logs",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/notebook",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/organization",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/process",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/recordings",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution_version:solutionversion",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/version",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/vm",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/world",
    ],
)
