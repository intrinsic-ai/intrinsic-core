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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "shared_memory_lockstep",
    srcs = [
        "shared_memory_lockstep.cc",
    ],
    hdrs = [
        "shared_memory_lockstep.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_proxy",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:domain_socket_utils",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:memory_segment",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread:lockstep",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)
