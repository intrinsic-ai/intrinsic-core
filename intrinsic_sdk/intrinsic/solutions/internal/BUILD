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

# Solution building Python libraries.

load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//intrinsic_sdk/intrinsic/solutions:__subpackages__"])

py_library(
    name = "process_providing",
    srcs = ["process_providing.py"],
    deps = [
        ":referenced_assets",
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_py_pb2",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_py_pb2",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/solutions:behavior_tree",
        "//intrinsic_sdk/intrinsic/solutions:providers",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_py_pb2",
    ],
)

py_library(
    name = "referenced_assets",
    srcs = ["referenced_assets.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_py_pb2",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic_sdk/intrinsic/executive/py:behavior_tree_visitor",
        "//intrinsic_sdk/intrinsic/solutions:behavior_tree",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_py_pb2",
    ],
)

py_library(
    name = "actions",
    srcs = ["actions.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/solutions:blackboard_value",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_call_py_pb2",
    ],
)

py_library(
    name = "code_execution",
    srcs = ["code_execution.py"],
    deps = [
        ":skill_utils",
        "//intrinsic_sdk/intrinsic/solutions:blackboard_value",
        "//intrinsic_sdk/intrinsic/solutions:proto_building",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_py_pb2",
    ],
)

py_library(
    name = "code_execution_client",
    srcs = ["code_execution_client.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/solutions:behavior_tree",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_info_service_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_info_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_py_pb2",
    ],
)

py_library(
    name = "blackboard",
    srcs = ["blackboard.py"],
    deps = [
        ":skill_utils",
        "//intrinsic_sdk/intrinsic/proto_tools/registry:proto_registry_client",
        "//intrinsic_sdk/intrinsic/solutions:blackboard_value",
        "//intrinsic_sdk/intrinsic/solutions:ipython",
        "//intrinsic_sdk/intrinsic/solutions:utils",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/executive/proto:blackboard_service_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:blackboard_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/util/status:extended_status_py_pb2",
    ],
)

py_library(
    name = "resources",
    srcs = ["resources.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client",
        "//intrinsic_sdk/intrinsic/solutions:provided",
        "//intrinsic_sdk/intrinsic/solutions:providers",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

py_library(
    name = "skill_utils",
    srcs = ["skill_utils.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/math/python:ros_proto_conversion",
        "//intrinsic_sdk/intrinsic/skills/client:skill_registry_client",
        "//intrinsic_sdk/intrinsic/solutions:blackboard_value",
        "//intrinsic_sdk/intrinsic/solutions:cel",
        "//intrinsic_sdk/intrinsic/solutions:pose_estimation",
        "//intrinsic_sdk/intrinsic/solutions:provided",
        "//intrinsic_sdk/intrinsic/solutions:utils",
        "//intrinsic_sdk/intrinsic/solutions:worlds",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors_py",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "//intrinsic_sdk/intrinsic/world/robot_payload/python:robot_payload",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:pose_py_pb2",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_py_pb2",
        "@intrinsic_apis//intrinsic/math/proto:pose_py_pb2",
        "@intrinsic_apis//intrinsic/motion_planning/proto:motion_target_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:robot_payload_py_pb2",
    ],
)

py_library(
    name = "skill_generation",
    srcs = ["skill_generation.py"],
    deps = [
        ":skill_utils",
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//intrinsic_sdk/intrinsic/assets/configuration:asset_configuration_client",
        "//intrinsic_sdk/intrinsic/solutions:blackboard_value",
        "//intrinsic_sdk/intrinsic/solutions:cel",
        "//intrinsic_sdk/intrinsic/solutions:provided",
        "//intrinsic_sdk/intrinsic/solutions:utils",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_call_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_py_pb2",
    ],
)

py_library(
    name = "skill_providing",
    srcs = ["skill_providing.py"],
    visibility = ["//intrinsic_sdk/intrinsic/solutions:__subpackages__"],
    deps = [
        ":resources",
        ":skill_generation",
        ":skill_utils",
        "//intrinsic_sdk/intrinsic/assets/configuration:asset_configuration_client",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client",
        "//intrinsic_sdk/intrinsic/skills/client:skill_registry_client",
        "//intrinsic_sdk/intrinsic/solutions:provided",
        "//intrinsic_sdk/intrinsic/solutions:providers",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors_py",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
    ],
)

py_library(
    name = "stubs",
    srcs = ["stubs.py"],
    deps = [
        ":skill_generation",
        ":skill_utils",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/solutions:provided",
        "//intrinsic_sdk/intrinsic/solutions:providers",
        "@ai_intrinsic_sdks_pip_deps//black",
        "@ai_intrinsic_sdks_pip_deps//typing_extensions",
    ],
)
