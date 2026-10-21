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

load("@rules_pkg//:pkg.bzl", "pkg_tar")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_binary")

package(default_visibility = [
    "//intrinsic_control/intrinsic/icon:rtcl_users",
    "//visibility:public",
])

# `MallocGuard` is a no-op under allocator-replacing sanitizers, so its tests
# are not allowed to be run with the following sanitizers.
SANITIZER_EXCLUSION_TAGS = [
    "noasan",
    "nomsan",
    "notsan",
]

cc_library(
    name = "bitmask_enums",
    hdrs = ["bitmask_enums.h"],
)

cc_library(
    name = "bitset",
    hdrs = ["bitset.h"],
)

cc_library(
    name = "constants",
    hdrs = ["constants.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "fixed_string",
    hdrs = ["fixed_string.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "fixed_str_cat",
    hdrs = ["fixed_str_cat.h"],
    deps = [
        ":fixed_string",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "strerror",
    hdrs = ["strerror.h"],
    deps = [
        ":fixed_str_cat",
        ":fixed_string",
    ],
)

cc_library(
    name = "arm_utils",
    srcs = ["arm_utils.cc"],
    hdrs = ["arm_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_cc_proto",
    ],
)

cc_library(
    name = "sensor_utils",
    srcs = ["sensor_utils.cc"],
    hdrs = ["sensor_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "realtime_status",
    srcs = ["realtime_status.cc"],
    hdrs = ["realtime_status.h"],
    deps = [
        ":fixed_str_cat",
        ":fixed_string",
        ":realtime_guard",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "realtime_status_or",
    hdrs = ["realtime_status_or.h"],
    deps = [
        ":realtime_status",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "realtime_status_matchers",
    testonly = True,
    hdrs = ["realtime_status_matchers.h"],
    deps = [
        ":malloc_guard",
        ":realtime_status",
        ":realtime_status_macro",
        ":realtime_status_or",
        "@abseil-cpp//absl/status",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest",
    ],
)

cc_library(
    name = "realtime_status_macro",
    hdrs = ["realtime_status_macro.h"],
    deps = [
        ":fixed_str_cat",
        ":realtime_status",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:absl_check",
    ],
)

cc_library(
    name = "exit_code",
    hdrs = ["exit_code.h"],
)

go_binary(
    name = "init_icon",
    srcs = [
        "init_icon.go",
        "init_runner.go",
    ],
    importpath = "intrinsic/icon/utils/init_icon",
    deps = [
        "//intrinsic/kubernetes:intrinsic",
        "@com_github_golang_glog//:go_default_library",
    ],
)

go_binary(
    name = "init_hwm",
    srcs = [
        "init_hwm.go",
        "init_runner.go",
    ],
    importpath = "intrinsic/icon/utils/init_hwm",
    deps = [
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "@com_github_golang_glog//:go_default_library",
    ],
)

pkg_tar(
    name = "init_hwm_tar",
    srcs = [":init_hwm"],
    include_runfiles = True,
)

cc_library(
    name = "core_time",
    srcs = [
        "clock.cc",
    ],
    hdrs = [
        "clock.h",
    ],
    deps = [
        ":log_internal",
        ":time",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "time",
    srcs = [
        "duration.cc",
    ],
    hdrs = [
        "clock_base.h",
        "duration.h",
    ],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "realtime_log_sink",
    srcs = ["realtime_log_sink.cc"],
    hdrs = ["realtime_log_sink.h"],
    deps = [
        ":log_sink",
        ":realtime_guard",
        "//intrinsic_control/intrinsic/icon/interprocess:binary_futex",
        "//intrinsic_sdk/intrinsic/platform/common/buffers:realtime_write_queue",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "log_internal",
    srcs = ["log_internal.cc"],
    hdrs = ["log_internal.h"],
    visibility = ["//visibility:private"],
    deps = [
        ":fixed_string",
        ":log_sink",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "log",
    srcs = ["log.cc"],
    hdrs = ["log.h"],
    deps = [
        ":log_internal",
        ":log_sink",
        ":realtime_log_sink",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/base:log_severity",
    ],
)

cc_library(
    name = "log_sink",
    srcs = ["log_sink.cc"],
    hdrs = ["log_sink.h"],
    deps = [
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "realtime_stack_trace",
    srcs = [
        "realtime_stack_trace.cc",
    ],
    hdrs = [
        "log.h",  # Avoid circular dep realtime_stack_trace <-> realtime_log_sink.
        "realtime_stack_trace.h",
    ],
    linkopts = [
        "-ldl",  # for dlfcn.h, dladdr in realtime_stack_trace.cc
    ],
    deps = [
        ":fixed_str_cat",
        ":fixed_string",
        ":log_internal",
        ":log_sink",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "realtime_guard",
    srcs = [
        "log.h",  # Avoid circular dep realtime_guard <-> realtime_log_sink.
        "realtime_guard.cc",
    ],
    hdrs = [
        "realtime_guard.h",
    ],
    linkopts = [
        "-ldl",  # for dlfcn.h, dladdr in realtime_guard.cc
    ],
    deps = [
        ":log_internal",
        ":log_sink",
        ":realtime_stack_trace",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "malloc_guard",
    srcs = [
        "malloc_guard.cc",
        "malloc_guard_plthook_elf.cc",
        "malloc_guard_plthook_elf.h",
    ],
    hdrs = [
        "malloc_guard.h",
    ],
    linkopts = ["-ldl"],
    visibility = [
        "//intrinsic_control/intrinsic/icon:hardware_module_authors",
        "//intrinsic_control/intrinsic/icon:rtcl_users",
    ],
    deps = [
        ":fixed_string",
        ":log",
        ":realtime_stack_trace",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@plthook",
    ],
)

cc_library(
    name = "world_utils",
    srcs = ["world_utils.cc"],
    hdrs = ["world_utils.h"],
    deps = [
        "//intrinsic/world",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic_kinematics/intrinsic/kinematics",
        "//intrinsic_kinematics/intrinsic/kinematics:skeleton",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util:string_type",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world/objects:frame",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_client",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/objects:world_object",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_service_cc_proto",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:physics_component_cc_proto",
    ],
)

cc_library(
    name = "shutdown_signals",
    srcs = ["shutdown_signals.cc"],
    hdrs = ["shutdown_signals.h"],
    deps = [
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "current_cycle",
    hdrs = ["current_cycle.h"],
    visibility = [
        "//intrinsic/simulation/gazebo/plugins:__subpackages__",
        "//intrinsic_control/intrinsic/icon/control:__pkg__",
        "//intrinsic_control/intrinsic/icon/hal:__pkg__",
        "//intrinsic_control/intrinsic/icon/hardware_modules:__subpackages__",
        "//intrinsic_control/intrinsic/icon/server:__pkg__",
    ],
)

cc_library(
    name = "futex",
    hdrs = ["futex.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "async_buffer",
    hdrs = ["async_buffer.h"],
    deps = ["@abseil-cpp//absl/log:check"],
)

cc_library(
    name = "async_request",
    hdrs = ["async_request.h"],
    deps = [
        ":realtime_status",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/platform/common/buffers:rt_promise",
    ],
)

cc_library(
    name = "realtime_metrics",
    srcs = ["realtime_metrics.cc"],
    hdrs = ["realtime_metrics.h"],
    deps = [
        ":log",
        ":realtime_status",
        ":realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/time",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_googletest//:gtest_prod",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/performance/analysis/proto:performance_metrics_cc_proto",
    ],
)

cc_library(
    name = "tcp_server_client",
    srcs = [
        "tcp_server_client.cc",
    ],
    hdrs = ["tcp_server_client.h"],
    deps = [
        ":realtime_status",
        ":realtime_status_or",
        ":strerror",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@abseil-cpp//absl/utility",
    ],
)

cc_library(
    name = "udp_server_client",
    srcs = ["udp_server_client.cc"],
    hdrs = [
        "udp_server_client.h",
    ],
    deps = [
        ":realtime_status",
        ":realtime_status_macro",
        ":realtime_status_or",
        ":strerror",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "aggregated_health_to_state",
    srcs = ["aggregated_health_to_state.cc"],
    hdrs = ["aggregated_health_to_state.h"],
    deps = [
        "//intrinsic/simulation/gazebo/plugins:aggregated_resource_health_cc_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)

cc_library(
    name = "inspection_publisher",
    hdrs = ["inspection_publisher.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/hal:module_config",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_inspection_cc_proto",
    ],
)
