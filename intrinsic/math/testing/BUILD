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

load("//bazel:cc_macros.bzl", "cc_library")

cc_library(
    name = "fuzz_math_utils",
    testonly = True,
    srcs = ["fuzz_math_utils.cc"],
    hdrs = ["fuzz_math_utils.h"],
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_fuzztest//fuzztest",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
    ],
)
