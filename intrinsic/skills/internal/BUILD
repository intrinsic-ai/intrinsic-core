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

# This directory should contain code that is used internally to our platform.
# Some files may still be exported as part of the Skills SDK, but they should
# not needed to be included directly when authoring a Skill.

load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = [
    "//intrinsic/apps:__subpackages__",
    "//intrinsic/assets/services:__subpackages__",
    "//intrinsic/executive:__subpackages__",
    "//intrinsic/frontend:__subpackages__",
    "//intrinsic/manipulation:__subpackages__",
    "//intrinsic/resources:__subpackages__",
    "//intrinsic/skills:__subpackages__",
    "//intrinsic/testing:__subpackages__",
    "//intrinsic_control/intrinsic/icon/skills:__subpackages__",
    "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
    "//intrinsic_runtime/intrinsic/proto_tools/registry:__subpackages__",
    "//intrinsic_sdk/intrinsic/executive:__subpackages__",
    "//intrinsic_sdk/intrinsic/skills:__subpackages__",
    "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
    "//incode/motion_planning/skills:__subpackages__",
])

alias(
    name = "error_utils",
    actual = "//intrinsic_sdk/intrinsic/skills/internal:error_utils",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

cc_library(
    name = "conflicts",
    srcs = ["conflicts.cc"],
    hdrs = ["conflicts.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
    ],
)

cc_library(
    name = "skill_service_client_interface",
    hdrs = ["skill_service_client_interface.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)

cc_library(
    name = "skill_service_client",
    srcs = ["skill_service_client.cc"],
    hdrs = ["skill_service_client.h"],
    deps = [
        ":error_utils",
        ":skill_service_client_interface",
        "//intrinsic/logging/utils:logger_labels",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/stats:tracing_utils",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/logging:data_logger_client",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_cc_proto",
        "//intrinsic_sdk/intrinsic/skills/cc:client_common",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_cc_proto",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/proto:any",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:concurrent_queue",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/logging/errors/proto:error_report_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
        "@io_opentelemetry_cpp//api",
    ],
)

cc_library(
    name = "kinematic_object_for_position_part",
    srcs = ["kinematic_object_for_position_part.cc"],
    hdrs = ["kinematic_object_for_position_part.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:kinematic_object",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
    ],
)

alias(
    name = "skill_registry_client",
    actual = "//intrinsic_sdk/intrinsic/skills/internal:skill_registry_client",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "skill_registry_client_interface",
    actual = "//intrinsic_sdk/intrinsic/skills/internal:skill_registry_client_interface",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = [
        "//intrinsic/apps:__subpackages__",
        "//intrinsic/assets/services:__subpackages__",
        "//intrinsic/executive:__subpackages__",
        "//intrinsic/frontend:__subpackages__",
        "//intrinsic/manipulation:__subpackages__",
        "//intrinsic/resources:__subpackages__",
        "//intrinsic/skills:__subpackages__",
        "//intrinsic/testing:__subpackages__",
        "//intrinsic_control/intrinsic/icon/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__pkg__",
        "//intrinsic_runtime/intrinsic/proto_tools/registry:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/cc:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
        "//incode/motion_planning/skills:__subpackages__",
    ],
)

cc_library(
    name = "world_service_utils",
    srcs = ["world_service_utils.cc"],
    hdrs = ["world_service_utils.h"],
    # Upload/download of full world protos should not be used widely.
    visibility = [
        "//intrinsic/skills/apps/icon2:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/cache:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/debug:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/nonvolatile_cache:__pkg__",
    ],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world:cc_proto",
        "//intrinsic/world/conversion:world_from_object_proto",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_deserializer",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_grpc_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_cc_proto",
    ],
)

go_library(
    name = "configmapwatcher",
    srcs = ["configmapwatcher.go"],
    importpath = "intrinsic/skills/internal/configmapwatcher",
    deps = [
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/fields:go_default_library",
        "@io_k8s_apimachinery//pkg/labels:go_default_library",
        "@io_k8s_client_go//informers:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//tools/cache:go_default_library",
    ],
)

go_library(
    name = "skillregistryservice",
    srcs = ["skillregistryservice.go"],
    importpath = "intrinsic/skills/internal/skillregistryservice",
    deps = [
        ":skillcomposer",
        ":skillfilter",
        ":skillruntime",
        "//intrinsic/skills/internal/proto:behavior_tree_registry_internal_go_proto",
        "//intrinsic/skills/internal/proto:skill_registry_internal_go_proto",
        "//intrinsic/skills/proto:skill_runtime_go_proto",
        "//intrinsic/util/pagination:pagetoken",
        "//intrinsic/util/proto:descriptorcompatibility",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/skills/proto:behavior_tree_registry_go_proto",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_registry_config_go_proto",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_registry_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_library(
    name = "skillinfo",
    srcs = ["skillinfo.go"],
    importpath = "intrinsic/skills/internal/skillinfo",
    deps = [
        "//intrinsic_sdk/intrinsic/skills/proto:skill_service_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_library(
    name = "skillfilter",
    srcs = ["skillfilter.go"],
    importpath = "intrinsic/skills/internal/skillfilter",
    deps = [
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "skillcomposer",
    srcs = ["skillcomposer.go"],
    importpath = "intrinsic/skills/internal/skillcomposer",
    visibility = [
        "//intrinsic/assets/internal:__pkg__",
        "//intrinsic/resources/service:__pkg__",
        "//intrinsic/skills/tools:__pkg__",
    ],
    deps = [
        ":configmapwatcher",
        ":skillinfo",
        "//intrinsic/assets:syncutils",
        "//intrinsic/assets/proto:installation_origin_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/util/proto:sourcecodeinfoview",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_go_proto",
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_client_go//informers:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb",
    ],
)

go_library(
    name = "skillruntime",
    srcs = ["skillruntime.go"],
    importpath = "intrinsic/skills/internal/skillruntime",
    visibility = [
        "//intrinsic/kubernetes/workcell_spec:__subpackages__",
        "//intrinsic/skills/tools:__subpackages__",
        "//intrinsic_sdk/intrinsic/skills/tools:__subpackages__",
    ],
    deps = [
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "//intrinsic/skills/proto:skill_runtime_go_proto",
        "//intrinsic_sdk/intrinsic/util/grpc:statusutil",
        "@org_golang_google_protobuf//proto",
    ],
)
