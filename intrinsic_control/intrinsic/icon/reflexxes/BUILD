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

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "reflexxes",
    srcs = [
        "calc_min_execution_time_utils.cc",
        "input_output_utility_functions.cc",
        "inputs.cc",
        "motion_polynomials.cc",
        "outputs.cc",
        "polynomial_solvers.cc",
        "position_calc_exec_time_funcs.cc",
        "position_calc_inoperative_zone_end.cc",
        "position_calc_inoperative_zone_start.cc",
        "position_calc_min_execution_time.cc",
        "position_calc_min_execution_time_per_dof.cc",
        "position_calc_traj_funcs.cc",
        "position_inputs.cc",
        "position_synchronize_dofs.cc",
        "reflexxes_api.cc",
        "synchronize_dofs_base.cc",
        "velocity_calc_min_execution_time.cc",
        "velocity_inputs.cc",
        "velocity_synchronize_dofs.cc",
    ],
    hdrs = [
        "constants_external.h",
        "flags.h",
        "inputs.h",
        "internal/calc_min_execution_time_utils.h",
        "internal/constants.h",
        "internal/decision_tree_utility_functions.h",
        "internal/functional.h",
        "internal/input_output_utility_functions.h",
        "internal/intermediate_profile_funcs.h",
        "internal/math.h",
        "internal/motion_state.h",
        "internal/motion_state_transforms.h",
        "internal/motion_state_with_polys.h",
        "internal/polynomial_solvers.h",
        "internal/position.h",
        "internal/position_calc_exec_time_funcs.h",
        "internal/position_calc_min_execution_time_base.h",
        "internal/position_calc_traj_funcs.h",
        "internal/synchronize_dofs_base.h",
        "internal/velocity.h",
        "motion_polynomials.h",
        "outputs.h",
        "position_flags.h",
        "position_inputs.h",
        "position_outputs.h",
        "profile.h",
        "reflexxes_api.h",
        "status.h",
        "velocity_flags.h",
        "velocity_inputs.h",
        "velocity_outputs.h",
    ],
    deps = [
        "//intrinsic/util:functional",
        "//intrinsic_sdk/intrinsic/util:fixed_vector",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/functional:bind_front",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)
