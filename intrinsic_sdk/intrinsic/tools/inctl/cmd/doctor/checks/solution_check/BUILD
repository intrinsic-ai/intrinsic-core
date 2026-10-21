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
    name = "solutioncheck",
    srcs = ["solutioncheck.go"],
    importpath = "intrinsic/tools/inctl/cmd/doctor/checks/solution_check/solutioncheck",
    deps = [
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/doctor/api",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/tools/inctl/cmd/doctor/proto/v1:report_go_proto",
    ],
)
