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
    name = "assetcmd",
    srcs = ["assetcmd.go"],
    importpath = "intrinsic/assets/inctl/assetcmd",
    deps = [
        ":getreleased",
        ":install",
        ":list",
        ":listreleased",
        ":listreleasedversions",
        ":release",
        ":uninstall",
        ":updatereleasemetadata",
        "//intrinsic/assets/inctl/instance",
        "//intrinsic/tools/inctl/cmd:root",
        "//intrinsic/tools/inctl/util:cobrautil",
    ] + [],
)

go_library(
    name = "assetviews",
    srcs = ["assetviews.go"],
    importpath = "intrinsic/assets/inctl/assetviews",
    deps = [
        "//intrinsic/assets:idutils",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "getreleased",
    srcs = ["getreleased.go"],
    importpath = "intrinsic/assets/inctl/getreleased",
    deps = [
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets:idutils",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_internal_go_proto",
        "//intrinsic/tools/inctl/cmd:root",
        "//intrinsic/tools/inctl/util:printer",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
    ],
)

go_library(
    name = "install",
    srcs = ["install.go"],
    importpath = "intrinsic/assets/inctl/install",
    deps = [
        "//intrinsic/assets:bundle",
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets:idutils",
        "//intrinsic/assets:imagetransfer",
        "//intrinsic/assets:referenceddata",
        "//intrinsic/assets:throttle",
        "//intrinsic/assets/platformlevelswitch",
        "//intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic/assets/scene_objects:gzfprocessor",
        "//intrinsic/assets/services:bundleimages",
        "//intrinsic/skills/tools/skill/cmd/directupload",
        "//intrinsic/storage/content_addressable_storage/pkg/filetocas",
        "//intrinsic/tools/inctl/util:casgeometryuploader",
        "//intrinsic/tools/inctl/util:color",
        "//intrinsic/util/status:extstatus",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_artifacts_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_x_sync//errgroup:go_default_library",
    ],
)

go_library(
    name = "list",
    srcs = ["list.go"],
    importpath = "intrinsic/assets/inctl/list",
    deps = [
        ":assetviews",
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic/tools/inctl/cmd:root",
        "//intrinsic/tools/inctl/util:printer",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto/v1:search_go_proto",
    ],
)

go_library(
    name = "listreleased",
    srcs = ["listreleased.go"],
    importpath = "intrinsic/assets/inctl/listreleased",
    deps = [
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets:idutils",
        "//intrinsic/assets:interfaceutils",
        "//intrinsic/assets:listutils",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic/tools/inctl/cmd:root",
        "//intrinsic/tools/inctl/util:printer",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "listreleasedversions",
    srcs = ["listreleasedversions.go"],
    importpath = "intrinsic/assets/inctl/listreleasedversions",
    deps = [
        ":assetviews",
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets:listutils",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic/tools/inctl/cmd:root",
        "//intrinsic/tools/inctl/util:printer",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "release",
    srcs = ["release.go"],
    importpath = "intrinsic/assets/inctl/release",
    deps = [
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets:imagetransfer",
        "//intrinsic/assets:imageutils",
        "//intrinsic/assets:throttle",
        "//intrinsic/assets/catalog:releaseasset",
        "//intrinsic/skills/tools/skill/cmd/directupload",
        "//intrinsic/tools/inctl/cmd:root",
        "//intrinsic/tools/inctl/util:printer",
        "@com_github_spf13_cobra//:go_default_library",
    ],
)

go_library(
    name = "uninstall",
    srcs = ["uninstall.go"],
    importpath = "intrinsic/assets/inctl/uninstall",
    deps = [
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets:idutils",
        "//intrinsic/assets/proto:installed_assets_go_proto",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_library(
    name = "updatereleasemetadata",
    srcs = ["updatereleasemetadata.go"],
    importpath = "intrinsic/assets/inctl/updatereleasemetadata",
    deps = [
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets:idutils",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic/tools/inctl/cmd:root",
        "//intrinsic/tools/inctl/util:printer",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/catalog/proto/v1:release_metadata_go_proto",
        "@org_golang_google_protobuf//types/known/fieldmaskpb",
    ],
)
