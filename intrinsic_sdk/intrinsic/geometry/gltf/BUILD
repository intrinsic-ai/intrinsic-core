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

""" Intrinsic package geometry/gltf. """

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//intrinsic:intrinsic_internal"])

cc_library(
    name = "gltf_io",
    srcs = ["gltf_io.cc"],
    hdrs = ["gltf_io.h"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@tinygltf",
    ],
)

cc_library(
    name = "gltf_materials",
    srcs = ["gltf_materials.cc"],
    hdrs = ["gltf_materials.h"],
    deps = [
        "@abseil-cpp//absl/strings:string_view",
        "@tinygltf",
    ],
)

cc_library(
    name = "gltf_merge",
    srcs = ["gltf_merge.cc"],
    hdrs = ["gltf_merge.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@tinygltf",
    ],
)

cc_library(
    name = "gltf_constants",
    hdrs = ["gltf_constants.h"],
    deps = ["@abseil-cpp//absl/strings:string_view"],
)

cc_library(
    name = "gltf_accessor",
    srcs = ["gltf_accessor.cc"],
    hdrs = ["gltf_accessor.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@tinygltf",
    ],
)

cc_library(
    name = "gltf_validation",
    srcs = ["gltf_validation.cc"],
    hdrs = ["gltf_validation.h"],
    deps = [
        ":gltf_accessor",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@tinygltf",
    ],
)

cc_library(
    name = "gltf_buffer_reader",
    srcs = ["gltf_buffer_reader.cc"],
    hdrs = ["gltf_buffer_reader.h"],
    deps = [
        ":gltf_accessor",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@tinygltf",
    ],
)

cc_library(
    name = "gltf_buffer_writer",
    srcs = ["gltf_buffer_writer.cc"],
    hdrs = ["gltf_buffer_writer.h"],
    deps = [
        ":gltf_accessor",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@tinygltf",
    ],
)

cc_library(
    name = "unpack_renderable",
    srcs = ["unpack_renderable.cc"],
    hdrs = ["unpack_renderable.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_nlohmann_json//:json",
        "@meshoptimizer",
        "@or_tools//ortools/base:file",
    ],
)
