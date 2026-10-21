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

# Metrics for the motion planning module.

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//intrinsic_motion_planning:__subpackages__",
])

cc_library(
    name = "metrics",
    srcs = ["metrics.cc"],
    hdrs = ["metrics.h"],
    deps = [
        "//intrinsic/stats:metrics_utils",
        "@abseil-cpp//absl/strings:string_view",
        "@io_opentelemetry_cpp//api",
    ],
)
