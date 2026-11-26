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

load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "buffer_factory",
    srcs = [
        "buffer_factory.cc",
    ],
    hdrs = [
        "buffer_factory.h",
    ],
    deps = [
        ":buffer_internal",
        ":immutable_buffer",
        ":mutable_buffer",
        "//intrinsic_sdk/intrinsic/middleware/buffers/allocators:buffer_allocator",
        "//intrinsic_sdk/intrinsic/middleware/buffers/allocators:heap_buffer_allocator",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "buffer_internal",
    hdrs = [
        "buffer_internal.h",
    ],
    visibility = ["//intrinsic_sdk/intrinsic/middleware:__subpackages__"],
)

cc_library(
    name = "immutable_buffer",
    srcs = [
        "immutable_buffer.cc",
    ],
    hdrs = [
        "immutable_buffer.h",
    ],
    deps = [
        ":buffer_internal",
        ":mutable_buffer",
    ],
)

cc_library(
    name = "mutable_buffer",
    srcs = [
        "mutable_buffer.cc",
    ],
    hdrs = [
        "mutable_buffer.h",
    ],
    deps = [
        ":buffer_internal",
    ],
)
