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

filegroup(
    name = "clips",
    srcs = glob([
        "*.clp",
        "behavior_tree/*.clp",
    ]),
)

cc_library(
    name = "clips_init",
    srcs = ["clips_init.cc"],
    hdrs = ["clips_init.h"],
    data = [
        ":clips",
    ],
    deps = [
        "//intrinsic/executive/clips/cc:id_handling",
        "//intrinsic/executive/clips/cc:recovery",
        "//intrinsic/executive/clips/cc:time",
        "//intrinsic/executive/clips/cc:tracing",
        "//intrinsic/executive/clips_cpp",
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/executive/clips_cpp:context",
        "//intrinsic/executive/proto:executive_events_cc_proto",
        "//intrinsic/executive/proto:process_tree_cc_proto",  # fixdeps: keep
        "//intrinsic/executive/proto:run_metadata_cc_proto",
        "//intrinsic/util/path_resolver",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/executive/proto:run_response_cc_proto",
        "@intrinsic_apis//intrinsic/executive/proto:world_query_cc_proto",
        "@or_tools//ortools/base:path",
    ],
)
