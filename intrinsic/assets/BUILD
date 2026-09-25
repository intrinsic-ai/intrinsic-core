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

# Common code for assets.

load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

go_library(
    name = "assetvalidate",
    srcs = ["assetvalidate.go"],
    importpath = "intrinsic/assets/assetvalidate",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/assets:idutils",
        "//intrinsic/assets/data:datavalidate",
        "//intrinsic/assets/errors:report",
        "//intrinsic/assets/hardware_devices:hardwaredevicevalidate",
        "//intrinsic/assets/processes:processvalidate",
        "//intrinsic/assets/proto/v1:asset_go_proto",
        "//intrinsic/assets/proto/v1:processed_asset_go_proto",
        "//intrinsic/assets/scene_objects:sceneobjectvalidate",
        "//intrinsic/assets/services:servicevalidate",
        "//intrinsic/skills:skillvalidate",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
    ],
)

go_library(
    name = "baseclientutils",
    srcs = ["baseclientutils.go"],
    importpath = "intrinsic/assets/baseclientutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "@com_github_pkg_errors//:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
    ],
)

go_library(
    name = "clientutils",
    srcs = ["clientutils.go"],
    importpath = "intrinsic/assets/clientutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":baseclientutils",
        ":cmdutils",
        "//intrinsic/config:environments",
        "//intrinsic/kubernetes/acl:identity",
        "//intrinsic/kubernetes/config:crcconfig",
        "//intrinsic/tools/inctl/auth",
        "//intrinsic/tools/inctl/util:gcpauth",
        "//intrinsic/tools/inctl/util:vmalias",
        "@com_github_google_go_containerregistry//pkg/authn:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/google:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/remote:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/frontend/cloud/api/v1:clusterdiscovery_api_go_proto",
        "@intrinsic_apis//intrinsic/frontend/cloud/api/v1:solutiondiscovery_api_go_proto",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ] + [],
)

go_library(
    name = "cmdutils",
    srcs = ["cmdutils.go"],
    importpath = "intrinsic/assets/cmdutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":imagetransfer",
        ":imageutils",
        ":typeutils",
        ":viewutils",
        "//intrinsic/assets/platformlevelswitch",
        "//intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic/assets/services:bundleimages",
        "//intrinsic/tools/inctl/util:orgutil",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@org_golang_x_exp//maps",
    ],
)

cc_library(
    name = "id_utils",
    srcs = ["id_utils.cc"],
    hdrs = ["id_utils.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/util/status:annotate",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@boringssl//:crypto",
        "@com_googlesource_code_re2//:re2",
        "@intrinsic_apis//intrinsic/assets/proto:id_cc_proto",
    ],
)

py_library(
    name = "id_utils_py",
    srcs = ["id_utils.py"],
    visibility = ["//intrinsic:public_api_users"],
    deps = ["@intrinsic_apis//intrinsic/assets/proto:id_py_pb2"],
)

go_library(
    name = "idutils",
    srcs = ["idutils.go"],
    importpath = "intrinsic/assets/idutils",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@org_golang_x_exp//slices",
    ],
)

go_library(
    name = "imagetransfer",
    srcs = ["imagetransfer.go"],
    importpath = "intrinsic/assets/imagetransfer",
    visibility = [
        "//intrinsic:internal_api_users",

        # TODO(b/330183066) Remove this with hwmodule tool.
    ],
    deps = [
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_google_go_containerregistry//pkg/authn:go_default_library",
        "@com_github_google_go_containerregistry//pkg/name:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/google:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/remote:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/remote/transport:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto",
    ],
)

go_library(
    name = "imageutils",
    srcs = ["imageutils.go"],
    importpath = "intrinsic/assets/imageutils",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto",
    ],
)

go_library(
    name = "pageutils",
    srcs = ["pageutils.go"],
    importpath = "intrinsic/assets/pageutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/assets/proto:page_token_data_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "tagutils",
    srcs = ["tagutils.go"],
    embedsrcs = [
        "//intrinsic/assets/proto:asset_tags",  # keep
    ],
    importpath = "intrinsic/assets/tagutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
    ],
)

