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

load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:go_macros.bzl", "go_binary", "go_library", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "asset_proto",
    srcs = ["asset.proto"],
    deps = [
        "//intrinsic/world/proto:object_world_updates_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_proto",
    ],
)

py_proto_library(
    name = "asset_py_pb2",
    deps = [":asset_proto"],
)

go_proto_library(
    name = "asset_go_proto",
    importpath = "intrinsic/assets/build_defs/asset_go_proto",
    protos = [":asset_proto"],
    deps = [
        "//intrinsic/world/proto:object_world_updates_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
    ],
)

go_binary(
    name = "assetlocalinfogen",
    srcs = ["assetlocalinfogen.go"],
    importpath = "intrinsic/assets/build_defs/assetlocalinfogen",
    deps = [
        ":asset_go_proto",
        "//intrinsic/assets:typeutils",
        "//intrinsic/assets/processes/proto:process_manifest_go_proto",
        "//intrinsic/production:intrinsic",
        "//intrinsic/util:flag",
        "//intrinsic/util/proto:descriptor",
        "//intrinsic/util/proto:protoio",
        "//intrinsic/util/proto:registryutil",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_binary(
    name = "assetinstancegen",
    srcs = ["assetinstancegen.go"],
    embedsrcs = ["instance_config.textproto.tmpl"],
    importpath = "intrinsic/assets/build_defs/assetinstancegen",
    deps = [
        "//intrinsic/production:intrinsic",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_binary(
    name = "localsolutiongen",
    srcs = ["localsolutiongen.go"],
    importpath = "intrinsic/assets/build_defs/localsolutiongen",
    deps = [
        ":asset_go_proto",
        ":localsolution",
        "//intrinsic/production:intrinsic",
        "//intrinsic/util:flag",
        "//intrinsic/util/proto:protoio",
        "//intrinsic/world/proto:object_world_updates_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
    ],
)

go_library(
    name = "localsolution",
    srcs = ["localsolution.go"],
    importpath = "intrinsic/assets/build_defs/localsolution",
    deps = [
        ":asset_go_proto",
        "//intrinsic/assets:idutils",
        "//intrinsic/util/proto:protoio",
        "//intrinsic/util/proto:registryutil",
        "//intrinsic/world/proto:object_world_updates_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
    ],
)

go_library(
    name = "assetprocessor",
    srcs = ["assetprocessor.go"],
    importpath = "intrinsic/assets/build_defs/assetprocessor",
    deps = [
        ":asset_go_proto",
        "//intrinsic/assets:bundle",
        "//intrinsic/assets:idutils",
        "//intrinsic/assets:throttle",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:common_go_proto",
        "//intrinsic/config/proto:process_go_proto",
        "//intrinsic/config/proto:resource_set_go_proto",
        "//intrinsic/util/proto:registryutil",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "cascopyprocessor",
    srcs = ["cascopyprocessor.go"],
    importpath = "intrinsic/assets/build_defs/cascopyprocessor",
    deps = [
        "//intrinsic/assets:referenceddata",
        "//intrinsic/storage/content_addressable_storage/pkg/crossproject",
        "//intrinsic/storage/content_addressable_storage/pkg/primordial",
        "//intrinsic/tools/inctl/util:gcpauth",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_binary(
    name = "solution_update",
    srcs = ["solution_update.go"],
    importpath = "intrinsic/assets/build_defs/solution_update",
    deps = [
        ":asset_go_proto",
        ":assetprocessor",
        ":cascopyprocessor",
        "//intrinsic/assets:bundle",
        "//intrinsic/assets:clientutils",
        "//intrinsic/assets:cmdutils",
        "//intrinsic/assets:imagetransfer",
        "//intrinsic/assets:referenceddata",
        "//intrinsic/assets:throttle",
        "//intrinsic/assets/platformlevelswitch",
        "//intrinsic/assets/scene_objects:gzfprocessor",
        "//intrinsic/assets/services:bundleimages",
        "//intrinsic/config:operationmode",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:common_go_proto",
        "//intrinsic/frontend/solution_service/proto:solution_service_go_proto",
        "//intrinsic/kubernetes/workcell_spec/proto:deploy_go_proto",
        "//intrinsic/production:intrinsic",
        "//intrinsic/skills/tools/skill/cmd/directupload",
        "//intrinsic/storage/content_addressable_storage/pkg/filetocas",
        "//intrinsic/tools/inctl/cmd:apputil",
        "//intrinsic/tools/inctl/util:casgeometryuploader",
        "//intrinsic/tools/inctl/util:color",
        "//intrinsic/tools/inctl/util:gcpauth",
        "//intrinsic/tools/inctl/util:introspection",
        "//intrinsic/tools/inctl/util:releaseutil",
        "//intrinsic/tools/inctl/util:traceutil",
        "//intrinsic/util/go:build",
        "//intrinsic/util/path_resolver:pathresolver",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_artifacts_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/frontend/solution_service/proto:status_go_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_bazel_rules_go//go/runfiles:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)
