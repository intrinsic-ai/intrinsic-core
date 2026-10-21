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

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//intrinsic_control/intrinsic/icon/skills:__subpackages__",
])

cc_library(
    name = "analog_digital_io",
    srcs = ["analog_digital_io.cc"],
    hdrs = ["analog_digital_io.h"],
    deps = [
        ":adio_util",
        "//intrinsic_control/intrinsic/icon/actions:adio_info",
        "//intrinsic_control/intrinsic/icon/cc_client:client",
        "//intrinsic_control/intrinsic/icon/cc_client:condition",
        "//intrinsic_control/intrinsic/icon/cc_client:session",
        "//intrinsic_control/intrinsic/icon/cc_client:state_variable_path",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/control/parts:io_block",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_rpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gpio:gpio_service_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_cc_proto",
        "@intrinsic_apis//intrinsic/icon/actions:adio_cc_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
    ],
)

cc_library(
    name = "adio_util",
    srcs = ["adio_util.cc"],
    hdrs = ["adio_util.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/cc_client:robot_config",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/hardware/gpio:gpio_service_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/actions:adio_cc_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:io_block_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
    ],
)

cc_library(
    name = "analog_digital_io_factory",
    srcs = ["analog_digital_io_factory.cc"],
    hdrs = ["analog_digital_io_factory.h"],
    deps = [
        ":analog_digital_io",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "//intrinsic_sdk/intrinsic/skills/cc:equipment_pack",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
    ],
)

cc_library(
    name = "position_part_util",
    srcs = ["position_part_util.cc"],
    hdrs = ["position_part_util.h"],
    visibility = [
        "//intrinsic_control/intrinsic/icon/skills:__subpackages__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/skills:__subpackages__",
        "//intrinsic_perception/intrinsic/perception/skills:__subpackages__",
        "//incode/motion_planning/skills:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "//intrinsic_sdk/intrinsic/world/util:object_reference_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:object_world_refs_cc_proto",
    ],
)
