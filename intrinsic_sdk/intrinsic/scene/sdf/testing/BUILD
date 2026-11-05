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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "sdf_matchers",
    testonly = 1,
    hdrs = ["sdf_matchers.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/simulation/gazebo:type_conversion",
        "@com_google_googletest//:gtest",
        "@gz-math//eigen3",
        "@sdformat",
    ],
)

cc_library(
    name = "xml_matchers",
    testonly = 1,
    srcs = ["xml_matchers.cc"],
    hdrs = ["xml_matchers.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest",
        "@libxml2",
    ],
)

cc_test(
    name = "xml_matchers_test",
    srcs = ["xml_matchers_test.cc"],
    deps = [
        ":xml_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
    ],
)
