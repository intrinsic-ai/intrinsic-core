# Copyright 2023 Intrinsic Innovation LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_binary.bzl", "hardware_module_binary")
load("//intrinsic_control/intrinsic/icon/hal/bzl:hardware_module_image.bzl", "hardware_module_image")

package(
    default_applicable_licenses = ["//third_party/abb_hardware_module:license"],
    default_visibility = ["//visibility:public"],
)

proto_library(
    name = "abb_config_proto",
    srcs = ["abb_config.proto"],
    deps = ["@intrinsic_apis//intrinsic/icon/proto:joint_position_pid_velocity_controller_config_proto"],
)

cc_proto_library(
    name = "abb_config_cc_proto",
    deps = [":abb_config_proto"],
)

cc_library(
    name = "abb_hardware_module",
    srcs = ["abb_hardware_module.cc"],
    hdrs = ["abb_hardware_module.h"],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    deps = [
        ":abb_config_cc_proto",
        ":abb_default_configs",
        ":egm_serialization",
        ":egm_server",
        ":rws_client",
        ":rws_egm_utils",
        "//intrinsic_control/intrinsic/icon/control:realtime_clock_interface",
        "//intrinsic_control/intrinsic/icon/control/algorithms:joint_position_pid_velocity_controller",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_cc",
        "//intrinsic_control/intrinsic/icon/control/safety:safety_messages_fbs_utils",
        "//intrinsic_control/intrinsic/icon/control/safety/extern:safety_status_fbs_cc",
        "//intrinsic_control/intrinsic/icon/flatbuffers:transform_utils",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_handle",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_registry",
        "//intrinsic_control/intrinsic/icon/hal:hardware_interface_traits",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_interface",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_command_fbs_utils",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_cc",
        "//intrinsic_control/intrinsic/icon/hal/interfaces:joint_state_fbs_utils",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:core_time",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_stack_trace",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//third_party/abb_hardware_module/utils:rate_monitor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
    ],
    alwayslink = 1,
)

hardware_module_binary(
    name = "abb_hardware_module_main",
    hardware_module_lib = ":abb_hardware_module",
)

hardware_module_image(
    name = "abb_hardware_module_image",
    hardware_module_binary = "abb_hardware_module_main",
    visibility = ["//intrinsic_control/intrinsic/icon/hardware_modules/abb:__subpackages__"],
)

cc_library(
    name = "udp_server",
    srcs = ["udp_server.cc"],
    hdrs = [
        "udp_server.h",
        "udp_server_interface.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
    ],
    alwayslink = 1,
)

cc_library(
    name = "egm_server",
    srcs = ["egm_server.cc"],
    hdrs = ["egm_server.h"],
    deps = [
        "udp_server",
        ":egm_serialization",
        "//intrinsic_control/intrinsic/icon/utils:async_buffer",
        "//intrinsic_control/intrinsic/icon/utils:fixed_str_cat",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "//third_party/abb_egm:egm_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
    alwayslink = 1,
)

cc_library(
    name = "egm_serialization",
    srcs = ["egm_serialization.cc"],
    hdrs = [
        "egm_data.h",
        "egm_serialization.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//third_party/abb_egm:egm_cc_proto",
    ],
)

cc_library(
    name = "abb_elog",
    srcs = ["abb_elog.cc"],
    hdrs = ["abb_elog.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_github_nlohmann_json//:json",
    ],
)

cc_library(
    name = "rws_client",
    srcs = [
        "abb_elog.cc",
        "rws_client.cc",
    ],
    hdrs = [
        "abb_elog.h",
        "rws_client.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    deps = [
        ":abb_elog",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//intrinsic_sdk/intrinsic/util/thread:thread_utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_curl_curl//:curl",
        "@com_github_nlohmann_json//:json",
    ],
)

cc_library(
    name = "rws_egm_utils",
    srcs = ["rws_egm_utils.cc"],
    hdrs = ["rws_egm_utils.h"],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    deps = [
        ":abb_config_cc_proto",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_nlohmann_json//:json",
    ],
)

cc_library(
    name = "abb_default_configs",
    hdrs = ["abb_default_configs.h"],
)
