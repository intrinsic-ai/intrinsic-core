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
load("//bazel:go_macros.bzl", "go_binary")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "world",
    srcs = ["world.cc"],
    hdrs = ["world.h"],
    deps = [
        "//intrinsic/assets/proto/v1alpha1:asset_info_internal_cc_grpc",
        "//intrinsic/assets/proto/v1alpha1:asset_info_internal_cc_proto",
        "//intrinsic/conductor/proto:wrapper_bridge_cc_proto",
        "//intrinsic/resources/proto:geometric_resource_data_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/util/status:status_cc_proto",
    ],
)

cc_library(
    name = "save_scene_monitor",
    srcs = ["save_scene_monitor.cc"],
    hdrs = ["save_scene_monitor.h"],
    deps = [
        ":world",
        "//intrinsic/assets/proto/v1alpha1:asset_info_internal_cc_grpc",
        "//intrinsic/conductor/proto:save_status_cc_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:resource_set_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "conductor",
    srcs = ["conductor.cc"],
    hdrs = ["conductor.h"],
    deps = [
        ":execution_context",
        ":resource_world_wrapper_headers",
        ":save_scene_monitor",
        ":world",
        "//intrinsic/assets/proto/v1alpha1:asset_info_internal_cc_grpc",
        "//intrinsic/conductor/proto:wrapper_bridge_cc_proto",
        "//intrinsic/kubernetes/acl/cc:client_context",
        "//intrinsic/longrunning/cc:operation_scheduler",
        "//intrinsic/scene/config:scene_object_config_updater",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_cc_grpc_proto",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/world/conversion:scene_object_to_world_updates",
        "//intrinsic/world/service/updater:world_updater_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/conductor/proto:conductor_cc_grpc",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/util/status:get_extended_status",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "//intrinsic_sdk/intrinsic/util/thread:util",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "@abseil-cpp//absl/functional:function_ref",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_grpc",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_cc_grpc",
        "@intrinsic_apis//intrinsic/assets/scene_objects/proto:scene_object_manifest_cc_proto",
    ],
)

go_binary(
    name = "resource_world_wrapper_c",
    srcs = [
        "cgo_main.go",
        "init_c.go",
        "resource_world_wrapper.go",
    ],
    cdeps = [
        "//intrinsic/util/cgo:c_types",
    ],
    cgo = 1,
    importpath = "intrinsic/conductor/conductor_wrappers_c",
    linkmode = "c-archive",
    deps = [
        "//intrinsic/conductor/proto:wrapper_bridge_go_proto",
        "//intrinsic/config/proto:resource_set_go_proto",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/resources/proto:geometric_resource_data_go_proto",
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "//intrinsic/resources/service:resourcereader",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/resources/service:resourceworld",
        "//intrinsic/storage/hot_shared_state/proto/v1:resource_set_service_go_proto",
        "//intrinsic/util/cgo:cgoconvert",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)

cc_library(
    name = "resource_reader_interface",
    hdrs = ["resource_reader_interface.h"],
    deps = [
        "//intrinsic/config/proto:resource_set_cc_proto",
        "//intrinsic/resources/proto:geometric_resource_data_cc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_cc_proto",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "resource_world_wrapper_headers",
    hdrs = ["resource_world_wrapper.h"],
    deps = [
        ":resource_reader_interface",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "resource_world_wrapper_cc",
    srcs = [
        "resource_world_wrapper.cc",
    ],
    deps = [
        ":resource_world_wrapper_c",
        ":resource_world_wrapper_headers",
        "//intrinsic/conductor/proto:wrapper_bridge_cc_proto",
        "//intrinsic/util/cgo:cpp_types",
        "//intrinsic_sdk/intrinsic/conductor/proto:conductor_cc_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/time",
        "@com_google_googleapis//google/rpc:status_cc_proto",
    ],
)

cc_library(
    name = "execution_context",
    srcs = ["execution_context.cc"],
    hdrs = ["execution_context.h"],
    deps = [
        "//intrinsic/conductor/proto:execution_context_cc_proto",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)
