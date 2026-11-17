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
    name = "constants",
    hdrs = ["constants.h"],
    deps = [
        ":user_data_keys",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "user_data_keys",
    hdrs = ["user_data_keys.h"],
    visibility = [
        ":__subpackages__",
        "//intrinsic/simulation/world:__subpackages__",
        "//intrinsic/world/conversion/sdf:__subpackages__",
        "//intrinsic_runtime/intrinsic/scene:__subpackages__",
        "//intrinsic_sdk/intrinsic/world/conversion/sdf:__subpackages__",
    ],
)
