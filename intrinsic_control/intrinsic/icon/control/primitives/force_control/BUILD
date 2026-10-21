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
    default_visibility = [
        "//intrinsic/skills/apps/icon2:__subpackages__",
        "//intrinsic_control/intrinsic/icon:__subpackages__",
    ],
)

cc_library(
    name = "action_builder",
    srcs = ["action_builder.cc"],
    hdrs = ["action_builder.h"],
    deps = [
        ":force_primitive_utils",
        "//intrinsic/manipulation/skills/force:contact_stiffness_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_admittance_action_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_admittance_info",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:cartesian_impedance_reference_generators_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:force_primitive_info",
        "//intrinsic_control/intrinsic/icon/control/algorithms:compute_critical_damping",
        "//intrinsic_control/intrinsic/icon/control/algorithms:directional_stiffness",
        "//intrinsic_control/intrinsic/icon/control/algorithms:overdamping",
        "//intrinsic_control/intrinsic/icon/control/algorithms:selection_matrix_helpers",
        "//intrinsic_control/intrinsic/icon/control/primitives/force_control/proto:controller_params_cc_proto",
        "//intrinsic_control/intrinsic/icon/control/primitives/force_control/proto:force_primitives_cc_proto",
        "//intrinsic_control/intrinsic/icon/proto:cart_space_conversion",
        "//intrinsic_control/intrinsic/icon/proto:eigen_conversion",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/math:twist",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@intrinsic_apis//intrinsic/icon/equipment:force_control_settings_cc_proto",
        "@intrinsic_apis//intrinsic/icon/proto:cart_space_cc_proto",
        "@intrinsic_apis//intrinsic/kinematics/types:joint_limits_cc_proto",
        "@intrinsic_apis//intrinsic/math/proto:pose_cc_proto",
    ],
)

cc_library(
    name = "force_primitive_utils",
    srcs = ["force_primitive_utils.cc"],
    hdrs = ["force_primitive_utils.h"],
    deps = [
        "//intrinsic/manipulation/skills/force:contact_stiffness_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:force_primitive_cc_proto",
        "//intrinsic_control/intrinsic/icon/actions:force_primitive_info",
        "//intrinsic_control/intrinsic/icon/control/primitives/force_control/proto:force_primitives_cc_proto",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@intrinsic_apis//intrinsic/icon/equipment:force_control_settings_cc_proto",
    ] + [],
)
