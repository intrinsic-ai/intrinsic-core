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
    name = "databundle",
    srcs = ["databundle.go"],
    importpath = "intrinsic/assets/data/databundle",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":datavalidate",
        ":utils",
        "//intrinsic_sdk/intrinsic/assets:ioutils",
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "//intrinsic_sdk/intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic_sdk/intrinsic/util/archive:tartooling",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_google_safearchive//tar",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@io_bazel_rules_go//go/runfiles",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "datafix",
    srcs = ["datafix.go"],
    importpath = "intrinsic/assets/data/datafix",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/util/proto:descriptorcompatibility",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_manifest_go_proto",
    ],
)

go_library(
    name = "datavalidate",
    srcs = ["datavalidate.go"],
    importpath = "intrinsic/assets/data/datavalidate",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":utils",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:metadatautils",
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "//intrinsic_sdk/intrinsic/assets/errors:report",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
    ],
)

go_library(
    name = "utils",
    srcs = ["utils.go"],
    importpath = "intrinsic/assets/data/utils",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@com_github_minio_highwayhash//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)