go_library(
    name = "typeutils",
    srcs = ["typeutils.go"],
    importpath = "intrinsic/assets/typeutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = ["@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto"],
)

go_library(
    name = "viewutils",
    srcs = ["viewutils.go"],
    importpath = "intrinsic/assets/viewutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic/assets/proto/v1:asset_go_proto",
        "//intrinsic/assets/proto/v1:processed_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb",
    ],
)

go_library(
    name = "bundle",
    srcs = ["bundle.go"],
    importpath = "intrinsic/assets/bundle",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":imageutils",
        ":ioutils",
        "//intrinsic/assets:referenceddata",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic/assets/data:databundle",
        "//intrinsic/assets/hardware_devices:hardwaredevicebundle",
        "//intrinsic/assets/processes:processbundle",
        "//intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic/assets/proto/v1:asset_go_proto",
        "//intrinsic/assets/proto/v1:processed_asset_go_proto",
        "//intrinsic/assets/scene_objects:gzfprocessor",
        "//intrinsic/assets/scene_objects:sceneobjectbundle",
        "//intrinsic/assets/services:servicebundle",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/skills:skillbundle",
        "//intrinsic/util/proto:descriptor",
        "@intrinsic_apis//intrinsic/assets/catalog/proto/v1:release_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_library(
    name = "version",
    srcs = ["version.go"],
    importpath = "intrinsic/assets/version",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":idutils",
        "//intrinsic/assets/proto:installed_assets_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "ioutils",
    srcs = ["ioutils.go"],
    importpath = "intrinsic/assets/ioutils",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/skills:__subpackages__",
    ],
    deps = [
        "@com_github_google_safearchive//tar",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "listutils",
    srcs = ["listutils.go"],
    importpath = "intrinsic/assets/listutils",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@org_golang_google_grpc//:go_default_library",
    ],
)

go_library(
    name = "metadatautils",
    srcs = ["metadatautils.go"],
    importpath = "intrinsic/assets/metadatautils",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":idutils",
        ":interfaceutils",
        ":tagutils",
        "//intrinsic/assets/dependencies:platform",
        "//intrinsic/assets/processes/proto:process_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:documentation_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:vendor_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "interfaceutils",
    srcs = ["interfaceutils.go"],
    importpath = "intrinsic/assets/interfaceutils",
    visibility = ["//intrinsic:public_api_users"],
    deps = ["@com_github_pkg_errors//:go_default_library"],
)

cc_library(
    name = "interface_utils",
    srcs = ["interface_utils.cc"],
    hdrs = ["interface_utils.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_googlesource_code_re2//:re2",
    ],
)

py_library(
    name = "interface_utils_py",
    srcs = ["interface_utils.py"],
    visibility = ["//intrinsic:public_api_users"],
)

go_library(
    name = "metadatautilsinternal",
    srcs = ["metadatautilsinternal.go"],
    importpath = "intrinsic/assets/metadatautilsinternal",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":idutils",
        ":interfaceutils",
        "//intrinsic/assets/hardware_devices:graph",
        "//intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic/util/proto:descriptor",
        "//intrinsic/util/proto:names",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_library(
    name = "referenceddata",
    srcs = ["referenceddata.go"],
    importpath = "intrinsic/assets/referenceddata",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic/util/proto:walkmessages",
        "@com_github_golang_glog//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_artifacts_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "syncutils",
    srcs = ["syncutils.go"],
    importpath = "intrinsic/assets/syncutils",
    visibility = ["//intrinsic:internal_api_users"],
)

go_library(
    name = "orderedmap",
    srcs = ["orderedmap.go"],
    importpath = "intrinsic/assets/orderedmap",
    visibility = ["//intrinsic/assets:__subpackages__"],
)
