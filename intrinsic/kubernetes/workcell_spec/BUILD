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
    name = "workcell-cluster-service-template",
    srcs = [
        "templates/workcell-cluster-service.yaml",
    ],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)

container_image(
    name = "workcell-spec-service-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [
        ":workcell_cluster_service",
        "//intrinsic/assets/deploy:render_templates",
    ],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)

go_binary(
    name = "workcell_cluster_service",
    embed = [":workcell_cluster_service_lib"],
    importpath = "intrinsic/kubernetes/workcell_spec/workcell_cluster_service",
)

go_library(
    name = "workcell_cluster_service_lib",
    srcs = ["workcell_cluster_service.go"] + [],
    importpath = "intrinsic/kubernetes/workcell_spec/workcell_cluster_service",
    deps = [
        ":chartassignment",
        ":deployservice",
        ":transfersvc",
        "//intrinsic/assets/artifacts:assetartifactsservice",
        "//intrinsic/assets/artifacts:processor",
        "//intrinsic/assets/artifacts:uploader",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_internal_go_proto",
        "//intrinsic/assets/configuration:assetconfigurationservice",
        "//intrinsic/assets/data:dataassetsservice",
        "//intrinsic/assets/deploy:assetdeploymentservice",
        "//intrinsic/assets/deploy:privileges",
        "//intrinsic/assets/deploy:render",
        "//intrinsic/assets/install:installedassetsservice",
        "//intrinsic/assets/instances:assetinstancesservice",
        "//intrinsic/assets/services:systemservicestate",
        "//intrinsic/conductor/proto:conductor_go_proto",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/kubernetes/clusterconfig",
        "//intrinsic/kubernetes/data_store/proto:cluster_service_go_proto",
        "//intrinsic/kubernetes/workcell_spec/proto:deploy_go_proto",
        "//intrinsic/kubernetes/workcell_spec/proto:solution_internal_go_proto",
        "//intrinsic/logging/proto:logger_service_go_proto",
        "//intrinsic/longrunning/go:operations",
        "//intrinsic/resources/catalog/proto:resource_catalog_internal_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/proto:runtime_db_go_proto",
        "//intrinsic/resources/service:resourcereader",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/resources/service:resourceworld",
        "//intrinsic/simulation/service/proto/first_party:simulation_service_go_proto",
        "//intrinsic/skills/internal:skillruntime",
        "//intrinsic/solution_versions/proto/v1:solution_version_service_go_proto",
        "//intrinsic/stats/go:telemetry",
        "//intrinsic/storage/content_addressable_storage/pkg/observability",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:cluster_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:resource_set_service_go_proto",
        "//intrinsic/util/go:shutdown",
        "//intrinsic/world/proto:object_world_service_go_proto",
        "//intrinsic_sdk/intrinsic/assets:baseclientutils",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_deployment_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/apis/apps/v1alpha1:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/client/versioned:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_deployment_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_artifacts_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_configuration_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:system_service_state_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_client_go//informers",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//rest:go_default_library",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
        "@org_golang_x_sync//errgroup:go_default_library",
    ] + [],
)

