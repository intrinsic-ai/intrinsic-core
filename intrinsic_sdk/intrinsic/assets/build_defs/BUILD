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
load("//bazel:go_macros.bzl", "go_binary", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "asset_proto",
    srcs = ["asset.proto"],
    deps = [
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
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
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
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
        "//intrinsic_sdk/intrinsic/assets:typeutils",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_manifest_go_proto",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util:flag",
        "//intrinsic_sdk/intrinsic/util/proto:descriptor",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
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
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)
