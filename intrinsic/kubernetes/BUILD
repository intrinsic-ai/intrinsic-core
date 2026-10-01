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

# This package contains infrastructure for Kubernetes in Intrinsic's labs.

load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")
load("//bazel:container.bzl", "container_image")
load("//bazel:go_macros.bzl", "go_binary", "go_library")
load("//bazel:python.bzl", "py_binary")
load("//bazel:sh_macros.bzl", "sh_binary")
load("//intrinsic/util/wrapped_executable:wrapped_executable.bzl", "wrapped_binary")

package(default_visibility = [":intrinsic_kubernetes_users"])

package_group(
    name = "intrinsic_kubernetes_users",
    packages = [
        "//...",
        "//intrinsic/...",
    ],
)

exports_files([
    # keep-sorted start
    "Chart.yaml.template",
    "common-values.yaml",
    "lint.sh",
    "yamllint_config.yaml",
    # keep-sorted end
])

filegroup(
    name = "global_values_template",
    srcs = [
        "global-values-core.yaml.template",
    ],
)

container_image(
    name = "base-image-oci",
    base = "@distroless_base",
    entrypoint = [
        "/dumb-init",
        "--",
    ],
    files = ["@dumb-init"],
    visibility = [
        ":intrinsic_kubernetes_users",
        "//third_party/abb_hardware_module/abb_hwm:__pkg__",
        "//third_party/chromium:__pkg__",
    ],
)

container_image(
    name = "base-image-cc-oci",
    base = "@distroless_cc",
    entrypoint = [
        "/dumb-init",
        "--",
    ],
    files = ["@dumb-init"],
    visibility = [
        ":intrinsic_kubernetes_users",
        "//third_party/abb_hardware_module/abb_hwm:__pkg__",
        "//third_party/chromium:__pkg__",
        "//third_party/toybox:__pkg__",
    ],
)

container_image(
    name = "python3-image-oci",
    base = "@distroless_python3",
    visibility = [
        ":intrinsic_kubernetes_users",
    ],
)

py_console_script_binary(
    name = "yamllint",
    pkg = "@ai_intrinsic_sdks_pip_deps//yamllint",
)

py_binary(
    name = "check_route_secpolicy",
    srcs = ["check_route_secpolicy.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//pyyaml",
    ],
)

sh_binary(
    name = "lint",
    srcs = ["lint.sh"],
    data = [
        "//intrinsic/kubernetes:check_route_secpolicy",
        "//intrinsic/kubernetes:yamllint",
        "//intrinsic/kubernetes:yamllint_config.yaml",
        "//third_party/crd_schemas",
        "@com_github_yannh_kubeconform//cmd/kubeconform",
        "@kubernetes_json_schema//file",
        "@sh_helm_helm_v3//cmd/helm",
    ],
    env = {
        "CHECK_ROUTE_SECPOLICY_BIN": "$(rlocationpath //intrinsic/kubernetes:check_route_secpolicy)",
        "CRD_SCHEMAS_FILES": "$(rlocationpaths //third_party/crd_schemas:crd_schemas)",
        "HELM_BIN": "$(rlocationpath @sh_helm_helm_v3//cmd/helm)",
        "KUBECONFORM_BIN": "$(rlocationpath @com_github_yannh_kubeconform//cmd/kubeconform)",
        "KUBECONFORM_JSON_SCHEMA": "$(rlocationpath @kubernetes_json_schema//file)",
        "YAMLLINT_BIN": "$(rlocationpath //intrinsic/kubernetes:yamllint)",
        "YAMLLINT_CONFIG": "$(rlocationpath //intrinsic/kubernetes:yamllint_config.yaml)",
    },
    visibility = ["//intrinsic/kubernetes:intrinsic_kubernetes_users"],
)

wrapped_binary(
    name = "lint_wrapped",
    target = ":lint",
)

