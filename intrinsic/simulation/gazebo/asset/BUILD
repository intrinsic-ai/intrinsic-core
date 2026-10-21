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
load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("//bazel:cc_oci_image.bzl", "cc_oci_image")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

exports_files(["gazebo_simulator_default_config.textproto"])

proto_library(
    name = "gazebo_simulator_config_proto",
    srcs = ["gazebo_simulator_config.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
    ],
)

cc_proto_library(
    name = "gazebo_simulator_config_cc_proto",
    visibility = ["//intrinsic/simulation:__subpackages__"],
    deps = [":gazebo_simulator_config_proto"],
)

cc_binary(
    name = "asset_simulation_server_main",
    srcs = ["server_main.cc"],
    deps = [
        # LINT.IfChange
        "//intrinsic_perception/intrinsic/perception/cameras:genicam_image_source",  # go/registerer (fake_genicam)
        "//intrinsic/simulation/gazebo:gz_ogre2_gl3plus_render_engine_static",
        "//intrinsic/simulation/gazebo:simulation_server_main_impl",
        "@gz-physics//bullet-featherstone:libgz-physics-bullet-featherstone-plugin-static",
        "@gz-sim//:gz-sim-sensors-system-static",
        # LINT.ThenChange(//intrinsic/simulation/gazebo/BUILD)
    ],
)

cc_oci_image(
    name = "asset-gzserver-image",
    base = "//intrinsic/simulation/docker:bullseye_mesa_with_zenoh",
    binary = ":asset_simulation_server_main",
    data_path = "/",
)

intrinsic_service(
    name = "gazebo_simulator_type",
    default_config = "gazebo_simulator_default_config.textproto",
    images = [
        ":asset-gzserver-image.tar",
        "//intrinsic/resources:noop_image.tar",
    ],
    manifest = "gazebo_simulator_manifest.textproto",
    visibility = ["//visibility:public"],
    deps = [
        ":gazebo_simulator_config_proto",
        "//intrinsic/simulation/simulator/proto/v1:simulator_control_service_proto",
        "@intrinsic_apis//intrinsic/simulation/gazebo/proto/v1:gazebo_service_proto",
    ],
)
