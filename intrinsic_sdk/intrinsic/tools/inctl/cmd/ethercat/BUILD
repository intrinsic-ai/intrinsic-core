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
    name = "ethercat",
    srcs = [
        "download_eni.go",
        "ethercat.go",
        "upload_eni.go",
        "upload_esi.go",
    ] + ["validation_syntax.go"],
    embedsrcs = [],
    importpath = "intrinsic/tools/inctl/cmd/ethercat/ethercat",
    visibility = [
        "//intrinsic/tools:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
        "//intrinsic_sdk/intrinsic/util/proto:descriptor",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@in_gopkg_xmlpath_v2//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:vendor_go_proto",
        "@intrinsic_apis//intrinsic/icon/fieldbus/ethercat/device_service/v1:eni_go_proto",
        "@intrinsic_apis//intrinsic/icon/fieldbus/ethercat/device_service/v1:esi_go_proto",
        "@libxml2-go//:go_default_library",
        "@libxml2-go//parser:go_default_library",
    ] + [] + [
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_x_net//html/charset:go_default_library",
    ],
)
