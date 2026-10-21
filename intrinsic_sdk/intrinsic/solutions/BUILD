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

package(default_visibility = [
    "//visibility:public",
])

py_library(
    name = "behavior_tree",
    srcs = ["behavior_tree.py"],
    deps = [
        ":blackboard_value",
        ":cel",
        ":errors",
        ":ipython",
        ":proto_building",
        ":utils",
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//intrinsic_sdk/intrinsic/assets/processes/proto:process_asset_py_pb2",
        "//intrinsic_sdk/intrinsic/solutions/internal:actions",
        "//intrinsic_sdk/intrinsic/solutions/internal:code_execution",
        "//intrinsic_sdk/intrinsic/solutions/internal:skill_utils",
        "//intrinsic_sdk/intrinsic/solutions/internal/bt:bt_util",
        "//intrinsic_sdk/intrinsic/solutions/internal/bt:world_query",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors_py",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@ai_intrinsic_sdks_pip_deps//graphviz",
        "@com_google_cel_spec//proto/cel/expr:syntax_py_pb2",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:documentation_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:vendor_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:any_list_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_call_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:world_query_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:skills_py_pb2",
        "@intrinsic_apis//intrinsic/util/status:extended_status_py_pb2",
    ],
)

py_library(
    name = "blackboard_value",
    srcs = ["blackboard_value.py"],
    deps = [
        "@com_google_protobuf//:protobuf_python",
    ],
)

py_library(
    name = "cel",
    srcs = ["cel.py"],
)

py_library(
    name = "deployments",
    srcs = ["deployments.py"],
    deps = [
        ":error_processing",
        ":errors",
        ":execution",
        ":ipython",
        ":pbt_registration",
        ":pose_estimation",
        ":proto_building",
        ":providers",
        ":simulation",
        ":structured_logging",
        ":worlds",
        "//intrinsic_sdk/intrinsic/assets/configuration:asset_configuration_client",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_py_pb2",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/proto_tools/registry:proto_registry_client",
        "//intrinsic_sdk/intrinsic/resources/client:resource_registry_client",
        "//intrinsic_sdk/intrinsic/skills/client:skill_registry_client",
        "//intrinsic_sdk/intrinsic/solutions/internal:code_execution_client",
        "//intrinsic_sdk/intrinsic/solutions/internal:process_providing",
        "//intrinsic_sdk/intrinsic/solutions/internal:resources",
        "//intrinsic_sdk/intrinsic/solutions/internal:skill_providing",
        "//intrinsic_sdk/intrinsic/solutions/internal:stubs",
        "//intrinsic_sdk/intrinsic/util/grpc:auth_py",
        "//intrinsic_sdk/intrinsic/util/grpc:dialerutil",
        "//intrinsic_sdk/intrinsic/util/grpc:userconfig",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/frontend/solution_service/proto:status_py_pb2",
    ],
)

py_library(
    name = "errors",
    srcs = ["errors.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//retrying",
    ],
)

py_library(
    name = "error_processing",
    srcs = ["error_processing.py"],
    deps = [
        ":errors",
        ":ipython",
        "@com_google_googleapis//google/longrunning:operations_py_proto",
        "@com_google_googleapis//google/rpc:code_py_proto",
        "@intrinsic_apis//intrinsic/logging/errors/proto:error_report_py_pb2",
    ],
)

py_library(
    name = "execution",
    srcs = ["execution.py"],
    deps = [
        ":behavior_tree",
        ":blackboard_value",
        ":error_processing",
        ":errors",
        ":ipython",
        ":provided",
        ":structured_logging",
        ":utils",
        ":worlds",
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//intrinsic_sdk/intrinsic/executive/proto:executive_service_py_pb2",
        "//intrinsic_sdk/intrinsic/executive/proto:executive_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/executive/proto:run_metadata_py_pb2",
        "//intrinsic_sdk/intrinsic/proto_tools/registry:proto_registry_client",
        "//intrinsic_sdk/intrinsic/solutions/internal:blackboard",
        "//intrinsic_sdk/intrinsic/solutions/internal:skill_utils",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "//intrinsic_sdk/intrinsic/util/status:status_exception",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_googleapis//google/longrunning:operations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:blackboard_service_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:blackboard_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/executive/proto:executive_execution_mode_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:run_response_py_pb2",
        "@intrinsic_apis//intrinsic/util/status:extended_status_py_pb2",
    ],
)

