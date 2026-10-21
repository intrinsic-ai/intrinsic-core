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

load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

go_library(
    name = "k8sclientutil",
    srcs = ["k8s_client_util.go"],
    importpath = "intrinsic/tools/internal/k8sclientutil",
    deps = [
        "//intrinsic/kubernetes/config:crcconfig",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/apis/registry/v1alpha1:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/client/versioned:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_client_go//discovery:go_default_library",
        "@io_k8s_client_go//dynamic:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//rest:go_default_library",
        "@io_k8s_client_go//tools/clientcmd:go_default_library",
        "@io_k8s_client_go//tools/clientcmd/api:go_default_library",
        "@org_golang_google_api//container/v1:go_default_library",
    ],
)
