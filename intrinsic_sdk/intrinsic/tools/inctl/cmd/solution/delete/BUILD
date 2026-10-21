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
    name = "delete",
    srcs = ["delete.go"],
    importpath = "intrinsic/tools/inctl/cmd/solution/delete/delete",
    visibility = [
        "//intrinsic/tools:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools:__subpackages__",
    ],
    deps = [
        "//intrinsic/solution_versions/proto/v1:solution_version_service_go_proto",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:agents",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:color",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
