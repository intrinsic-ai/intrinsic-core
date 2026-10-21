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

load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:go_macros.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = [
    "//visibility:public",
])

proto_library(
    name = "resource_type_proto",
    srcs = ["resource_type.proto"],
    deps = [
        "//intrinsic/config/proto:version_proto",
        "//intrinsic/world/proto:world_fragment_proto",
        "@com_google_googleapis//google/api:field_behavior_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
    ],
)

go_proto_library(
    name = "resource_type_go_proto",
    importpath = "intrinsic/resources/proto/resource_type_go_proto",
    protos = [":resource_type_proto"],
    deps = [
        "//intrinsic/config/proto:version_go_proto",
        "//intrinsic/world/proto:world_fragment_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_go_proto",
        "@org_golang_google_genproto_googleapis_api//annotations",
    ],
)

proto_library(
    name = "resource_instance_proto",
    srcs = ["resource_instance.proto"],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@intrinsic_apis//intrinsic/config/proto:data_files_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_proto",
    ],
)

go_proto_library(
    name = "resource_instance_go_proto",
    importpath = "intrinsic/resources/proto/resource_instance_go_proto",
    protos = [":resource_instance_proto"],
    deps = [
        "@intrinsic_apis//intrinsic/config/proto:data_files_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_go_proto",
    ],
)

cc_proto_library(
    name = "resource_instance_cc_proto",
    deps = [":resource_instance_proto"],
)

proto_library(
    name = "resource_registry_internal_proto",
    srcs = ["resource_registry_internal.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_proto",
    ],
)

go_proto_library(
    name = "resource_registry_internal_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/resources/proto/resource_registry_internal_go_proto",
    protos = [":resource_registry_internal_proto"],
    visibility = ["//intrinsic/resources/service:__pkg__"],
    deps = [
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
    ],
)

proto_library(
    name = "resource_operational_status_proto",
    srcs = ["resource_operational_status.proto"],
)

cc_proto_library(
    name = "resource_operational_status_cc_proto",
    deps = [":resource_operational_status_proto"],
)

go_proto_library(
    name = "resource_operational_status_go_proto",
    importpath = "intrinsic/resources/proto/resource_operational_status_go_proto",
    protos = [":resource_operational_status_proto"],
)

proto_library(
    name = "resource_health_proto",
    srcs = ["resource_health.proto"],
    deps = [
        ":resource_operational_status_proto",
    ],
)

cc_proto_library(
    name = "resource_health_cc_proto",
    deps = [":resource_health_proto"],
)

cc_grpc_library(
    name = "resource_health_cc_grpc_proto",
    srcs = [":resource_health_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":resource_health_cc_proto"],
)

proto_library(
    name = "resource_type_runtime_proto",
    srcs = ["resource_type_runtime.proto"],
    deps = [
        "//intrinsic/assets/proto:installation_origin_proto",
        "//intrinsic/world/proto:world_fragment_proto",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:asset_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:descriptor_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_proto",
    ],
)

go_proto_library(
    name = "resource_type_runtime_go_proto",
    importpath = "intrinsic/resources/proto/resource_type_runtime_go_proto",
    protos = [":resource_type_runtime_proto"],
    deps = [
        "//intrinsic/assets/proto:installation_origin_go_proto",
        "//intrinsic/world/proto:world_fragment_go_proto",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:processed_skill_manifest_go_proto",
    ],
)

cc_proto_library(
    name = "resource_type_runtime_cc_proto",
    deps = [":resource_type_runtime_proto"],
)

proto_library(
    name = "runtime_db_proto",
    srcs = ["runtime_db.proto"],
    visibility = ["//visibility:private"],
    deps = [
        "@com_google_protobuf//:wrappers_proto",
    ],
)

cc_proto_library(
    name = "runtime_db_cc_proto",
    deps = [":runtime_db_proto"],
)

cc_grpc_library(
    name = "runtime_db_cc_grpc",
    srcs = [":runtime_db_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":runtime_db_cc_proto"],
)

go_proto_library(
    name = "runtime_db_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/resources/proto/runtime_db_go_proto",
    protos = [":runtime_db_proto"],
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/conductor:__subpackages__",
        "//intrinsic/kubernetes/workcell_spec:__subpackages__",
        "//intrinsic/resources/service:__pkg__",
        "//intrinsic/skills/internal:__pkg__",
        "//intrinsic/skills/tools:__pkg__",
        "//intrinsic_sdk/intrinsic/skills/internal:__pkg__",
    ],
)

proto_library(
    name = "geometric_resource_data_proto",
    srcs = ["geometric_resource_data.proto"],
    deps = [
        "//intrinsic/world/proto:object_world_updates_proto",
        "//intrinsic/world/proto:world_fragment_proto",
        "@com_google_protobuf//:any_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_proto",
    ],
)

go_proto_library(
    name = "geometric_resource_data_go_proto",
    importpath = "intrinsic/resources/proto/geometric_resource_data_go_proto",
    protos = [":geometric_resource_data_proto"],
    deps = [
        "//intrinsic/world/proto:object_world_updates_go_proto",
        "//intrinsic/world/proto:world_fragment_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
    ],
)

cc_proto_library(
    name = "geometric_resource_data_cc_proto",
    deps = [":geometric_resource_data_proto"],
)
