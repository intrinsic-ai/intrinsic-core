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

package(default_visibility = ["//visibility:public"])

java_binary(
    name = "firestore-emulator",
    main_class = "com.google.cloud.datastore.emulator.firestore.CloudFirestore",
    runtime_deps = [
        "@firestore_emulator//jar",
    ],
)

# This test just makes sure we can build and link the profilers.
# Set CPUPROFILE=<path> to write a gperftools CPU profile, and/or
# HEAPPROFILE=<path> to write a tcmalloc heap profile (gzipped pprof proto).
# TODO(b/390153423): Consider linking it into every cc target like in g3.
cc_test(
    name = "gperftools_test",
    srcs = ["gperftools_test.cc"],
    deps = [
        "//bazel/cpp:cpu_profiler",
        "//bazel/cpp:heap_profiler",
        "@com_google_googletest//:gtest_main",
    ],
)
