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

# StatusBuilder to build Intrinsic platform error reports

load("//bazel:go_macros.bzl", "go_library")

package(
    default_visibility = ["//visibility:public"],
)

alias(
    name = "status_builder",
    actual = "//intrinsic_sdk/intrinsic/util/status:status_builder",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "status_macros",
    actual = "//intrinsic_sdk/intrinsic/util/status:status_macros",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "status_conversion_rpc",
    actual = "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "status_conversion_grpc",
    actual = "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "log_if_error",
    actual = "//intrinsic_sdk/intrinsic/util/status:log_if_error",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "return",
    actual = "//intrinsic_sdk/intrinsic/util/status:return",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "ret_check",
    actual = "//intrinsic_sdk/intrinsic/util/status:ret_check",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "status_builder_grpc",
    actual = "//intrinsic_sdk/intrinsic/util/status:status_builder_grpc",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "status_macros_grpc",
    actual = "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "ret_check_grpc",
    actual = "//intrinsic_sdk/intrinsic/util/status:ret_check_grpc",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "status_specs",
    actual = "//intrinsic_sdk/intrinsic/util/status:status_specs",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

go_library(
    name = "approximatestatus",
    srcs = ["approximatestatus.go"],
    importpath = "intrinsic/util/status/approximatestatus",
    deps = [
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
