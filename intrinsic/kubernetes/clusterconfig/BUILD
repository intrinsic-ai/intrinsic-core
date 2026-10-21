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

# clusterconfig exposes read-only cluster settings

load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//intrinsic/kubernetes:__subpackages__"])

go_library(
    name = "clusterconfig",
    srcs = ["clusterconfig.go"] + [],
    importpath = "intrinsic/kubernetes/clusterconfig/clusterconfig",
    deps = [
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/apis/registry/v1alpha1:go_default_library",
    ],
)