go_library(
    name = "chartapply",
    srcs = ["chartapply.go"],
    importpath = "intrinsic/kubernetes/chartapply",
    deps = [
        ":values",
        "//intrinsic/kubernetes/config:crcconfig",
        "//intrinsic/production:imagepublisher",
        "//intrinsic/tools/inctl/util:gcpauth",
        "//intrinsic/tools/internal:k8sclientutil",
        "@cat_dario_mergo//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core_src//go/pkg/apis/apps/v1alpha1:go_default_library",
        "@com_github_googlecloudrobotics_core_src//go/pkg/client/versioned:go_default_library",
        "@com_google_cloud_go_storage//:storage",
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/api/errors:go_default_library",
        "@io_k8s_apimachinery//pkg/api/meta:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1/unstructured:go_default_library",
        "@io_k8s_apimachinery//pkg/runtime/schema:go_default_library",
        "@io_k8s_apimachinery//pkg/types:go_default_library",
        "@io_k8s_apimachinery//pkg/util/yaml:go_default_library",
        "@io_k8s_client_go//discovery:go_default_library",
        "@io_k8s_client_go//discovery/cached/memory:go_default_library",
        "@io_k8s_client_go//dynamic:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//rest:go_default_library",
        "@io_k8s_client_go//restmapper:go_default_library",
        "@io_k8s_client_go//tools/clientcmd:go_default_library",
        "@io_k8s_client_go//tools/clientcmd/api:go_default_library",
        "@org_golang_google_api//cloudbuild/v1:go_default_library",
        "@org_golang_google_api//option:go_default_library",
        "@sh_helm_helm_v3//pkg/action:go_default_library",
        "@sh_helm_helm_v3//pkg/chart:go_default_library",
        "@sh_helm_helm_v3//pkg/chart/loader:go_default_library",
        "@sh_helm_helm_v3//pkg/cli:go_default_library",
    ] + [],
)

go_library(
    name = "chartrelease",
    srcs = ["chartrelease.go"],
    importpath = "intrinsic/kubernetes/chartrelease",
    deps = [
        "//intrinsic/kubernetes/workcell_spec:chartassignment",
        "//intrinsic/kubernetes/workcell_spec:imagetags",
        "//intrinsic/kubernetes/workcell_spec:workcellspec",
        "//intrinsic/tools/inctl/util:gcpauth",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_masterminds_semver//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_google_cloud_go_storage//:storage",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "@sh_helm_helm_v3//pkg/registry:go_default_library",
    ],
)

go_library(
    name = "chartassignmentrelease",
    srcs = ["chartassignmentrelease.go"],
    importpath = "intrinsic/kubernetes/chartassignmentrelease",
    deps = [
        ":values",
        "//intrinsic/kubernetes/workcell_spec:chartassignment",
        "//intrinsic/kubernetes/workcell_spec:workcellspec",
        "//intrinsic/production:imagepublisher",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core_src//go/pkg/apis/apps/v1alpha1:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@io_k8s_sigs_yaml//:go_default_library",
    ],
)

go_binary(
    name = "chart_executable",
    srcs = [
        "chart_executable.go",
    ],
    importpath = "intrinsic/kubernetes/chart_executable",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":chartassignmentrelease",
        ":chartrelease",
        ":values",
        "//intrinsic/kubernetes/appstat:inspect",
        "//intrinsic/kubernetes/workcell_spec:chartassignment",
        "//intrinsic/kubernetes/workcell_spec:workcellspec",
        "//intrinsic/production:imagepublisher",
        "//intrinsic/production:intrinsic",
        "//intrinsic/tools/inctl/cmd:chartstop",
        "//intrinsic/tools/inctl/cmd:lint",
        "//intrinsic/tools/inctl/util:traceutil",
        "//intrinsic/tools/internal:k8sclientutil",
        "//intrinsic/util/path_resolver:pathresolver",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core_src//go/pkg/client/versioned:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "@io_k8s_client_go//dynamic:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
    ] + [],
)

go_library(
    name = "intrinsic",
    srcs = ["init.go"],
    importpath = "intrinsic/kubernetes/intrinsic",
    visibility = [":intrinsic_kubernetes_users"],
    deps = [
        "//intrinsic/production:intrinsic",
        "@com_github_golang_glog//:go_default_library",
    ],
)

go_library(
    name = "values",
    srcs = ["values.go"],
    importpath = "intrinsic/kubernetes/values",
    deps = [
        "//intrinsic/config:environments",
        "//intrinsic/kubernetes/workcell_spec:repositorybasename",
        "//intrinsic/production:imagepublisher",
        "@cat_dario_mergo//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core_src//go/pkg/apis/apps/v1alpha1:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@io_k8s_sigs_yaml//:go_default_library",
        "@sh_helm_helm_v3//pkg/chart:go_default_library",
    ],
)
