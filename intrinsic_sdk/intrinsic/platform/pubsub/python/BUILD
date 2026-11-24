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

# Python wrapper for the intrinsic pubsub c++ library.

load("@pybind11_bazel//:build_defs.bzl", "PYBIND_DEPS")
load("//bazel:cc_macros.bzl", "cc_library", "pybind_extension")

package(default_visibility = ["//visibility:public"])

pybind_extension(
    name = "pubsub",
    srcs = ["pubsub.cc"],
    deps = [
        ":gil_aware_pubsub",
        "//intrinsic_sdk/intrinsic/middleware/buffers:buffer_factory",
        "//intrinsic_sdk/intrinsic/middleware/buffers:immutable_buffer",
        "//intrinsic_sdk/intrinsic/middleware/buffers:mutable_buffer",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:kvstore",
        "//intrinsic_sdk/intrinsic/platform/pubsub:liveliness_query",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/platform/pubsub:subscription",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:parse",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@pybind11_abseil//pybind11_abseil:absl_casters",
        "@pybind11_abseil//pybind11_abseil:no_throw_status",
        "@pybind11_abseil//pybind11_abseil:status_casters",
        "@pybind11_protobuf//pybind11_protobuf:native_proto_caster",
    ],
)

cc_library(
    name = "gil_aware_pubsub",
    srcs = ["gil_aware_pubsub.cc"],
    hdrs = ["gil_aware_pubsub.h"],
    deps = PYBIND_DEPS + [
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/strings",
    ],
)
