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

# gazelle:ignore
package(default_visibility = ["//visibility:public"])

proto_library(
    name = "clustermanager_api_proto",
    srcs = ["clustermanager_api.proto"],
    deps = [
        "//intrinsic/config/proto:application_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-openapiv2/options:options_proto",
        "@com_google_googleapis//google/api:annotations_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:field_mask_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_proto",
        "@intrinsic_apis//intrinsic/kubernetes/intrinsic_os/protos/v1:cluster_proto",
        "@intrinsic_apis//intrinsic/kubernetes/intrinsic_os/protos/v1:dhcp_leases_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_proto",
    ],
)

go_proto_library(
    name = "clustermanager_api_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
        "//bazel:go_gen_grpc_gateway",
    ],
    importpath = "github.com/intrinsic-ai/insrc/incode/frontend/cloud/api/v1/clustermanager_api_go_proto",
    protos = [":clustermanager_api_proto"],
    deps = [
        "//intrinsic/config/proto:application_go_proto",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//protoc-gen-openapiv2/options",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/generator/proto:auth_options_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/intrinsic_os/protos/v1:cluster_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/intrinsic_os/protos/v1:dhcp_leases_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)
