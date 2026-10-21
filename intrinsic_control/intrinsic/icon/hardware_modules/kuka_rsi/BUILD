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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_binary", "cc_library", "cc_test")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic/util/proto/build_defs/testing:text_proto_test.bzl", "text_proto_test_suite")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_image.bzl", "hardware_module_image")
load("//intrinsic_control/intrinsic/icon/hal/bzl:resources.bzl", "hardware_module_manifest")
load("//intrinsic_sdk/intrinsic/assets/hardware_devices/build_defs:hardware_device.bzl", "intrinsic_hardware_device")
load("//intrinsic_sdk/intrinsic/assets/scene_objects/build_defs:scene_object.bzl", "intrinsic_scene_object")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

package(default_visibility = ["//visibility:public"])

# files also needed for resource_users
exports_files(
    srcs = [
        "kr6_r900_2_manifest.textproto",
        "kuka_rsi_default_config.textproto",
        "kuka_rsi_fake_config.textproto",
        "loopback_hwm_config.textproto",
    ],
    visibility = [
        "//intrinsic_control/intrinsic/icon:resource_users",
    ],
)

proto_library(
    name = "kuka_rsi_proto",
    srcs = ["kuka_rsi.proto"],
    deps = ["@com_google_protobuf//:duration_proto"],
)

cc_proto_library(
    name = "kuka_rsi_cc_proto",
    deps = [":kuka_rsi_proto"],
)

proto_library(
    name = "kuka_rsi_inspection_proto",
    srcs = ["kuka_rsi_inspection.proto"],
)

cc_proto_library(
    name = "kuka_rsi_inspection_cc_proto",
    deps = [":kuka_rsi_inspection_proto"],
)