go_library(
    name = "workcellspec",
    srcs = ["transfer.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/workcellspec",
    visibility = ["//intrinsic/kubernetes:intrinsic_kubernetes_users"],
    deps = [
        "@com_github_pkg_errors//:go_default_library",
        "@in_gopkg_yaml_v3//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
    ],
)

go_library(
    name = "repositorybasename",
    srcs = ["repository_basename.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/repositorybasename",
    visibility = [
        "//visibility:public",
    ],
)

go_binary(
    name = "chartassignmentgen",
    srcs = ["chartassignmentgen.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/chartassignmentgen",
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic/production:intrinsic",
        "//intrinsic/util:flag",
        "@com_github_golang_glog//:go_default_library",
    ],
)

go_library(
    name = "transfersvc",
    srcs = [
        "transfer_service.go",
    ],
    importpath = "intrinsic/kubernetes/workcell_spec/transfersvc",
    visibility = ["//intrinsic/kubernetes:intrinsic_kubernetes_users"],
    deps = [
        ":chartassignment",
        "//intrinsic/assets/deploy:render",
        "//intrinsic/config:environments",
        "//intrinsic/kubernetes/workcell_spec/proto:solution_internal_go_proto",
        "//intrinsic/stats/go:telemetry",
        "@cat_dario_mergo//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/apis/apps/v1alpha1:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/client/versioned:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "@io_k8s_api//apps/v1:go_default_library",
        "@io_k8s_api//batch/v1:go_default_library",
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/api/errors:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_library(
    name = "author",
    srcs = ["author.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/author",
    visibility = ["//intrinsic/kubernetes:intrinsic_kubernetes_users"],
    deps = [
        "@com_github_minio_highwayhash//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
    ],
)

go_library(
    name = "runtimedbtransfer",
    srcs = ["runtimedbtransfer.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/runtimedbtransfer",
    visibility = ["//intrinsic/kubernetes:intrinsic_kubernetes_users"],
    deps = [
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_internal_go_proto",
        "//intrinsic/assets/conversion:runtime",
        "//intrinsic/assets/deploy:privileges",
        "//intrinsic/assets/install:gather",
        "//intrinsic/assets/install:localconv",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/resources/catalog/proto:resource_catalog_internal_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/skills/internal:skillruntime",
        "//intrinsic/skills/proto:skill_runtime_go_proto",
        "//intrinsic/util/go:xiter",
        "//intrinsic/util/grpc:statusutil",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_library(
    name = "chartassignment",
    srcs = ["chartassignment.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/chartassignment",
    visibility = [
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/kubernetes:__pkg__",
        "//intrinsic/tools/inctl:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl:__subpackages__",
    ],
    deps = [
        "//intrinsic/kubernetes/appstat:inspect",
        "//intrinsic/stats/go:telemetry",
        "@cat_dario_mergo//:go_default_library",
        "@com_github_bazelbuild_buildtools//labels:go_default_library",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_google_safearchive//tar",
        "@com_github_googlecloudrobotics_core//src/go/pkg/apis/apps/v1alpha1:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/client/versioned:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "@io_k8s_apimachinery//pkg/api/errors:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/util/wait:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_sigs_yaml//:go_default_library",
        "@io_opentelemetry_go_otel//attribute",
        "@io_opentelemetry_go_otel//propagation",
        "@io_opentelemetry_go_otel_trace//:trace",
    ],
)

go_library(
    name = "chartbuilder",
    srcs = ["chartbuilder.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/chartbuilder",
    visibility = ["//intrinsic/assets/deploy:__pkg__"],
    deps = [
        "@com_github_google_safetext//yamltemplate:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@in_gopkg_yaml_v3//:go_default_library",
    ],
)

go_binary(
    name = "valuesgen",
    srcs = ["valuesgen_core.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/valuesgen",
    visibility = ["//visibility:public"],
    deps = [] + [
        "//intrinsic/kubernetes:values",
        "//intrinsic/production:intrinsic",
        "//intrinsic/util:flag",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_googlecloudrobotics_core//src/go/pkg/apis/apps/v1alpha1:go_default_library",
        "@in_gopkg_yaml_v3//:go_default_library",
    ],
)

go_binary(
    name = "imagelistgen",
    srcs = ["imagelistgen.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/imagelistgen",
    visibility = ["//visibility:public"],
    deps = [
        ":repositorybasename",
        "//intrinsic/production:intrinsic",
        "@com_github_golang_glog//:go_default_library",
    ],
)

go_binary(
    name = "digester",
    srcs = ["digester.go"],
    importpath = "intrinsic/kubernetes/workcell_spec/digester",
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic/production:intrinsic",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/tarball:go_default_library",
    ],
)

go_library(
    name = "deployservice",
    srcs = [
        "deploy_service.go",
    ] + [],
    importpath = "intrinsic/kubernetes/workcell_spec/deployservice",
    deps = [
        ":runtimedbtransfer",
        ":transfersvc",
        "//intrinsic/assets/catalog/proto/v1:asset_catalog_internal_go_proto",
        "//intrinsic/assets/conversion:solutionconversion",
        "//intrinsic/assets/dependencies:graph",
        "//intrinsic/assets/dependencies:runtimegraph",
        "//intrinsic/assets/deploy:privileges",
        "//intrinsic/assets/deploy:render",
        "//intrinsic/assets/instances:instanceconversion",
        "//intrinsic/assets/proto:installation_origin_go_proto",
        "//intrinsic/conductor/proto:conductor_go_proto",
        "//intrinsic/config:applicationview",
        "//intrinsic/config:convertrunnables",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:cluster_go_proto",
        "//intrinsic/config/proto:common_go_proto",
        "//intrinsic/config/proto:process_go_proto",
        "//intrinsic/config/proto:resource_set_go_proto",
        "//intrinsic/kubernetes/acl:clientcontext",
        "//intrinsic/kubernetes/data_store/proto:cluster_service_go_proto",
        "//intrinsic/kubernetes/workcell_spec/proto:deploy_go_proto",
        "//intrinsic/logging/proto:logger_service_go_proto",
        "//intrinsic/longrunning/go:operations",
        "//intrinsic/modified_solution/proto/v1:modified_solution_go_proto",
        "//intrinsic/resources/catalog/proto:resource_catalog_internal_go_proto",
        "//intrinsic/resources/proto:resource_instance_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/skills/internal:skillruntime",
        "//intrinsic/skills/proto:skill_registry_config_go_proto",
        "//intrinsic/skills/proto:skill_runtime_go_proto",
        "//intrinsic/solution_versions/proto/v1:solution_version_service_go_proto",
        "//intrinsic/stats/go:telemetry",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic/storage/hot_shared_state/proto/v1:cluster_service_go_proto",
        "//intrinsic/util/go:pointer",
        "//intrinsic/util/go:xiter",
        "//intrinsic/util/proto:descriptorcompatibility",
        "//intrinsic/world/proto:object_world_updates_go_proto",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:viewutils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:platform",
        "//intrinsic_sdk/intrinsic/assets/errors:report",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_deployment_go_proto",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_go_proto",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_instances_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:instance_config_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:data_files_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/emptypb",
    ] + [],
)

alias(
    name = "imagetags",
    actual = "//intrinsic_sdk/intrinsic/kubernetes/workcell_spec:imagetags",
    tags = ["go/intrinsic-ioc-transition-tdd"],
    visibility = [
        "//visibility:public",
    ],
)
