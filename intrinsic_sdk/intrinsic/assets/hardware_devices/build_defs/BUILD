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

load("//bazel:go_macros.bzl", "go_binary", "go_library")

go_library(
    name = "hardwaredevicegen",
    srcs = ["hardwaredevicegen.go"],
    importpath = "intrinsic/assets/hardware_devices/build_defs/hardwaredevicegen",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic_sdk/intrinsic/assets:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets/build_defs:asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/hardware_devices:hardwaredevicebundle",
        "//intrinsic_sdk/intrinsic/assets/hardware_devices:hardwaredevicefix",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@intrinsic_apis//intrinsic/assets/hardware_devices/proto/v1:hardware_device_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:reference_go_proto",
    ],
)

go_binary(
    name = "hardwaredevicegen_main",
    srcs = ["hardwaredevicegen_main.go"],
    importpath = "intrinsic/assets/hardware_devices/build_defs/hardwaredevicegen_main",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic_sdk/intrinsic/assets:__subpackages__",
    ],
    deps = [
        ":hardwaredevicegen",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util:flag",
        "@com_github_golang_glog//:go_default_library",
    ],
)
