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

package(default_visibility = ["//visibility:public"])

exports_files([
    "linters.bzl",
])

# Link this into a cc_binary/cc_test to make tcmalloc the allocator and write
# a heap profile (gzipped pprof proto) to $HEAPPROFILE at process exit. Also
# exposes intrinsic::WriteHeapProfile() for taking snapshots explicitly.
# Usually depended on via the //:heap_profiler alias; see docs/faq.md.
cc_library(
    name = "heap_profiler",
    srcs = ["heap_profiler.cc"],
    hdrs = ["heap_profiler.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@tcmalloc//tcmalloc",
        "@tcmalloc//tcmalloc:malloc_extension",
        "@tcmalloc//tcmalloc:profile_marshaler",
    ],
    alwayslink = True,
)

# Link into a C++ binary/test, then run with CPUPROFILE=<path> to write a
# gperftools CPU profile. We are using an alias here to allow us to use a
# proxy library similar to :heap_profiler if ever needed.
alias(
    name = "cpu_profiler",
    actual = "@gperftools//:cpu_profiler",
)
