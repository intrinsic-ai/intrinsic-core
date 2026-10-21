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

load("//bazel:container.bzl", "container_image")
load("//bazel:go_macros.bzl", "go_binary", "go_library")

filegroup(
    name = "runtime_db_template",
    srcs = ["runtime_db.yaml"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

go_library(
    name = "resourceregistry",
    srcs = ["resource_registry.go"],
    importpath = "intrinsic/resources/service/resourceregistry",
    visibility = ["//visibility:private"],
    deps = [
        ":resourceregistryutil",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/skills/internal:skillcomposer",
        "//intrinsic/util/pagination:pagetoken",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_binary(
    name = "resource_registry_main",
    srcs = ["resource_registry_main.go"],
    importpath = "intrinsic/resources/service/resource_registry_main",
    deps = [
        ":resourcereader",
        ":resourceregistry",
        ":resourcetyperuntime",
        "//intrinsic/assets/internal:assetinfointernal",
        "//intrinsic/assets/proto/v1alpha1:asset_info_internal_go_proto",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/resources/proto:resource_registry_internal_go_proto",
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "//intrinsic/skills/internal:skillcomposer",
        "//intrinsic/stats/go:telemetry",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:resource_set_service_go_proto",
        "//intrinsic/util/go:shutdown",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@io_k8s_client_go//informers:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//rest:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

container_image(
    name = "resource-registry-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [":resource_registry_main"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

go_library(
    name = "resourceregistryutil",
    srcs = ["resource_registry_util.go"],
    importpath = "intrinsic/resources/service/resourceregistryutil",
    visibility = [
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/assets/instances:__pkg__",
    ],
    deps = [
        "//intrinsic/util/pagination:pagetoken",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_x_exp//maps",
    ],
)

go_library(
    name = "resourcereader",
    srcs = ["resourcereader.go"],
    importpath = "intrinsic/resources/service/resourcereader",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":handles",
        ":resourcetyperuntime",
        "//intrinsic/assets/services:inspection",
        "//intrinsic/config/proto:resource_set_go_proto",
        "//intrinsic/resources/proto:geometric_resource_data_go_proto",
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:resource_set_service_go_proto",
        "//intrinsic/world/proto:object_world_updates_go_proto",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "resourceworld",
    srcs = ["resourceworld.go"],
    importpath = "intrinsic/resources/service/resourceworld",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic/config/proto:resource_set_go_proto",
        "//intrinsic/resources/proto:geometric_resource_data_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:resource_set_service_go_proto",
        "//intrinsic/world/proto:object_world_service_go_proto",
        "//intrinsic/world/proto:object_world_updates_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_library(
    name = "handles",
    srcs = ["handles.go"],
    importpath = "intrinsic/resources/service/handles",
    deps = [
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/icon/control/parts/hal/force_torque_sensor_part:hal_force_torque_sensor_part_config_go_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:force_control_settings_go_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_go_proto",
        "@intrinsic_apis//intrinsic/icon/server/config:icon_main_config_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "runtimedb",
    srcs = ["runtimedb.go"],
    importpath = "intrinsic/resources/service/runtimedb",
    visibility = [
        "//intrinsic/conductor:__pkg__",
    ],
    deps = [
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "@io_etcd_go_bbolt//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_x_exp//slices",
    ],
)

go_binary(
    name = "runtimedbmain",
    srcs = ["runtimedbmain.go"],
    importpath = "intrinsic/resources/service/runtimedbmain",
    deps = [
        ":runtimedb",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "//intrinsic/stats/go:telemetry",
        "//intrinsic/util/go:shutdown",
        "@com_github_golang_glog//:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

container_image(
    name = "runtime-db-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [":runtimedbmain"],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)

go_library(
    name = "resourcetyperuntime",
    srcs = ["resourcetyperuntime.go"],
    importpath = "intrinsic/resources/service/resourcetyperuntime",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/conductor:__subpackages__",
        "//intrinsic/kubernetes/workcell_spec:__subpackages__",
        "//intrinsic/modified_solution:__subpackages__",
        "//intrinsic/skills/internal:__pkg__",
        "//intrinsic/skills/tools:__pkg__",
        "//intrinsic_sdk/intrinsic/skills/internal:__pkg__",
    ],
    deps = [
        "//intrinsic/resources:resourcefix",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "//intrinsic/util/grpc:statusutil",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "@com_github_golang_glog//:go_default_library",
        "@org_golang_google_grpc//codes",
        "@org_golang_google_grpc//status",
        "@org_golang_google_protobuf//proto",
    ],
)
