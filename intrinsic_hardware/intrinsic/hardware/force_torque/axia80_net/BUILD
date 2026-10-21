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
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_binary.bzl", "hardware_module_binary")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_image.bzl", "hardware_module_image")
load("//intrinsic_control/intrinsic/icon/hal/bzl:resources.bzl", "hardware_module_manifest", "wrap_in_any_textproto")
load("//intrinsic_sdk/intrinsic/assets/hardware_devices/build_defs:hardware_device.bzl", "intrinsic_hardware_device")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

package(default_visibility = [
    "//visibility:private",
])

intrinsic_service(
    name = "axia80_net_hwm",
    # Typically, you'll need to override this in the instance to set the ips.
    default_config = ":axia80_net_default_config",
    images = [
        ":axia80_net_hwm_image.tar",
        # TODO(b/263305147): remove once resources no longer fallback to using
        # the real image in simulation.
        "//intrinsic/resources:noop_image.tar",
    ],
    manifest = ":axia80_net_manifest",
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        ":axia80_net_proto",
        "//intrinsic/resources/proto:resource_health_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/icon/hal/proto:hardware_module_config_proto",
    ],
)

intrinsic_hardware_device(
    name = "axia80_net_hardware_device",
    assets = [
        "//intrinsic/resources/catalog/resourcedata/sensor:ati_axia80_m20_resource_type",
        ":axia80_net_hwm",
    ],
    manifest = "axia80_net_hardware_device_manifest.textproto",
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
)

################################################################################
# Private values that you typically should not need to access.
################################################################################

proto_library(
    name = "axia80_net_proto",
    srcs = ["axia80_net.proto"],
)

cc_proto_library(
    name = "axia80_net_cc_proto",
    deps = [":axia80_net_proto"],
)

cc_library(
    name = "axia80_net_lib",
    srcs = ["axia80_net_lib.cc"],
    hdrs = ["axia80_net_lib.h"],
    visibility = [
        "//visibility:private",
    ],
    deps = [
        ":axia80_net_cc_proto",
        "//intrinsic_control/intrinsic/icon/hal:default_hardware_interfaces",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_traits",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_sensor_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:force_torque_fbs_utils",
        "//intrinsic_control/intrinsic/icon/utils:log",
        "//intrinsic_control/intrinsic/icon/utils:malloc_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_guard",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:sensor_utils",
        "//intrinsic_hardware/intrinsic/hardware/gripper/wsg32:async_buffer",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

hardware_module_binary(
    name = "axia80_net_hwm_main",
    hardware_module_lib = ":axia80_net_lib",
)

hardware_module_image(
    name = "axia80_net_hwm_image",
    hardware_module_binary = "axia80_net_hwm_main",
)

hardware_module_manifest(
    name = "axia80_net_manifest",
    image = ":axia80_net_hwm_image.tar",
    # TODO(b/263305147): remove once resources no longer fallback to using the
    # real image in simulation.
    image_sim = "//intrinsic/resources:noop_image.tar",
    manifest = "axia80_net_hwm_manifest.textproto",
)

wrap_in_any_textproto(
    name = "axia80_net_default_config",
    src = ":axia80_net_hwm_example_config.pbtxt",
)

go_proto_library(
    name = "axia80_net_go_proto",
    importpath = "intrinsic/hardware/force_torque/axia80_net/axia80_net_go_proto",
    proto = ":axia80_net_proto",
)