cc_library(
    name = "kuka_rsi_util",
    srcs = [
        "kuka_rsi_util.cc",
    ],
    hdrs = [
        "kuka_rsi_util.h",
    ],
    deps = [
        ":kuka_rsi_cc_proto",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:bitmask_enums",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_control/intrinsic/icon/utils/xml:realtime_xml_generator",
        "//intrinsic_control/intrinsic/icon/utils/xml:realtime_xml_parser",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:inertia_utils",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:units",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "kuka_rsi",
    srcs = [
        "kuka_rsi_client.cc",
        "kuka_rsi_communicator.cc",
    ],
    hdrs = [
        "kuka_rsi_client.h",
        "kuka_rsi_communicator.h",
    ],
    deps = [
        ":kuka_rsi_inspection_cc_proto",
        ":kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_inspection_cc_proto",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_control:kuka_system_control_factory",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_control:kuka_system_control_interface",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_status:kuka_system_status_factory",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi/kuka_system_status:kuka_system_status_interface",
        "//intrinsic_control/intrinsic/icon/interprocess:lockable_binary_futex",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:async_buffer",
        "//intrinsic_control/intrinsic/icon/utils:bitmask_enums",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_control/intrinsic/icon/utils:strerror",
        "//intrinsic_control/intrinsic/icon/utils:udp_server_client",
        "//intrinsic_control/intrinsic/icon/utils/xml:realtime_xml_parser_generator_util",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util:invalid_until_set",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_cc_proto",
    ] + [],
)

cc_library(
    name = "kuka_rsi_loopback_lib",
    srcs = ["kuka_rsi_loopback.cc"],
    hdrs = ["kuka_rsi_loopback.h"],
    deps = [
        ":kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/utils:fixed_string",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:udp_server_client",
        "//intrinsic_control/intrinsic/icon/utils/xml:realtime_xml_generator",
        "//intrinsic_control/intrinsic/icon/utils/xml:realtime_xml_parser",
        "//intrinsic_sdk/intrinsic/math:units",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "kuka_rsi_hardware_module",
    srcs = ["kuka_rsi_hardware_module.cc"],
    hdrs = ["kuka_rsi_hardware_module.h"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        ":kuka_rsi",
        ":kuka_rsi_cc_proto",
        ":kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:adio_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:payload_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:robot_payload_utils",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math/signals:butter_filter2",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

hardware_module_image(
    name = "kuka_rsi_hal_module_image",
    hardware_module_lib = ":kuka_rsi_hardware_module",
)

# Specific hardware module which carries the kr6 world fragment.
# Works in conjunction with the generic ICON machine instance.
hardware_module_manifest(
    name = "kr6_r900_2_manifest",
    image = ":kuka_rsi_hal_module_image.tar",
    image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    manifest = "kr6_r900_2_hardware_module_service_node_manifest.textproto",
    provides_service_inspection = True,
)

intrinsic_service(
    name = "kr6_r900_2_hardware_module_service_node",
    default_config = ":kuka_rsi_default_config.textproto",
    images = [
        ":kuka_rsi_hal_module_image.tar",
        "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    ],
    manifest = ":kr6_r900_2_manifest",
    visibility = ["//visibility:private"],
    deps = [
        ":kuka_rsi_inspection_proto",
        ":kuka_rsi_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_inspection_proto",
    ],
)

intrinsic_scene_object(
    name = "kr6_r900_2_hardware_module_scene_object_node",
    manifest = "kr6_r900_2_hardware_module_scene_object_node_manifest.textproto",
    scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr6_r900_2_scene_object",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "kr6_r900_2_hardware_module",
    assets = [
        ":kr6_r900_2_hardware_module_scene_object_node",
        ":kr6_r900_2_hardware_module_service_node",
    ],
    manifest = "kr6_r900_2_manifest.textproto",
)

hardware_module_manifest(
    name = "kr6_r900_2_fake_manifest",
    image = "//intrinsic_control/intrinsic/icon/hardware_modules/fake:fake_module_image.tar",
    image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    manifest = "kr6_r900_2_fake_hardware_module_service_node_manifest.textproto",
)

intrinsic_service(
    name = "kr6_r900_2_fake_hardware_module_service_node",
    default_config = ":kuka_rsi_fake_config.textproto",
    images = [
        "//intrinsic_control/intrinsic/icon/hardware_modules/fake:fake_module_image.tar",
        "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    ],
    manifest = ":kr6_r900_2_fake_manifest",
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic_control/intrinsic/icon/hardware_modules/fake:fake_module_config_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
    ],
)

intrinsic_scene_object(
    name = "kr6_r900_2_fake_hardware_module_scene_object_node",
    manifest = "kr6_r900_2_fake_hardware_module_scene_object_node_manifest.textproto",
    scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr6_r900_2_scene_object",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "kr6_r900_2_fake_hardware_module",
    assets = [
        ":kr6_r900_2_fake_hardware_module_scene_object_node",
        ":kr6_r900_2_fake_hardware_module_service_node",
    ],
    manifest = "kr6_r900_2_fake_manifest.textproto",
)

hardware_module_manifest(
    name = "loopback_hal_module_manifest",
    image = "//intrinsic_control/intrinsic/icon/hardware_modules/loopback:loopback_hardware_module_image.tar",
    image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/loopback:loopback_hardware_module_image.tar",
    manifest = "kr6_r900_2_loopback_hardware_module_service_node_manifest.textproto",
)

intrinsic_service(
    name = "kr6_r900_2_loopback_hardware_module_service_node",
    default_config = ":loopback_hwm_config.textproto",
    images = ["//intrinsic_control/intrinsic/icon/hardware_modules/loopback:loopback_hardware_module_image.tar"],
    manifest = ":loopback_hal_module_manifest",
    visibility = ["//visibility:private"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
        "@intrinsic_apis//intrinsic/icon/hardware_modules/loopback:loopback_config_proto",
    ],
)

intrinsic_scene_object(
    name = "kr6_r900_2_loopback_hardware_module_scene_object_node",
    manifest = "kr6_r900_2_loopback_hardware_module_scene_object_node_manifest.textproto",
    scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr6_r900_2_scene_object",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "kr6_r900_2_loopback_hardware_module",
    assets = [
        ":kr6_r900_2_loopback_hardware_module_scene_object_node",
        ":kr6_r900_2_loopback_hardware_module_service_node",
    ],
    manifest = "loopback_manifest.textproto",
)

# Specific hardware module which carries the kr10 world fragment.
# Works in conjunction with the generic ICON machine instance.
hardware_module_manifest(
    name = "kr10_r1100_2_manifest",
    image = ":kuka_rsi_hal_module_image.tar",
    image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    manifest = "kr10_r1100_2_hardware_module_service_node_manifest.textproto",
    provides_service_inspection = True,
)

intrinsic_service(
    name = "kr10_r1100_2_hardware_module_service_node",
    default_config = ":kuka_rsi_default_config.textproto",
    images = [
        ":kuka_rsi_hal_module_image.tar",
        "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    ],
    manifest = ":kr10_r1100_2_manifest",
    visibility = ["//visibility:private"],
    deps = [
        ":kuka_rsi_inspection_proto",
        ":kuka_rsi_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_inspection_proto",
    ],
)

intrinsic_scene_object(
    name = "kr10_r1100_2_hardware_module_scene_object_node",
    manifest = "kr10_r1100_2_hardware_module_scene_object_node_manifest.textproto",
    scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr10_r1100_2_scene_object",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "kr10_r1100_2_hardware_module",
    assets = [
        ":kr10_r1100_2_hardware_module_scene_object_node",
        ":kr10_r1100_2_hardware_module_service_node",
    ],
    manifest = "kr10_r1100_2_manifest.textproto",
)

# Specific hardware module which carries the kr16 world fragment.
# Works in conjunction with the generic ICON machine instance.
hardware_module_manifest(
    name = "kr16_r2010_2_manifest",
    image = ":kuka_rsi_hal_module_image.tar",
    image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    manifest = "kr16_r2010_2_hardware_module_service_node_manifest.textproto",
    provides_service_inspection = True,
)

intrinsic_service(
    name = "kr16_r2010_2_hardware_module_service_node",
    default_config = ":kuka_rsi_default_config.textproto",
    images = [
        ":kuka_rsi_hal_module_image.tar",
        "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    ],
    manifest = ":kr16_r2010_2_manifest",
    visibility = ["//visibility:private"],
    deps = [
        ":kuka_rsi_inspection_proto",
        ":kuka_rsi_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_inspection_proto",
    ],
)

intrinsic_scene_object(
    name = "kr16_r2010_2_hardware_module_scene_object_node",
    manifest = "kr16_r2010_2_hardware_module_scene_object_node_manifest.textproto",
    scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr16_r2010_2_scene_object",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "kr16_r2010_2_hardware_module",
    assets = [
        ":kr16_r2010_2_hardware_module_scene_object_node",
        ":kr16_r2010_2_hardware_module_service_node",
    ],
    manifest = "kr16_r2010_2_manifest.textproto",
)

# Specific hardware module which carries the kr20 world fragment.
# Works in conjunction with the generic ICON machine instance.
hardware_module_manifest(
    name = "kr20_r1810_2_manifest",
    image = ":kuka_rsi_hal_module_image.tar",
    image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    manifest = "kr20_r1810_2_hardware_module_service_node_manifest.textproto",
    provides_service_inspection = True,
)

intrinsic_service(
    name = "kr20_r1810_2_hardware_module_service_node",
    default_config = ":kuka_rsi_default_config.textproto",
    images = [
        ":kuka_rsi_hal_module_image.tar",
        "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    ],
    manifest = ":kr20_r1810_2_manifest",
    visibility = ["//visibility:private"],
    deps = [
        ":kuka_rsi_inspection_proto",
        ":kuka_rsi_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_inspection_proto",
    ],
)

intrinsic_scene_object(
    name = "kr20_r1810_2_hardware_module_scene_object_node",
    manifest = "kr20_r1810_2_hardware_module_scene_object_node_manifest.textproto",
    scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr20_r1810_2_scene_object",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "kr20_r1810_2_hardware_module",
    assets = [
        ":kr20_r1810_2_hardware_module_scene_object_node",
        ":kr20_r1810_2_hardware_module_service_node",
    ],
    manifest = "kr20_r1810_2_manifest.textproto",
)

# Specific hardware module which carries the kr50 world fragment.
# Works in conjunction with the generic ICON machine instance.
hardware_module_manifest(
    name = "kr50_r2500_manifest",
    image = ":kuka_rsi_hal_module_image.tar",
    image_sim = "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    manifest = "kr50_r2500_hardware_module_service_node_manifest.textproto",
    provides_service_inspection = True,
)

intrinsic_service(
    name = "kr50_r2500_hardware_module_service_node",
    default_config = ":kuka_rsi_default_config.textproto",
    images = [
        ":kuka_rsi_hal_module_image.tar",
        "//intrinsic_control/intrinsic/icon/hardware_modules/gazebo_hwm_stub:gazebo_hwm_stub_image.tar",
    ],
    manifest = ":kr50_r2500_manifest",
    visibility = ["//visibility:private"],
    deps = [
        ":kuka_rsi_inspection_proto",
        ":kuka_rsi_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_inspection_proto",
    ],
)

intrinsic_scene_object(
    name = "kr50_r2500_hardware_module_scene_object_node",
    manifest = "kr50_r2500_hardware_module_scene_object_node_manifest.textproto",
    scene_object = "//intrinsic_control/intrinsic/models/robot_definitions/kuka:kr50_r2500_scene_object",
    visibility = ["//visibility:private"],
)

intrinsic_hardware_device(
    name = "kr50_r2500_hardware_module",
    assets = [
        ":kr50_r2500_hardware_module_scene_object_node",
        ":kr50_r2500_hardware_module_service_node",
    ],
    manifest = "kr50_r2500_manifest.textproto",
)

# Test code
cc_library(
    name = "mock_kuka_rsi_client",
    testonly = True,
    hdrs = ["mock_kuka_rsi_client.h"],
    deps = [
        ":kuka_rsi",
        ":kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest",
    ],
)

cc_test(
    name = "kuka_rsi_util_test",
    srcs = ["kuka_rsi_util_test.cc"],
    deps = [
        ":kuka_rsi_cc_proto",
        ":kuka_rsi_util",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "kuka_rsi_hardware_module_test",
    srcs = ["kuka_rsi_hardware_module_test.cc"],
    data = ["kuka_rsi_module_test_config.pbtxt"],
    deps = [
        ":kuka_rsi_cc_proto",
        ":kuka_rsi_hardware_module",
        ":kuka_rsi_util",
        ":mock_kuka_rsi_client",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_proxy",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_runtime",
        "//intrinsic_control/intrinsic/icon/hal:icon_state_register",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:adio_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:icon_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:io_controller_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:memory_segment",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager:segment_info_fbs_cc",
        "//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager/testing:unique_segment_name",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:current_cycle",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@or_tools//ortools/base:file",
    ],
)

text_proto_test_suite(
    name = "kuka_rsi_proto_tests",
    srcs = [
        "kuka_rsi_module_test_config.pbtxt",
    ],
    message = "intrinsic_proto.icon.HardwareModuleConfig",
    deps = [
        ":kuka_rsi_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
    ],
)

cc_binary(
    name = "kuka_rsi_loopback",
    srcs = [
        "kuka_rsi_loopback_main.cc",
    ],
    deps = [
        ":kuka_rsi_loopback_lib",
        ":kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

filegroup(
    name = "kuka_rsi_robots_core",
    srcs = [
        "README.md",
        ":kr10_r1100_2_hardware_module",
        ":kr16_r2010_2_hardware_module",
        ":kr20_r1810_2_hardware_module",
        ":kr50_r2500_hardware_module",
        ":kr6_r900_2_fake_hardware_module",
        ":kr6_r900_2_hardware_module",
        ":kr6_r900_2_loopback_hardware_module",
    ],
)
