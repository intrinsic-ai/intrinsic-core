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

load("//bazel:go_macros.bzl", "go_library")

go_library(
    name = "privileges",
    srcs = ["privileges.go"],
    importpath = "intrinsic/assets/deploy/privileges",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
    ],
    deps = [
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_go_proto",
    ],
)

go_library(
    name = "render",
    srcs = [
        "render.go",
        "renderutil.go",
    ],
    data = [
        ":resources.yaml",
        ":skills.yaml",
    ],
    importpath = "intrinsic/assets/deploy/render",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/skills/internal:__pkg__",
        "//intrinsic_sdk/intrinsic/skills/internal:__pkg__",
    ],
    deps = [
        "//intrinsic/assets/dependencies:resolver",
        "//intrinsic/assets/proto:installation_origin_go_proto",
        "//intrinsic/assets/services:inspection",
        "//intrinsic/config:convertrunnables",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/kubernetes/workcell_spec:chartassignment",
        "//intrinsic/kubernetes/workcell_spec:chartbuilder",
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/skills/catalog/proto:skill_deployment_data_go_proto",
        "//intrinsic/util/go:xiter",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets/services:servicevalidate",
        "//intrinsic_sdk/intrinsic/util/go:pointer",
        "//intrinsic_sdk/intrinsic/util/go:validate",
        "//intrinsic_sdk/intrinsic/util/path_resolver:pathresolver",
        "//intrinsic_sdk/intrinsic/util/proto:names",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/apis/apps/v1alpha1:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_permissions_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_volume_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_permissions_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_service_definition_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_volume_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_go_proto",
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/api/resource:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_sigs_yaml//:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_x_exp//maps",
    ],
)

filegroup(
    name = "render_templates",
    srcs = [
        ":resources.yaml",
        ":skills.yaml",
    ],
    visibility = ["//intrinsic/kubernetes/workcell_spec:__pkg__"],
)

go_library(
    name = "resourcefixer",
    srcs = ["resourcefixer.go"],
    importpath = "intrinsic/assets/deploy/resourcefixer",
    visibility = [
        "//intrinsic/resources/service:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/util/go:pointer",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_drivers_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_identifier_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "resourcewriter",
    srcs = ["resourcewriter.go"],
    importpath = "intrinsic/assets/deploy/resourcewriter",
    visibility = ["//intrinsic/kubernetes/workcell_spec:__pkg__"],
    deps = [
        ":privileges",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_internal_go_proto",
        "//intrinsic/assets/dependencies:errors",
        "//intrinsic/assets/dependencies:graph",
        "//intrinsic/assets/dependencies:runtimegraph",
        "//intrinsic/assets/deploy:resourcefixer",
        "//intrinsic/assets/install:gather",
        "//intrinsic/assets/install:preload",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:resource_set_go_proto",
        "//intrinsic/kubernetes/workcell_spec:runtimedbtransfer",
        "//intrinsic/longrunning/go:operations",
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/service:resourcereader",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic/util/proto:descriptorcompatibility",
        "//intrinsic/world/proto:object_world_service_go_proto",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:typeutils",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:data_files_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "assetdeploymentservice",
    srcs = ["asset_deployment_service.go"],
    importpath = "intrinsic/assets/deploy/assetdeploymentservice",
    visibility = ["//intrinsic/kubernetes/workcell_spec:__pkg__"],
    deps = [
        ":privileges",
        ":render",
        ":resourcefixer",
        ":resourcewriter",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_internal_go_proto",
        "//intrinsic/assets/dependencies:graph",
        "//intrinsic_sdk/intrinsic/assets/dependencies:platform",
        "//intrinsic/assets/dependencies:resolver",
        "//intrinsic/assets/dependencies:runtimegraph",
        "//intrinsic_sdk/intrinsic/assets/errors:report",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        # LINT.IfChange(dynamic_reconfiguration_versions)
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_go_proto",
        # LINT.ThenChange(//intrinsic/assets/deploy/renderutil.go:supported_dynamic_reconfiguration_versions)
        "//intrinsic/config/proto:application_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:data_files_go_proto",
        "//intrinsic_sdk/intrinsic/kubernetes/acl:clientcontext",
        "//intrinsic/kubernetes/workcell_spec:runtimedbtransfer",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "//intrinsic/longrunning/go:operations",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_go_proto",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/resources/service:resourceworld",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_config_go_proto",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
        "//intrinsic/world/proto:object_world_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)
