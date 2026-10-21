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

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "remote_trigger_server",
    srcs = [
        "remote_trigger_server.cc",
    ],
    hdrs = [
        "remote_trigger_constants.h",
        "remote_trigger_server.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/interprocess:binary_futex",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:memory_segment",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "remote_trigger_client",
    srcs = [
        "remote_trigger_client.cc",
    ],
    hdrs = [
        "remote_trigger_client.h",
        "remote_trigger_constants.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/interprocess:binary_futex",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:domain_socket_utils",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:memory_segment",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)
