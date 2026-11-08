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

load("//bazel:cc_macros.bzl", "pybind_extension")
load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

pybind_extension(
    name = "channel_fake",
    testonly = True,
    srcs = ["channel_fake.cc"],
    deps = [
        "//intrinsic_control/intrinsic/icon/cc_client/testing:channel_fake",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:rtcl_controller_channel_fake",
        "//intrinsic_control/intrinsic/icon/cc_client/testing:rtcl_controller_fake_session",
        "//intrinsic_control/intrinsic/icon/common:id_types",
        "//intrinsic_control/intrinsic/icon/common:part_properties",
        "//intrinsic_control/intrinsic/icon/server:extended_status_constants",
        "//intrinsic_control/intrinsic/icon/server/testing:in_process_server",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/grpc:channel_interface",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
        "@pybind11_abseil//pybind11_abseil:absl_casters",
        "@pybind11_abseil//pybind11_abseil:status_casters",
        "@pybind11_protobuf//pybind11_protobuf:native_proto_caster",
    ],
)

py_library(
    name = "icon_fake",
    testonly = True,
    srcs = ["icon_fake.py"],
    deps = [
        ":channel_fake",
        "//intrinsic_control/intrinsic/icon/python:icon",
        "@ai_intrinsic_sdks_pip_deps//portpicker",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_py_pb2",
        "@pybind11_abseil//pybind11_abseil:import_status_module",
    ],
)
