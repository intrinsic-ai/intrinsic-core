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

load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "icon",
    srcs = [
        "_session.py",
        "actions.py",
        "errors.py",
        "icon_api.py",
        "reactions.py",
        "state_variable_path.py",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/proto:matrix_conversions",  # buildcleaner: keep to allow import
        "//intrinsic_sdk/intrinsic/solutions:deployments",
        "//intrinsic_sdk/intrinsic/util/grpc:connection",
        "//intrinsic_sdk/intrinsic/util/grpc:interceptor",
        "//intrinsic_sdk/intrinsic/world/robot_payload/python:robot_payload",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@com_google_googleapis//google/rpc:code_py_proto",
        "@com_google_googleapis//google/rpc:status_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/icon/proto:logging_mode_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto/v1:condition_types_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_py_pb2",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_py_pb2",
        "@intrinsic_apis//intrinsic/logging/proto:context_py_pb2",
    ],
)

py_library(
    name = "cancelation",
    srcs = ["cancelation.py"],
    deps = [
        ":icon",
        "//intrinsic_control/intrinsic/icon/actions:stop_utils",
        "//intrinsic_sdk/intrinsic/skills/python:skill_canceller",
    ],
)

py_library(
    name = "icon_logging",
    srcs = ["icon_logging.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_py_pb2",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_py_pb2",
    ],
)

py_library(
    name = "create_action_utils",
    srcs = ["create_action_utils.py"],
    deps = [
        "//intrinsic_control/intrinsic/icon/actions:adio_utils",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_admittance_utils",
        "//intrinsic_control/intrinsic/icon/actions:force_primitive_utils",
        "//intrinsic_control/intrinsic/icon/actions:point_to_point_move_utils",
        "//intrinsic_control/intrinsic/icon/actions:stop_utils",
        "//intrinsic_control/intrinsic/icon/actions:tare_force_torque_sensor_utils",
        "//intrinsic_control/intrinsic/icon/actions:trajectory_tracking_action_utils",
        "//intrinsic_control/intrinsic/icon/actions:wait_for_settling_utils",
    ] + [],
)
