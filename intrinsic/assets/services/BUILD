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

go_library(
    name = "inspection",
    srcs = ["inspection.go"],
    importpath = "intrinsic/assets/services/inspection",
    visibility = [
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/assets/instances:__pkg__",
        "//intrinsic/resources/service:__pkg__",
    ],
    deps = ["//intrinsic/resources/proto:resource_type_runtime_go_proto"],
)

go_library(
    name = "systemservicestate",
    srcs = ["system_service_state.go"],
    importpath = "intrinsic/assets/services/systemservicestate",
    visibility = ["//intrinsic/kubernetes/workcell_spec:__pkg__"],
    deps = [
        "//intrinsic/assets:syncutils",
        "//intrinsic/assets/deploy:render",
        # LINT.IfChange(service_state_versions)
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        # LINT.ThenChange(//intrinsic/assets/deploy/renderutil.go:supported_service_state_versions)
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:system_service_state_go_proto",
        "//intrinsic/skills/internal:configmapwatcher",
        "//intrinsic_sdk/intrinsic/util/status:extstatus",
        "@com_github_golang_glog//:go_default_library",
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_client_go//informers:go_default_library",
        "@io_k8s_client_go//informers/core/v1:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//listers/core/v1:go_default_library",
        "@io_k8s_client_go//tools/cache:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_x_sync//errgroup:go_default_library",
    ],
)

exports_files(
    ["servicevalidate_enterprise.go"],
    visibility = ["//intrinsic_sdk/intrinsic/assets/services:__pkg__"],
)
