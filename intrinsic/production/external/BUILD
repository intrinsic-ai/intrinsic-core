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

load("@rules_java//java:java_binary.bzl", "java_binary")
load("//bazel:cc_macros.bzl", "cc_test")
load("//bazel:python.bzl", "py_test")

package(default_visibility = ["//visibility:public"])

# Used by compile_pip_requirements.
exports_files(
    srcs = ["requirements.txt"],
    visibility = ["//:__pkg__"],
)

exports_files(
    srcs = [
        "intrinsic.bazelrc",
        "intrinsic.bazelversion",
    ],
    visibility = [
        "//intrinsic/tools/inctl/cmd/bazel/templates:__pkg__",
    ],
)

py_test(
    name = "grpc_synchronized_py_test",
    size = "small",
    srcs = ["grpc_synchronized_test.py"],
    args = [
        "--module-bazel",
        "$(location //:MODULE.bazel)",
        "--requirements-in",
        "$(location //:requirements.in)",
        "--requirements-txt",
        "$(location //:requirements.txt)",
    ],
    data = [
        "//:MODULE.bazel",
        "//:requirements.in",
        "//:requirements.txt",
    ],
    main = "grpc_synchronized_test.py",
)

java_binary(
    name = "firestore-emulator",
    main_class = "com.google.cloud.datastore.emulator.firestore.CloudFirestore",
    runtime_deps = [
        "@firestore_emulator//jar",
    ],
)

# This test just makes sure we can build and link the profiler.
# It does not use it.
# TODO(b/390153423): Consider linking it into every cc target like in g3.
cc_test(
    name = "gperftools_test",
    srcs = ["gperftools_test.cc"],
    deps = [
        "@com_google_googletest//:gtest_main",
        "@gperftools//:cpu_profiler",
        "@tcmalloc//tcmalloc",
    ],
)
