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

load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

# Data structures for path planning

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:protos.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

cc_proto_library(
    name = "nearest_neighbor_configs_cc_proto",
    deps = [":nearest_neighbor_configs_proto"],
)

proto_library(
    name = "nearest_neighbor_configs_proto",
    srcs = ["nearest_neighbor_configs.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = ["@com_google_protobuf//:any_proto"],
)

cc_proto_library(
    name = "planner_graph_cc_proto",
    deps = [":planner_graph_proto"],
)

proto_library(
    name = "planner_graph_proto",
    srcs = ["planner_graph.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = [
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning/data_structures:nearest_neighbor_configs_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
    ],
)

cc_proto_library(
    name = "state_sampler_configs_cc_proto",
    deps = [":state_sampler_configs_proto"],
)

proto_library(
    name = "state_sampler_configs_proto",
    srcs = ["state_sampler_configs.proto"],
    # keep
    strip_import_prefix = "/intrinsic_motion_planning",
    deps = ["@com_google_protobuf//:any_proto"],
)

cc_library(
    name = "rrt_scoring_heuristic_state_sampler",
    srcs = ["rrt_scoring_heuristic_state_sampler.cc"],
    hdrs = ["rrt_scoring_heuristic_state_sampler.h"],
    deps = [
        ":nearest_neighbor_utils",
        ":path_planner_graph",
        ":planner_graph_cc_proto",
        ":state_sampler",
        ":state_sampler_configs_cc_proto",
        ":uniform_state_sampler",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:labels",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "prm_heuristic_state_sampler",
    srcs = ["prm_heuristic_state_sampler.cc"],
    hdrs = ["prm_heuristic_state_sampler.h"],
    deps = [
        ":path_planner_graph",
        ":planner_graph_cc_proto",
        ":state_sampler",
        ":state_sampler_configs_cc_proto",
        ":uniform_state_sampler",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:validators_config_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "state_sampler",
    srcs = ["state_sampler.cc"],
    hdrs = ["state_sampler.h"],
    deps = [
        ":path_planner_graph",
        ":state_sampler_configs_cc_proto",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_kinematics/intrinsic/kinematics/types:check_joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@cppregpattern//:registry",
    ],
)

cc_library(
    name = "state_sampler_utils",
    srcs = ["state_sampler_utils.cc"],
    hdrs = ["state_sampler_utils.h"],
    deps = [
        ":state_sampler",
        ":state_sampler_configs_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "uniform_state_sampler",
    srcs = ["uniform_state_sampler.cc"],
    hdrs = ["uniform_state_sampler.h"],
    deps = [
        ":path_planner_graph",
        ":state_sampler",
        ":state_sampler_configs_cc_proto",
        "//intrinsic/eigenmath:halton_sequence",
        "//intrinsic/eigenmath:random_number_utils",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:kinematics_system_proxy",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:clamp",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@boost.math",
        "@com_google_protobuf//:protobuf",
    ],
    alwayslink = 1,  # Required for go/registerer
)

cc_library(
    name = "nearest_neighbor",
    hdrs = ["nearest_neighbor.h"],
    deps = [
        ":nearest_neighbor_configs_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "linear_nearest_neighbor",
    hdrs = ["linear_nearest_neighbor.h"],
    deps = [
        ":nearest_neighbor",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "nearest_neighbor_utils",
    hdrs = ["nearest_neighbor_utils.h"],
    deps = [
        ":linear_nearest_neighbor",
        ":nearest_neighbor",
        ":nearest_neighbor_configs_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "path_planner_graph",
    srcs = ["path_planner_graph.cc"],
    hdrs = ["path_planner_graph.h"],
    copts = ["-fexceptions"],  #A* requires exceptions.
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    deps = [
        ":nearest_neighbor",
        ":nearest_neighbor_configs_cc_proto",
        ":nearest_neighbor_utils",
        ":planner_graph_cc_proto",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:path_planner_definitions",
        "//intrinsic_motion_planning/intrinsic/motion_planning/path_planning:robot_chain",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/util:eigen",
        "//intrinsic_sdk/intrinsic/util:macros",
        "//intrinsic_sdk/intrinsic/util/proto:get_text_proto",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:optional",
        "@boost.graph",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_cc_proto",
        "@intrinsic_apis//intrinsic/messages:messages_cc_proto",
    ],
)

go_proto_library(
    name = "nearest_neighbor_configs_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/data_structures/nearest_neighbor_configs_go_proto",
    proto = ":nearest_neighbor_configs_proto",
)

go_proto_library(
    name = "planner_graph_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/data_structures/planner_graph_go_proto",
    proto = ":planner_graph_proto",
    deps = [
        ":nearest_neighbor_configs_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
    ],
)

go_proto_library(
    name = "state_sampler_configs_go_proto",
    importpath = "intrinsic/motion_planning/path_planning/data_structures/state_sampler_configs_go_proto",
    proto = ":state_sampler_configs_proto",
)
