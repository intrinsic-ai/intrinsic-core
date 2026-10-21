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

load("@bazel_lib//lib:expand_template.bzl", "expand_template")
load("//bazel:go_macros.bzl", "go_binary", "go_library")

expand_template(
    name = "version",
    out = "version.txt",
    stamp_substitutions = {"VERSION-PLACEHOLDER": "{{BUILD_EMBED_LABEL}}"},
    substitutions = {"VERSION-PLACEHOLDER": ""},
    template = "version.txt.template",
    visibility = ["//visibility:public"],
)

go_library(
    name = "runtimeasset",
    srcs = ["runtimeasset.go"],
    importpath = "intrinsic/assets/dependencies/build_defs/runtimeasset",
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic/assets/conversion:runtime",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:platform",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:documentation_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:vendor_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:grpc_connection_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_binary(
    name = "runtimeinstalledassetgen_main",
    srcs = ["runtimeinstalledassetgen_main.go"],
    importpath = "intrinsic/assets/dependencies/build_defs/runtimeinstalledassetgen_main",
    visibility = ["//intrinsic/assets/dependencies:__pkg__"],
    deps = [
        ":runtimeasset",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
    ],
)

go_binary(
    name = "runtimeassetinstancegen_main",
    srcs = ["runtimeassetinstancegen_main.go"],
    importpath = "intrinsic/assets/dependencies/build_defs/runtimeassetinstancegen_main",
    visibility = ["//intrinsic/assets/dependencies:__pkg__"],
    deps = [
        ":runtimeasset",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
    ],
)

go_binary(
    name = "runtimeresourceinstancegen_main",
    srcs = ["runtimeresourceinstancegen_main.go"],
    importpath = "intrinsic/assets/dependencies/build_defs/runtimeresourceinstancegen_main",
    visibility = ["//intrinsic/assets/dependencies:__pkg__"],
    deps = [
        ":runtimeasset",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
    ],
)
