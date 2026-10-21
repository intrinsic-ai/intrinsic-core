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

load("//bazel:container.bzl", "container_image")
load("//bazel:go_macros.bzl", "go_binary", "go_library")

go_library(
    name = "solutionservice",
    srcs = ["solution_service.go"],
    importpath = "intrinsic/frontend/solution_service/solutionservice",
    deps = [
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:process_go_proto",
        "//intrinsic/frontend/solution_service/sequencelist",
        "//intrinsic/kubernetes/workcell_spec/proto:solution_internal_go_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic/util/pagination:pagesize",
        "//intrinsic/util/pagination:pagetoken",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_go_proto",
        "//intrinsic_sdk/intrinsic/kubernetes/acl:clientcontext",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_go_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/frontend/solution_service/proto:status_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

go_binary(
    name = "solution_service_main",
    srcs = ["solution_service_main.go"],
    importpath = "intrinsic/frontend/solution_service/solution_service_main",
    deps = [
        ":solutionservice",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/kubernetes/workcell_spec/proto:solution_internal_go_proto",
        "//intrinsic/modified_solution/proto/v1:modified_solution_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_go_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_go_proto",
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_go_proto",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

container_image(
    name = "solution-service-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [
        ":solution_service_main",
    ],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
