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
    name = "hotsharedstateservice",
    srcs = ["hot_shared_state_service.go"],
    importpath = "intrinsic/storage/hot_shared_state/hotsharedstateservice",
    visibility = [
        # Do not extend visibility! Use the fakes from //intrinsic/storage/hot_shared_state/testing/go/BUILD instead.
        "//intrinsic/storage/hot_shared_state:__subpackages__",
    ],
    deps = [
        "//intrinsic/config:applicationview",
        "//intrinsic/config:convertrunnables",
        "//intrinsic/config:util",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:cluster_go_proto",
        "//intrinsic/config/proto:common_go_proto",
        "//intrinsic/config/proto:resource_set_go_proto",
        "//intrinsic/kubernetes/data_store:revision",
        "//intrinsic/kubernetes/data_store/proto:cluster_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:cluster_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:resource_set_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

go_library(
    name = "onpreminit",
    srcs = ["onprem_init.go"],
    importpath = "intrinsic/storage/hot_shared_state/onpreminit",
    visibility = ["//visibility:private"],
    deps = [
        ":hotsharedstateservice",
        "//intrinsic/config/proto:cluster_go_proto",
        "//intrinsic/config/proto:common_go_proto",
        "//intrinsic/kubernetes/data_store/proto:cluster_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:cluster_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

go_binary(
    name = "hot_shared_state_main",
    srcs = ["hot_shared_state_main.go"],
    importpath = "intrinsic/storage/hot_shared_state/hot_shared_state_main",
    deps = [
        ":hotsharedstateservice",
        ":onpreminit",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/kubernetes/data_store:datastoreserver",
        "//intrinsic/kubernetes/data_store/proto:cluster_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:cluster_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:resource_set_service_go_proto",
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//credentials/oauth:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
        "@org_golang_x_oauth2//google:go_default_library",
        "@org_golang_x_sys//unix:go_default_library",
    ],
)

container_image(
    name = "hot-shared-state-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [
        ":hot_shared_state_main",
    ],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
