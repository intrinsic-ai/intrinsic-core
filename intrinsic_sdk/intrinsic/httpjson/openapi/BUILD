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
    name = "handlers",
    srcs = ["handlers.go"],
    importpath = "intrinsic/httpjson/openapi/handlers",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "@com_github_grpc_ecosystem_grpc_gateway_v2//runtime",
        "@io_bazel_rules_go//go/runfiles:go_default_library",
    ],
)
