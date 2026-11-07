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

# appstat is a utility that inspects a Kubernetes namespace and looks for common
# errors.

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library")

go_library(
    name = "inspect",
    srcs = ["inspect.go"],
    importpath = "intrinsic/kubernetes/appstat/inspect",
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        ":generatedpodsuffix",
        ":pollingspans",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@io_k8s_api//apps/v1:go_default_library",
        "@io_k8s_api//batch/v1:go_default_library",
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/api/errors:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1/unstructured:go_default_library",
        "@io_k8s_apimachinery//pkg/runtime/schema:go_default_library",
        "@io_k8s_client_go//dynamic:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//kubernetes/typed/core/v1:go_default_library",
        "@io_k8s_client_go//plugin/pkg/client/auth/gcp:go_default_library",
        "@io_k8s_client_go//rest:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_x_sys//unix:go_default_library",
    ],
)

go_library(
    name = "pollingspans",
    srcs = ["polling_spans.go"],
    importpath = "intrinsic/kubernetes/appstat/pollingspans",
    visibility = ["//visibility:private"],
)

go_library(
    name = "generatedpodsuffix",
    srcs = ["generated_pod_suffix.go"],
    importpath = "intrinsic/kubernetes/appstat/generatedpodsuffix",
    visibility = ["//visibility:private"],
)
