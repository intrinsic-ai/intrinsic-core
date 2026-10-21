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

load("//bazel:cc_macros.bzl", "cc_library", "cc_test")
load("//bazel:python.bzl", "py_library", "py_test")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "gripper_python_utils",
    srcs = ["gripper_python_utils.py"],
    deps = [
        ":gripper_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_client",
        "//intrinsic_sdk/intrinsic/world/python:object_world_resources",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

py_library(
    name = "gripper_client",
    srcs = ["gripper_client.py"],
    deps = [
        "//intrinsic_control/intrinsic/icon/python:icon",
        "//intrinsic_sdk/intrinsic/util/grpc:connection",
        "//intrinsic_sdk/intrinsic/util/grpc:interceptor",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_py_pb2",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_py_pb2",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

py_test(
    name = "gripper_python_utils_test",
    srcs = ["gripper_python_utils_test.py"],
    deps = [
        ":gripper_client",
        ":gripper_python_utils",
        "//intrinsic_sdk/intrinsic/util/grpc:connection",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/testing:absltest",
        "@com_google_absl_py//absl/testing:parameterized",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_py_pb2",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

py_test(
    name = "gripper_client_test",
    srcs = ["gripper_client_test.py"],
    deps = [
        ":gripper_client",
        "//intrinsic_control/intrinsic/icon/python:icon",
        "//intrinsic_sdk/intrinsic/util/grpc:connection",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/testing:absltest",
        "@com_google_absl_py//absl/testing:parameterized",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_py_pb2",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_py_pb2",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_py_pb2_grpc",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_py_pb2",
    ],
)

cc_library(
    name = "gripper_utils",
    srcs = ["gripper_utils.cc"],
    hdrs = ["gripper_utils.h"],
    visibility = ["//intrinsic/simulation/gazebo/plugins/grippers:__subpackages__"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
    ],
)

cc_test(
    name = "gripper_utils_test",
    srcs = ["gripper_utils_test.cc"],
    deps = [
        ":gripper_utils",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
    ],
)

cc_library(
    name = "gripper_impl",
    hdrs = ["gripper_impl.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":gripper_utils",
        "//intrinsic/resources/health:health_state_machine",
        "//intrinsic/resources/health:operational_status",
        "//intrinsic_hardware/intrinsic/hardware/gpio:gpio_client",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
    ],
)

cc_test(
    name = "gripper_impl_test",
    srcs = ["gripper_impl_test.cc"],
    deps = [
        ":gripper_impl",
        ":gripper_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
    ],
)

cc_library(
    name = "specialized_gripper_impl",
    srcs = ["specialized_gripper_impl.cc"],
    hdrs = ["specialized_gripper_impl.h"],
    deps = [
        ":gripper_impl",
        ":gripper_utils",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
    ],
)

cc_library(
    name = "pinch_gripper_service",
    srcs = [
        "pinch_gripper_service.cc",
    ],
    hdrs = [
        "pinch_gripper_service.h",
    ],
    deps = [
        ":gripper_impl",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
    ],
)

cc_library(
    name = "suction_gripper_service",
    srcs = [
        "suction_gripper_service.cc",
    ],
    hdrs = [
        "suction_gripper_service.h",
    ],
    deps = [
        ":gripper_impl",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
    ],
)

cc_library(
    name = "generic_gripper_service",
    srcs = ["generic_gripper_service.cc"],
    hdrs = ["generic_gripper_service.h"],
    deps = [
        ":gripper_impl",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/log:die_if_null",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/service/proto:generic_gripper_cc_proto",
    ],
)

cc_library(
    name = "gripper_health_service",
    srcs = ["gripper_health_service.cc"],
    hdrs = ["gripper_health_service.h"],
    deps = [
        ":gripper_impl",
        "//intrinsic/resources/proto:resource_health_cc_grpc_proto",
        "//intrinsic/resources/proto:resource_health_cc_proto",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:gripper_config_cc_proto",
    ],
)

cc_library(
    name = "gripper_services",
    srcs = ["gripper_services.cc"],
    hdrs = ["gripper_services.h"],
    deps = [
        ":generic_gripper_service",
        ":gripper_health_service",
        ":pinch_gripper_service",
        ":specialized_gripper_impl",
        ":suction_gripper_service",
        "//intrinsic_hardware/intrinsic/hardware/gpio:gpio_service_factory",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper/eoat:eoat_service_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto",
    ],
)
