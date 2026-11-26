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
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")
load("//intrinsic_sdk/bazel:container.bzl", "container_image")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_binary", "go_library", "go_proto_library", "go_test")

package(default_visibility = [
    "//visibility:public",
])

genrule(
    name = "empty_spawner_config_pbtxt",
    srcs = [],
    outs = ["empty_spawner_config.pbtxt"],
    cmd = "echo [type.googleapis.com/intrinsic_proto.simulation.spawner.SpawnerConfiguration] {} > $@",
)

proto_library(
    name = "spawner_config_proto",
    srcs = ["spawner_config.proto"],
)

go_proto_library(
    name = "spawner_config_go_proto",
    importpath = "intrinsic/simulation/resources/spawner_config_go_proto",
    protos = [":spawner_config_proto"],
)

cc_proto_library(
    name = "spawner_config_cc_proto",
    deps = [":spawner_config_proto"],
)

py_proto_library(
    name = "spawner_config_py_pb2",
    deps = [":spawner_config_proto"],
)

go_library(
    name = "poserandomizer",
    srcs = ["poserandomizer.go"],
    importpath = "intrinsic/simulation/resources/poserandomizer",
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:point_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_test(
    name = "poserandomizer_test",
    srcs = ["poserandomizer_test.go"],
    embed = [":poserandomizer"],
    importpath = "intrinsic/simulation/resources/poserandomizer_test",
    deps = [
        "@intrinsic_apis//intrinsic/math/proto:point_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_go_proto",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_gonum_v1_gonum//num/quat:go_default_library",
    ],
)

go_binary(
    name = "spawner_server",
    srcs = ["spawner_server.go"],
    importpath = "intrinsic/simulation/resources/spawner_server",
    deps = [
        ":simspawnerserver",
        ":spawner_config_go_proto",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/simulation/gazebo/proto:spawner_service_go_proto",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_go_proto",
        "@intrinsic_apis//intrinsic/simulation/resources:spawner_server_go_proto",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//backoff:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_test(
    name = "simspawnerserver_test",
    srcs = ["simspawnerserver_test.go"],
    embed = [":simspawnerserver"],
    importpath = "intrinsic/simulation/resources/simspawnerserver_test",
    deps = [
        ":spawner_config_go_proto",
        "//intrinsic/simulation/gazebo/proto:spawner_service_go_proto",
        "@com_github_google_go_cmp//cmp:go_default_library",
        "@intrinsic_apis//intrinsic/math/proto:point_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
        "@intrinsic_apis//intrinsic/simulation/resources:spawner_server_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//testing/protocmp:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_library(
    name = "simspawnerserver",
    srcs = ["simspawnerserver.go"],
    importpath = "intrinsic/simulation/resources/simspawnerserver",
    deps = [
        ":poserandomizer",
        ":spawner_config_go_proto",
        "//intrinsic/simulation/gazebo/proto:spawner_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:point_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:quaternion_go_proto",
        "@intrinsic_apis//intrinsic/simulation/resources:spawner_server_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_gonum_v1_gonum//num/quat:go_default_library",
    ],
)

container_image(
    name = "spawner_server_image",
    base = "//intrinsic/kubernetes:base-image-oci",
    cmd = ["intrinsic/simulation/resources/spawner_server"],
    data_path = "/",
    files = [":spawner_server"],
)

intrinsic_service(
    name = "spawner_type",
    default_config = ":empty_spawner_config.pbtxt",
    images = [":spawner_server_image.tar"],
    manifest = "spawner_manifest.textproto",
    deps = [
        ":spawner_config_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/simulation/resources:spawner_server_proto",
    ],
)

genrule(
    name = "empty_outfeed_config_pbtxt",
    srcs = [],
    outs = ["empty_outfeed_config.pbtxt"],
    cmd = "echo [type.googleapis.com/intrinsic_proto.simulation.outfeed.OutfeedConfiguration] {} > $@",
)

proto_library(
    name = "outfeed_server_proto",
    srcs = ["outfeed_server.proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:vector3_proto"],
)

cc_proto_library(
    name = "outfeed_server_cc_proto",
    deps = [":outfeed_server_proto"],
)

cc_grpc_library(
    name = "outfeed_server_cc_grpc_proto",
    srcs = [":outfeed_server_proto"],
    grpc_only = True,
    deps = [":outfeed_server_cc_proto"],
)

proto_library(
    name = "outfeed_config_proto",
    srcs = ["outfeed_config.proto"],
)

go_proto_library(
    name = "outfeed_config_go_proto",
    importpath = "intrinsic/simulation/resources/outfeed_config_go_proto",
    protos = [":outfeed_config_proto"],
)

cc_proto_library(
    name = "outfeed_config_cc_proto",
    deps = [":outfeed_config_proto"],
)

go_proto_library(
    name = "outfeed_server_go_proto",
    compilers = [
        "@io_bazel_rules_go//proto:go_grpc_v2",
        "@io_bazel_rules_go//proto:go_proto",
    ],
    importpath = "intrinsic/simulation/resources/outfeed_server_go_proto",
    protos = [":outfeed_server_proto"],
    deps = ["@intrinsic_apis//intrinsic/math/proto:vector3_go_proto"],
)

go_binary(
    name = "outfeed_server",
    srcs = ["outfeed_server.go"],
    importpath = "intrinsic/simulation/resources/outfeed_server",
    deps = [
        ":outfeed_server_go_proto",
        ":simoutfeedserver",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic/simulation/gazebo/proto:outfeed_service_go_proto",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_go_proto",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//backoff:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

go_library(
    name = "simoutfeedserver",
    srcs = ["simoutfeedserver.go"],
    importpath = "intrinsic/simulation/resources/simoutfeedserver",
    deps = [
        ":outfeed_server_go_proto",
        "//intrinsic/simulation/gazebo/proto:outfeed_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_test(
    name = "simoutfeedserver_test",
    srcs = ["simoutfeedserver_test.go"],
    embed = [":simoutfeedserver"],
    importpath = "intrinsic/simulation/resources/simoutfeedserver_test",
    deps = [
        ":outfeed_config_go_proto",
        ":outfeed_server_go_proto",
        "//intrinsic/simulation/gazebo/proto:outfeed_service_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:vector3_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

container_image(
    name = "outfeed_server_image",
    base = "//intrinsic/kubernetes:base-image-oci",
    cmd = ["/outfeed_server"],
    files = [":outfeed_server"],
)

intrinsic_service(
    name = "outfeed_type",
    default_config = ":empty_outfeed_config.pbtxt",
    images = [":outfeed_server_image.tar"],
    manifest = "outfeed_manifest.textproto",
    deps = [
        ":outfeed_config_proto",
        ":outfeed_server_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
    ],
)
