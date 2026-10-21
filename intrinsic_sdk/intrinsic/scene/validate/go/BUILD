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
load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "scene_object_validation_c",
    srcs = ["scene_object_validation_c.cc"],
    hdrs = ["scene_object_validation_c.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/scene/validate:scene_object_validation",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
    ],
)

go_library(
    name = "sceneobjectvalidation",
    srcs = ["scene_object_validation.go"],
    cdeps = [
        ":scene_object_validation_c",
    ],
    cgo = 1,
    importpath = "intrinsic/scene/validate/go/sceneobjectvalidation",
    deps = [
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)
