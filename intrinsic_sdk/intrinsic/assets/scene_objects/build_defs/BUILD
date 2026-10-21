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

load("//bazel:go_macros.bzl", "go_binary", "go_library")

# Starlark rules and macros for building scene object related targets.

package(
    default_visibility = ["//visibility:public"],
)

go_library(
    name = "sceneobjectgen",
    srcs = ["sceneobjectgen.go"],
    importpath = "intrinsic/assets/scene_objects/build_defs/sceneobjectgen",
    deps = [
        "//intrinsic_sdk/intrinsic/assets/scene_objects:sceneobjectbundle",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "//intrinsic_sdk/intrinsic/util/proto:sourcecodeinfoview",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
    ],
)

go_binary(
    name = "sceneobjectgen_main",
    srcs = ["sceneobjectgen_main.go"],
    importpath = "intrinsic/assets/scene_objects/build_defs/sceneobjectgen_main",
    deps = [
        ":sceneobjectgen",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util:flag",
        "@com_github_golang_glog//:go_default_library",
    ],
)
