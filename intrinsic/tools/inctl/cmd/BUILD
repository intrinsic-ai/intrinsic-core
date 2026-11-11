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

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

exports_files(["data/ssh_config"])

go_library(
    name = "chartstop",
    srcs = ["chart_stop.go"],
    importpath = "intrinsic/tools/inctl/cmd/chartstop",
    deps = [
        "//intrinsic/kubernetes:chartapply",
        "//intrinsic/kubernetes/workcell_spec:chartassignment",
        "//intrinsic/tools/inctl/util:gcpauth",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_pflag//:go_default_library",
    ],
)

go_library(
    name = "lint",
    srcs = ["lint.go"],
    data = ["//intrinsic/kubernetes:lint_wrapped"],
    importpath = "intrinsic/tools/inctl/cmd/lint",
    deps = [
        "//intrinsic/tools/inctl/util:resolve",
        "//intrinsic_sdk/intrinsic/util/go:set",
        "@cat_dario_mergo//:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/apis/apps/v1alpha1:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@io_bazel_rules_go//go/runfiles:go_default_library",
        "@io_k8s_sigs_yaml//:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
    ],
)