py_library(
    name = "ipython",
    srcs = ["ipython.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_exception",
        "@ai_intrinsic_sdks_pip_deps//ipywidgets",
        "@intrinsic_apis//intrinsic/util/status:extended_status_py_pb2",
    ],
)

py_library(
    name = "pose_estimation",
    srcs = ["pose_estimation.py"],
    deps = [
        ":ipython",
        ":structured_logging",
        "//intrinsic_sdk/intrinsic/assets:interface_utils_py",
        "//intrinsic_sdk/intrinsic/assets/install:installed_assets_client",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
        "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_py_pb2",
    ],
)

py_library(
    name = "proto_building",
    srcs = ["proto_building.py"],
    deps = [
        ":errors",
        ":provided",
        "//intrinsic_sdk/intrinsic/solutions/internal:skill_utils",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/executive/proto:proto_builder_py_pb2",
        "@intrinsic_apis//intrinsic/executive/proto:proto_builder_py_pb2_grpc",
    ],
)

py_library(
    name = "pbt_registration",
    srcs = ["pbt_registration.py"],
    deps = [
        ":behavior_tree",
        "//intrinsic_sdk/intrinsic/skills/proto:behavior_tree_registry_py_pb2",
        "//intrinsic_sdk/intrinsic/skills/proto:behavior_tree_registry_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/skills/proto:skill_registry_config_py_pb2",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_py_pb2",
    ],
)

py_library(
    name = "simulation",
    srcs = ["simulation.py"],
    deps = [
        ":errors",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_py_pb2",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulation_service_py_pb2",
        "@intrinsic_apis//intrinsic/simulation/service/proto/v1:simulation_service_py_pb2_grpc",
    ],
)

py_library(
    name = "provided",
    srcs = ["provided.py"],
    deps = [
        ":blackboard_value",
        ":cel",
        ":utils",
        "//intrinsic_sdk/intrinsic/solutions/internal:actions",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_py_pb2",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_py_pb2",
    ],
)

py_library(
    name = "providers",
    srcs = ["providers.py"],
    deps = [
        ":behavior_tree",
        ":provided",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ] + [],
)

py_library(
    name = "structured_logging",
    srcs = ["structured_logging.py"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon_logging",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_py_pb2",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_py_pb2",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@ai_intrinsic_sdks_pip_deps//pandas",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_py_pb2",
        "@intrinsic_apis//intrinsic/logging/proto:bag_metadata_py_pb2",
    ],
)

py_library(
    name = "utils",
    srcs = ["utils.py"],
    deps = [":errors"],
)

py_library(
    name = "worlds",
    srcs = ["worlds.py"],
    deps = [
        ":utils",
        "//intrinsic_sdk/intrinsic/geometry/proto:geometry_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/math/python:data_types",
        "//intrinsic_sdk/intrinsic/math/python:proto_conversion",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_py_pb2_grpc",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/motion_planning/proto:motion_target_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:collision_settings_py_pb2",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_py_pb2",
    ],
)

# This wheel currently can only be built from the SDK. To make it build from
# another repo, we'd need to fix the paths ('packages' of the py_package and
# 'strip_path_prefixes' of the py_wheel)
#
# IMPORTANT: If you change the wheel's deps here, you also have to change the following files
# in insrc. For the internal solutions wheel:
#   - https://github.com/intrinsic-ai/intrinsic-core/blob/main/developer_resources/dev_tools/dev_container/features/solutions-lib-internal/requirements.in
#   - https://github.com/intrinsic-ai/intrinsic-core/blob/main/developer_resources/dev_tools/dev_container/features/solutions-lib-internal/requirements.txt
# For the external solutions wheel:
#   - https://github.com/intrinsic-ai/intrinsic-core/blob/main/developer_resources/dev_tools/dev_container/.devcontainer/local-features/solutions-lib/requirements.in
#   - https://github.com/intrinsic-ai/intrinsic-core/blob/main/developer_resources/dev_tools/dev_container/.devcontainer/local-features/solutions-lib/requirements.txt
