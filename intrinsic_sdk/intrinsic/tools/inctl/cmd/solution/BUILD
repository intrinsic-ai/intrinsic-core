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

go_library(
    name = "solution",
    srcs = [
        "solution.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/solution/solution",
    visibility = [
        "//intrinsic/skills/tools:__subpackages__",
        "//intrinsic/tools:__subpackages__",
        "//intrinsic/tools/inctl:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/tools:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/create",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/delete",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/get",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/list",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/migrate",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/share",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/start",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/stop",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
    ] + [],
)
