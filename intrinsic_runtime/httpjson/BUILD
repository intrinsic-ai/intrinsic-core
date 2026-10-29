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

load("//bazel:container.bzl", "container_image", "container_layer")
load("//bazel:go_macros.bzl", "go_binary", "go_library")
load(
    "//intrinsic_sdk/intrinsic/httpjson/openapi:protoc_gen_openapi.bzl",
    "protoc_gen_openapi",
)

go_library(
    name = "arguments",
    srcs = ["arguments.go"],
    importpath = "intrinsic_runtime/httpjson/arguments",
    visibility = ["//visibility:public"],
)

go_library(
    name = "livelinesshandlers",
    srcs = ["livelinesshandlers.go"],
    importpath = "intrinsic_runtime/httpjson/livelinesshandlers",
    deps = [
        "@com_github_grpc_ecosystem_grpc_gateway_v2//runtime",
        "@org_golang_google_grpc//:grpc",
    ],
)

protoc_gen_openapi(
    name = "http_gateway_openapi",
    protos = [
        "//intrinsic_sdk/intrinsic/executive/proto:executive_service_proto",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_proto",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_proto",
        "//intrinsic_sdk/intrinsic/scene/proto/v1:scene_object_import_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_configuration_proto",
        "@intrinsic_apis//intrinsic/executive/proto:blackboard_service_proto",
        "@intrinsic_apis//intrinsic/executive/proto:proto_builder_proto",
        "@intrinsic_apis//intrinsic/kubernetes/intrinsic_os/protos/v1:cluster_service_proto",
        "@intrinsic_apis//intrinsic/kubernetes/inversion/v1:inversion_proto",
        "@intrinsic_apis//intrinsic/logging/proto:replay_service",
        "@intrinsic_apis//intrinsic/perception/proto/v1:train_service_proto",
        "@intrinsic_apis//intrinsic/proto_tools/proto:proto_registry_proto",
    ],
)

go_library(
    name = "openapihandler",
    srcs = ["openapihandler.go"],
    data = [":http_gateway_openapi"],
    importpath = "intrinsic_runtime/httpjson/openapihandler",
    deps = [
        "//intrinsic_sdk/intrinsic/httpjson/openapi:handlers",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//runtime",
    ],
)

go_library(
    name = "httphandlers",
    srcs = ["httphandlers.go"],
    importpath = "intrinsic_runtime/httpjson/httphandlers",
    deps = [
        ":arguments",
        ":livelinesshandlers",
        ":openapihandler",
        ":webpubsub",
        "//intrinsic_runtime/httpjson/webrtc",
        "//intrinsic_sdk/intrinsic/config:environments",
        "//intrinsic_sdk/intrinsic/executive/proto:executive_service_go_proto",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_go_proto",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_go_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_go_proto",
        "//intrinsic_sdk/intrinsic/platform/pubsub/golang:pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub/golang:pubsubinterface",
        "//intrinsic_sdk/intrinsic/scene/proto/v1:scene_object_import_go_proto",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_go_proto",
        "@com_github_gorilla_websocket//:go_default_library",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//runtime",
        "@com_github_pion_ice_v4//:go_default_library",
        "@com_github_pion_webrtc_v4//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_configuration_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:blackboard_service_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:proto_builder_go_proto",
        "@intrinsic_apis//intrinsic/httpjson/proto/v1alpha:controlplane_go_proto",
        "@intrinsic_apis//intrinsic/httpjson/proto/v1alpha:dataplane_go_proto",
        "@intrinsic_apis//intrinsic/httpjson/proto/v1alpha:signalling_go_proto",
        "@intrinsic_apis//intrinsic/httpjson/proto/v1alpha:webpubsub_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/intrinsic_os/connectivity_diagnostics/proto/v1:connectivity_diagnostics_service_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/intrinsic_os/protos/v1:cluster_service_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/inversion/v1:inversion_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:replay_service_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:train_service_go_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub/adapters:pubsub_go_proto",
        "@intrinsic_apis//intrinsic/proto_tools/proto:proto_registry_go_proto",
        "@io_k8s_api//core/v1:go_default_library",
        "@io_k8s_apimachinery//pkg/apis/meta/v1:go_default_library",
        "@io_k8s_client_go//kubernetes:go_default_library",
        "@io_k8s_client_go//rest:go_default_library",
        "@org_golang_google_grpc//:grpc",
        "@org_golang_google_grpc//credentials/insecure",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
        "@org_golang_google_protobuf//proto:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/structpb:go_default_library",
    ],
)

go_library(
    name = "webpubsub",
    srcs = [
        "webpubsub.go",
    ] + [],
    importpath = "intrinsic_runtime/httpjson/webpubsub",
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic_runtime/httpjson/webrtc",
        "//intrinsic_sdk/intrinsic/config:environments",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//runtime",
        "@intrinsic_apis//intrinsic/httpjson/proto/v1alpha:controlplane_go_proto",
        "@intrinsic_apis//intrinsic/httpjson/proto/v1alpha:dataplane_go_proto",
        "@intrinsic_apis//intrinsic/httpjson/proto/v1alpha:signalling_go_proto",
        "@intrinsic_apis//intrinsic/httpjson/proto/v1alpha:webpubsub_go_proto",
        "@org_golang_google_grpc//:grpc",
        "@org_golang_google_protobuf//types/known/emptypb",
    ] + [],
)

go_binary(
    name = "gateway",
    srcs = ["gateway.go"],
    data = [
        "//intrinsic/tools/introspect:gzmsgs_descriptor_set",
        "//intrinsic/tools/introspect:intrinsic_descriptor_set",
        "//intrinsic/tools/introspect:ros_descriptor_set",
    ],
    deps = [
        ":arguments",
        ":httphandlers",
        ":webpubsub",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic_runtime/httpjson/webrtc",
        "//intrinsic_sdk/intrinsic/httpjson/any",
        "//intrinsic_sdk/intrinsic/httpjson/serialization",
        "//intrinsic_sdk/intrinsic/platform/pubsub/golang:pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub/golang:pubsubinterface",
        "//intrinsic_sdk/intrinsic/stats/go:telemetry",
        "@com_github_grpc_ecosystem_grpc_gateway_v2//runtime",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
    ],
)

container_layer(
    name = "gateway_binary_layer",
    files = [":gateway"],
    include_runfiles = True,
)

container_image(
    name = "http-gateway-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    entrypoint = ["/gateway"],
    layers = [":gateway_binary_layer"],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)

filegroup(
    name = "http-gateway-template",
    srcs = ["http-gateway.yaml"],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
