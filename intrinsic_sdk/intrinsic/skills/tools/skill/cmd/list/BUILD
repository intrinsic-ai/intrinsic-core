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
    name = "list",
    srcs = ["list.go"],
    importpath = "intrinsic/skills/tools/skill/cmd/list/list",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd:listutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
    ],
)
