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
load("//bazel:python.bzl", "py_test")

# With bazel the pybind_extension does not work if the visibility is restricted.
package(default_visibility = ["//visibility:public"])

# Use the Python wrapper in intrinsic/manipulation/skills/force/utils.py to access this library.
pybind_extension(
    name = "directional_stiffness",
    srcs = ["directional_stiffness.cc"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/algorithms:directional_stiffness",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status:statusor",
        "@pybind11_abseil//pybind11_abseil:status_casters",
    ],
)

py_test(
    name = "directional_stiffness_test",
    srcs = ["directional_stiffness_test.py"],
    deps = [
        ":directional_stiffness",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_google_absl_py//absl/testing:absltest",
    ],
)

py_test(
    name = "overdamping_test",
    srcs = ["overdamping_test.py"],
    deps = [
        ":overdamping",
        "@ai_intrinsic_sdks_pip_deps//numpy",
        "@com_google_absl_py//absl/testing:absltest",
    ],
)

# Use the Python wrapper in intrinsic/manipulation/skills/force/utils.py to access this library.
pybind_extension(
    name = "overdamping",
    srcs = ["overdamping.cc"],
    deps = [
        "//intrinsic_control/intrinsic/icon/control/algorithms:overdamping",
        "//intrinsic_control/intrinsic/icon/utils:realtime_status_macro",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/status:statusor",
        "@pybind11_abseil//pybind11_abseil:status_casters",
    ],
)
