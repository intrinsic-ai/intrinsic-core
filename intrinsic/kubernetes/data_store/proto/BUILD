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

# This package contains the protos which describe the API of the data store service.

load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = [
    "//visibility:public",
])

# application

application_visibility = [
    "//intrinsic/assets/build_defs:__pkg__",
    "//intrinsic/config:__pkg__",
    "//intrinsic/kubernetes/data_store:__subpackages__",
    "//intrinsic/kubernetes/workcell_spec:__pkg__",
    "//intrinsic/tools:__subpackages__",
    "//intrinsic/frontend:__subpackages__",
    "//intrinsic/storage/tools:__pkg__",
    "//intrinsic/solution_versions:__subpackages__",
    "//intrinsic/solution_copy:__subpackages__",
    "//incode/performance/dashboards:__subpackages__",
    "//incode/frontend:__subpackages__",
]

application_go_deps = [
    ":filter_spec_go_proto",
    "//intrinsic/config/proto:application_go_proto",
]

# cluster

cluster_visibility = [
    "//intrinsic/frontend/go/api:__pkg__",
    "//intrinsic/kubernetes/data_store:__subpackages__",
    "//intrinsic/kubernetes/workcell_spec:__pkg__",
    "//intrinsic/perception:__subpackages__",
    "//intrinsic/resources/service:__pkg__",
    "//intrinsic/skills:__subpackages__",
    "//intrinsic/testing/fleet_manager:__pkg__",
    "//intrinsic/tools:__subpackages__",
    "//intrinsic/world/public/objects:__subpackages__",
    "//intrinsic/world/service:__subpackages__",
    "//intrinsic/storage/hot_shared_state:__subpackages__",
]

proto_library(
    name = "cluster_service_proto",
    srcs = ["cluster_service.proto"],
    visibility = cluster_visibility,
    deps = [
        ":filter_spec_proto",
        "//intrinsic/config/proto:cluster_proto",
    ],
)

cluster_go_deps = [
    ":filter_spec_go_proto",
    "//intrinsic/config/proto:cluster_go_proto",
]

go_proto_library(
    name = "cluster_service_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/kubernetes/data_store/proto/cluster_service_go_proto",
    protos = [":cluster_service_proto"],
    visibility = cluster_visibility,
    deps = cluster_go_deps,
)

# resource_type

resource_type_visibility = [
    "//intrinsic:intrinsic_internal",
]

resource_type_go_deps = [
    ":filter_spec_go_proto",
    "//intrinsic/resources/proto:resource_type_go_proto",
]

# test_compute

test_compute_visibility = [
    "//intrinsic/kubernetes/data_store:__subpackages__",
    "//intrinsic/testing/dockerized_test_executor:__subpackages__",
    "//intrinsic/testing/fleet_manager:__pkg__",
    "//intrinsic/tools:__subpackages__",
]

test_compute_go_deps = [
    "//intrinsic/testing/dockerized_test_executor:test_compute_go_proto",
    "//intrinsic/kubernetes/data_store/proto:filter_spec_go_proto",
]

proto_library(
    name = "filter_spec_proto",
    srcs = ["filter_spec.proto"],
    visibility = ["//visibility:private"],
)

go_proto_library(
    name = "filter_spec_go_proto",
    importpath = "intrinsic/kubernetes/data_store/proto/filter_spec_go_proto",
    protos = [":filter_spec_proto"],
    visibility = ["//intrinsic/kubernetes/data_store:__subpackages__"],
)
