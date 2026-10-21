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
    "//intrinsic_control/intrinsic/icon:__subpackages__",
])

cc_library(
    name = "hal_laser_tracker_part",
    srcs = ["hal_laser_tracker_part.cc"],
    hdrs = ["hal_laser_tracker_part.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/parts:hal_realtime_part_base",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_factory_common",
        "//intrinsic_control/intrinsic/icon/control/parts/feature_interfaces:laser_tracker_position_state",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_manager",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:laser_tracker_state_cc_fbs",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/icon/control/parts/hal/laser_tracker_part:hal_laser_tracker_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/control/parts/hal/v1:hal_part_config_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:generic_part_config_cc_proto",
    ],
)

cc_library(
    name = "hal_laser_tracker_part_register",
    srcs = ["hal_laser_tracker_part_register.cc"],
    visibility = ["//intrinsic_control/intrinsic/icon:rtcl_users"],
    deps = [
        ":hal_laser_tracker_part",
        "//intrinsic_control/intrinsic/icon/control/parts:realtime_part_from_proto_factory_registry",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/icon/control/parts/hal/laser_tracker_part:hal_laser_tracker_part_config_cc_proto",
    ],
    alwayslink = True,
)
