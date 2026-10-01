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
    name = "migrate",
    srcs = ["migrate.go"],
    importpath = "intrinsic/tools/inctl/cmd/solution/migrate/migrate",
    visibility = ["//intrinsic/tools/inctl/cmd/solution:__pkg__"],
    deps = [
        "//intrinsic/solution_versions/proto/v1alpha1:motion_planner_service_migration_go_proto",
        "//intrinsic/solution_versions/proto/v1alpha1:solution_migration_go_proto",
        "//intrinsic/tools/inctl/auth",
        "//intrinsic/tools/inctl/util:color",
        "//intrinsic/tools/inctl/util:orgutil",
        "//intrinsic/tools/inctl/util:printer",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_protobuf//encoding/protojson",
        "@org_golang_google_protobuf//proto",
    ],
)
