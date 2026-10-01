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

# Skills utilizing multiple cameras within the workcell.

load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//bazel:python.bzl", "py_library")
load("//intrinsic/skills/build_defs:skill.bzl", "py_skill", "skill_manifest")

package(default_visibility = [
    "//visibility:public",
])

######### estimate_pose_multi_view #########
proto_library(
    name = "estimate_pose_multi_view_proto",
    srcs = ["estimate_pose_multi_view.proto"],
    deps = [
        "@intrinsic_apis//intrinsic/geometry/proto:oriented_bounding_box_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_data_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimate_in_root_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    ],
)

py_proto_library(
    name = "estimate_pose_multi_view_py_pb2",
    deps = [":estimate_pose_multi_view_proto"],
)

go_proto_library(
    name = "estimate_pose_multi_view_go_proto",
    importpath = "intrinsic/perception/skills/multi_view/estimate_pose_multi_view_go_proto",
    proto = ":estimate_pose_multi_view_proto",
    deps = [
        "@intrinsic_apis//intrinsic/geometry/proto:oriented_bounding_box_go_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_data_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimate_in_root_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_go_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_go_proto",
    ],
)

py_library(
    name = "estimate_pose_multi_view",
    srcs = ["estimate_pose_multi_view.py"],
    deps = [
        ":estimate_pose_multi_view_py_pb2",
        ":multi_view_pose_utils",
        "//intrinsic/util:decorators",
        "//intrinsic_perception/intrinsic/perception/service/python:pose_estimation_client_utils",
        "//intrinsic_perception/intrinsic/perception/service/python:pose_estimation_service_client",
        "//intrinsic_sdk/intrinsic/platform/pubsub/python:pubsub",
        "//intrinsic_sdk/intrinsic/skills/python:skill_interface",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "//incode/ml/services/utils:otel_tracing",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@intrinsic_apis//intrinsic/geometry/proto:oriented_bounding_box_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimate_in_root_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimate_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_py_pb2",
    ] + [],
)

skill_manifest(
    name = "estimate_pose_multi_view_manifest",
    src = "estimate_pose_multi_view_manifest.textproto",
    incompatible_disallow_manifest_dependencies = False,
    deps = [":estimate_pose_multi_view_proto"],
)

py_skill(
    name = "estimate_pose_multi_view_skill",
    manifest = ":estimate_pose_multi_view_manifest",
    deps = [
        ":estimate_pose_multi_view",
        ":estimate_pose_multi_view_py_pb2",
    ],
)

######### Libraries #########
py_library(
    name = "multi_view_pose_utils",
    srcs = ["multi_view_pose_utils.py"],
    deps = [
        "//intrinsic/geometry/api/python:conversion_utils",
        "//intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic/world/python:geometry_component_utils",
        "//intrinsic/world/python:geometry_types",
        "//intrinsic_perception/intrinsic/perception/client/v1/python/camera:data_classes",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/platform/pubsub/python:pubsub",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@ai_intrinsic_sdks_pip_deps//opencv_python_headless",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/geometry/proto/v1:geometry_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_data_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:capture_result_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimate_in_root_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:target_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:footprint_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_py_pb2",
    ],
)
