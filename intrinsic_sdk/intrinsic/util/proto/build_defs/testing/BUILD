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
    default_testonly = True,
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "text_proto_test",
    srcs = ["text_proto_test.cc"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/proto:dynamic_message_parser",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:status_matchers",
        "@com_google_cel_cpp//internal:testing_no_main",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
        "@or_tools//ortools/base:file",
    ],
    alwayslink = True,
)
