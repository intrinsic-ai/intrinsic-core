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

package(
    default_visibility = [
        "//visibility:public",
    ],
)

cc_library(
    name = "builtins",
    hdrs = ["builtins.h"],
)

cc_library(
    name = "id_types",
    hdrs = ["id_types.h"],
    deps = ["@or_tools//ortools/base:strong_int"],
)

cc_library(
    name = "part_group",
    srcs = ["part_group.cc"],
    hdrs = ["part_group.h"],
    deps = [
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "part_group_test",
    size = "small",
    srcs = ["part_group_test.cc"],
    deps = [
        ":part_group",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "topic_names",
    srcs = ["topic_names.cc"],
    hdrs = ["topic_names.h"],
    deps = [
        ":id_types",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "topic_names_test",
    size = "small",
    srcs = ["topic_names_test.cc"],
    deps = [
        ":id_types",
        ":topic_names",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "slot_part_map",
    srcs = ["slot_part_map.cc"],
    hdrs = ["slot_part_map.h"],
    deps = [
        "@abseil-cpp//absl/container:btree",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_test(
    name = "slot_part_map_test",
    size = "small",
    srcs = ["slot_part_map_test.cc"],
    deps = [
        ":slot_part_map",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_cc_proto",
    ],
)

cc_library(
    name = "part_properties",
    srcs = ["part_properties.cc"],
    hdrs = ["part_properties.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_test(
    name = "part_properties_test",
    srcs = ["part_properties_test.cc"],
    deps = [
        ":part_properties",
        "@com_github_inazarenko_protobuf_matchers//protobuf-matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_cc_proto",
    ],
)

cc_library(
    name = "state_variable_path_constants",
    hdrs = [
        "state_variable_path_constants.h",
    ],
)

cc_library(
    name = "state_variable_path_util",
    srcs = [
        "state_variable_path_util.cc",
    ],
    hdrs = [
        "state_variable_path_util.h",
    ],
    deps = [
        ":state_variable_path_constants",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)
