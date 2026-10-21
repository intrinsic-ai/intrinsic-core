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

package(
    default_testonly = True,
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "fake_channel_factory",
    srcs = ["fake_channel_factory.cc"],
    hdrs = ["fake_channel_factory.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/cc_client/testing:channel_fake",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:rtcl_controller_channel_fake",
        "//intrinsic_control/intrinsic/icon/equipment:channel_factory",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/grpc:connection_params",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "equipment_test_utils",
    srcs = ["equipment_test_utils.cc"],
    hdrs = ["equipment_test_utils.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/equipment:equipment_utils",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/icon/equipment:force_control_settings_cc_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_cc_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_handle_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:equipment_cc_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skills_cc_proto",
    ],
)
