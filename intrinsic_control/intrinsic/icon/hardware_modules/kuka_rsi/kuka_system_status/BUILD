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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "kuka_system_status_interface",
    hdrs = [
        "kuka_system_status_interface.h",
    ],
    deps = [
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/world/robot_payload:robot_payload_base",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "kuka_eki_system_status",
    srcs = [
        "kuka_eki_system_status.cc",
    ],
    hdrs = [
        "kss_messages.h",
        "kuka_eki_system_status.h",
    ],
    deps = [
        ":kuka_system_status_interface",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_cc_proto",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/testing:realtime_annotations",
        "//intrinsic_control/intrinsic/icon/utils:async_buffer",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_control/intrinsic/icon/utils:tcp_server_client",
        "//intrinsic_control/intrinsic/icon/utils/xml:realtime_xml_generator",
        "//intrinsic_control/intrinsic/icon/utils/xml:realtime_xml_parser",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:units",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "//intrinsic_sdk/intrinsic/util:invalid_until_set",
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
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_gitlab_libeigen_eigen//:eigen",
    ],
)

cc_test(
    name = "kuka_eki_system_status_test",
    size = "small",
    srcs = ["kuka_eki_system_status_test.cc"],
    tags = ["notsan"],
    deps = [
        ":kuka_eki_system_status",
        "//intrinsic_control/intrinsic/icon/hal:hardware_module_registry",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/testing:mock_tcp_client",
        "//intrinsic_control/intrinsic/icon/utils:bitmask_enums",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_or",
        "//intrinsic_control/intrinsic/icon/utils/xml:realtime_xml_generator",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "kuka_system_status_factory",
    srcs = [
        "kuka_system_status_factory.cc",
    ],
    hdrs = [
        "kuka_system_status_factory.h",
    ],
    deps = [
        ":kuka_eki_system_status",
        ":kuka_system_status_interface",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_cc_proto",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_util",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "kuka_system_status_factory_test",
    srcs = ["kuka_system_status_factory_test.cc"],
    flaky = True,  # b/393504488
    deps = [
        ":kuka_system_status_factory",
        ":kuka_system_status_interface",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_cc_proto",
        "//intrinsic_control/intrinsic/icon/hardware_modules/kuka_rsi:kuka_rsi_util",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_matchers",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)
