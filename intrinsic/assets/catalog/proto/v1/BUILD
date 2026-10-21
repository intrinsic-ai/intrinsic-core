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

load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

# AssetCatalog protos.

package(default_visibility = ["//visibility:public"])

alias(
    name = "asset_catalog_proto",
    actual = "//intrinsic_sdk/intrinsic/assets/catalog/proto/v1:asset_catalog_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "asset_catalog_go_proto",
    actual = "//intrinsic_sdk/intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

proto_library(
    name = "asset_catalog_internal_proto",
    srcs = ["asset_catalog_internal.proto"],
    deps = [
        ":asset_catalog_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_proto",
    ],
)

go_proto_library(
    name = "asset_catalog_internal_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/assets/catalog/proto/v1/asset_catalog_internal_go_proto",
    protos = [":asset_catalog_internal_proto"],
    deps = [
        ":asset_catalog_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
    ],
)
