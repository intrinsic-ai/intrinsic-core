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
load(
    "//intrinsic/assets/dependencies/build_defs:runtime_asset.bzl",
    "intrinsic_runtime_asset_instance",
    "intrinsic_runtime_installed_asset",
    "intrinsic_runtime_resource_instance",
)

go_library(
    name = "resolver",
    srcs = ["resolver.go"],
    importpath = "intrinsic/assets/dependencies/resolver",
    visibility = ["//intrinsic/assets/deploy:__pkg__"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "//intrinsic_sdk/intrinsic/util/proto:walkmessages",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:grpc_connection_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

cc_library(
    name = "resolver_cc",
    srcs = ["resolver.cc"],
    hdrs = ["resolver.h"],
    visibility = ["//intrinsic/executive/engine:__pkg__"],
    deps = [
        "//intrinsic/executive/engine:skill_action",
        "//intrinsic/util/proto:walk_messages",
        "//intrinsic_sdk/intrinsic/assets:id_utils",
        "//intrinsic_sdk/intrinsic/assets:interface_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:dependency_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:grpc_connection_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_cc_proto",
    ],
)

alias(
    name = "utils",
    actual = "//intrinsic_sdk/intrinsic/assets/dependencies:utils",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:public_api_users"],
)

go_library(
    name = "traversal",
    srcs = ["traversal.go"],
    importpath = "intrinsic/assets/dependencies/traversal",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":utils",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:dependency_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

alias(
    name = "platform",
    actual = "//intrinsic_sdk/intrinsic/assets/dependencies:platform",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = ["//intrinsic:__subpackages__"],
)

intrinsic_runtime_installed_asset(
    name = "runtime_installed_asset",
    visibility = ["//intrinsic:__subpackages__"],
)

intrinsic_runtime_asset_instance(
    name = "runtime_asset_instance",
    visibility = ["//intrinsic:__subpackages__"],
)

intrinsic_runtime_resource_instance(
    name = "runtime_resource_instance",
    visibility = ["//intrinsic:__subpackages__"],
)

go_library(
    name = "runtimeasset",
    srcs = ["runtimeasset.go"],
    embedsrcs = [
        ":runtime_asset_instance.pbtxt",
        ":runtime_installed_asset.pbtxt",
        ":runtime_resource_instance.pbtxt",
    ],
    importpath = "intrinsic/assets/dependencies/runtimeasset",
    visibility = ["//intrinsic:__subpackages__"],
    deps = [
        ":platform",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@org_golang_google_protobuf//encoding/prototext",
    ],
)

go_library(
    name = "errors",
    srcs = ["errors.go"],
    importpath = "intrinsic/assets/dependencies/errors",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:extstatus",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
    ],
)

go_library(
    name = "graph",
    srcs = ["graph.go"],
    importpath = "intrinsic/assets/dependencies/graph",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":errors",
        ":traversal",
        ":utils",
        "//intrinsic_sdk/intrinsic/assets:typeutils",
        "//intrinsic_sdk/intrinsic/assets/errors:report",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:dependency_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "graphutils",
    srcs = ["graphutils.go"],
    importpath = "intrinsic/assets/dependencies/graphutils",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "//intrinsic_sdk/intrinsic/util/proto:names",
    ],
)

go_library(
    name = "runtimegraph",
    srcs = ["runtimegraph.go"],
    importpath = "intrinsic/assets/dependencies/runtimegraph",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":graph",
        ":graphutils",
        ":runtimeasset",
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/executive/go:behaviortree",
        "//intrinsic_sdk/intrinsic/util/proto:names",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)
