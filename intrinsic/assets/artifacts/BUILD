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
    name = "processor",
    srcs = ["processor.go"],
    importpath = "intrinsic/assets/artifacts/processor",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/longrunning/go:operations",
        "//intrinsic/storage/content_addressable_storage/pkg/clienthelpers",
        "//intrinsic/storage/content_addressable_storage/pkg/crossproject",
        "//intrinsic/storage/content_addressable_storage/pkg/observability",
        "//intrinsic_sdk/intrinsic/assets:baseclientutils",
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "uploader",
    srcs = ["uploader.go"],
    importpath = "intrinsic/assets/artifacts/uploader",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/storage/content_addressable_storage/pkg/clienthelpers",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:uuid",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_go_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "assetartifactsservice",
    srcs = ["asset_artifacts_service.go"],
    importpath = "intrinsic/assets/artifacts/assetartifactsservice",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":processor",
        ":uploader",
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "@com_github_golang_glog//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_artifacts_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)
