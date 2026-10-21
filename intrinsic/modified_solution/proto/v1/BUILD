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
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

proto_library(
    name = "modified_solution_proto",
    srcs = ["modified_solution.proto"],
    visibility = [
        # Modified solution should not be added as dependencies to any other protos.
        "//intrinsic/solution_versions/proto/v1:__pkg__",
    ],
    deps = [
        "//intrinsic/config/proto:application_proto",
        "//intrinsic/config/proto:process_proto",
        "//intrinsic/resources/proto:resource_type_runtime_proto",
        "//intrinsic/skills/catalog/proto:skill_deployment_data_proto",
        "@intrinsic_apis//intrinsic/config/proto:data_files_proto",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_proto",
    ],
)

py_proto_library(
    name = "modified_solution_py_pb2",
    visibility = ["//visibility:public"],
    deps = [":modified_solution_proto"],
)

go_proto_library(
    name = "modified_solution_go_proto",
    importpath = "intrinsic/modified_solution/proto/v1/modified_solution_go_proto",
    protos = [":modified_solution_proto"],
    visibility = [
        "//intrinsic/assets:__pkg__",
        "//intrinsic/assets/conversion:__pkg__",
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/modified_solution:__subpackages__",
        "//intrinsic/solution_versions:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution/create:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/solution_version:__pkg__",
    ],
    deps = [
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:process_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/skills/catalog/proto:skill_deployment_data_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:data_files_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_go_proto",
    ],
)

proto_library(
    name = "modified_solution_metadata_proto",
    srcs = ["modified_solution_metadata.proto"],
    deps = ["@com_google_protobuf//:timestamp_proto"],
)

go_proto_library(
    name = "modified_solution_metadata_go_proto",
    importpath = "intrinsic/modified_solution/proto/v1/modified_solution_metadata_go_proto",
    protos = [":modified_solution_metadata_proto"],
    visibility = [
        "//intrinsic/modified_solution:__subpackages__",
        "//intrinsic/solution_versions:__subpackages__",
    ],
)

proto_library(
    name = "modified_solution_service_proto",
    srcs = ["modified_solution_service.proto"],
    deps = [
        ":modified_solution_metadata_proto",
        "@com_google_googleapis//google/longrunning:operations_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_proto",
    ],
)

go_proto_library(
    name = "modified_solution_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/modified_solution/proto/v1/modified_solution_service_go_proto",
    protos = [":modified_solution_service_proto"],
    visibility = [
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/frontend/solution_service:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/modified_solution:__subpackages__",
        "//intrinsic/tools/inctl/cmd:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:__subpackages__",
    ],
    deps = [
        ":modified_solution_metadata_go_proto",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/solution_versions/proto/v1:snapshot_go_proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)
