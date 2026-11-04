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

load("//intrinsic_sdk/bazel:sh_macros.bzl", "sh_library")

# Utility helper functions
package(default_visibility = ["//visibility:public"])

sh_library(
    name = "integration_test_util",
    testonly = True,
    srcs = ["integration_test_util.sh"],
    data = [
        "@kubebuilder_test_tools//:kubectl",
    ] + [],
)
