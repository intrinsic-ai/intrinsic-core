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

load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

go_library(
    name = "utils",
    srcs = ["utils.go"],
    importpath = "intrinsic/assets/dependencies/utils",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":platform",
        "//intrinsic_sdk/intrinsic/assets/instances/connect",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:grpc_connection_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

cc_library(
    name = "utils_cc",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":platform_cc",
        "//intrinsic_sdk/intrinsic/assets/instances/connect:connect_cc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:dependency_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:grpc_connection_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
    ],
)

py_library(
    name = "utils_py",
    srcs = ["utils.py"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":platform_py",
        "//intrinsic_sdk/intrinsic/assets/instances/connect:connect_py",
        "//intrinsic_sdk/intrinsic/util/grpc:interceptor",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_py_pb2",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
    ],
)

go_library(
    name = "platform",
    srcs = ["platform.go"],
    importpath = "intrinsic/assets/dependencies/platform",
    visibility = [
        "//intrinsic:__subpackages__",
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/assets/dependencies:__pkg__",
        "//intrinsic_sdk/intrinsic/assets:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills:__subpackages__",
    ],
    deps = [
        "//intrinsic/assets/hardware_devices:graph",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

cc_library(
    name = "platform_cc",
    hdrs = ["platform.h"],
    visibility = [
        "//intrinsic:__subpackages__",
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/assets/dependencies:__pkg__",
        "//intrinsic_sdk/intrinsic/assets:__subpackages__",
    ],
    deps = [
        "@abseil-cpp//absl/strings:string_view",
    ],
)

py_library(
    name = "platform_py",
    srcs = ["platform.py"],
    visibility = [
        "//intrinsic:__subpackages__",
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/assets/dependencies:__pkg__",
        "//intrinsic_sdk/intrinsic/assets:__subpackages__",
    ],
)
