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

cc_library(
    name = "clips_cpp",
    srcs = [
        "assert_facade.cc",
        "environment.cc",
        "fact.cc",
        "function_facade.cc",
        "function_util.cc",
        "readonly_facade.cc",
        "template.cc",
        "trace.cc",
        "value.cc",
        "value_util.cc",
    ],
    hdrs = [
        "assert_facade.h",
        "environment.h",
        "fact.h",
        "function_facade.h",
        "function_util.h",
        "readonly_facade.h",
        "slot_value.h",
        "template.h",
        "trace.h",
        "value.h",
        "value_util.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/util:log_lines",
        "//intrinsic_sdk/intrinsic/util/path_resolver",
        "//intrinsic_sdk/intrinsic/util/status:return",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_googlesource_code_re2//:re2",
        "@intrinsic-clips//clips",
        "@intrinsic_apis//intrinsic/executive/proto:clips_snapshot_cc_proto",
        "@or_tools//ortools/base:file",
    ],
)

cc_library(
    name = "clips_protobuf",
    srcs = [
        "protobuf.cc",
    ],
    hdrs = [
        "protobuf.h",
    ],
    deps = [
        ":clips_cpp",
        ":protopath",
        "//intrinsic_sdk/intrinsic/util/proto:descriptor_pools",
        "//intrinsic_sdk/intrinsic/util/proto:descriptors",
        "//intrinsic_sdk/intrinsic/util/proto:parsed_type_url",
        "//intrinsic_sdk/intrinsic/util/proto:type_url",
        "//intrinsic_sdk/intrinsic/util/status:get_extended_status",
        "//intrinsic_sdk/intrinsic/util/status:return",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/container:node_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
        "@com_google_cel_cpp//eval/public/structs:cel_proto_descriptor_pool_builder",
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/executive/proto:any_list_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_library(
    name = "protopath",
    srcs = [
        "protopath.cc",
    ],
    hdrs = [
        "protopath.h",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "clips_trace_span_manager",
    srcs = [
        "trace_span_manager.cc",
    ],
    hdrs = [
        "trace_span_manager.h",
    ],
    deps = [
        ":clips_cpp",
        "//intrinsic/stats:tracing_utils",
        "//third_party/imported/cpp_libraries/clock",
        "//intrinsic_sdk/intrinsic/util/status:return",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:die_if_null",
        "@abseil-cpp//absl/log:log_streamer",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@io_opentelemetry_cpp//api",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_library(
    name = "context",
    srcs = ["context.cc"],
    hdrs = ["context.h"],
    deps = [
        ":clips_cpp",
        ":clips_protobuf",
        ":clips_trace_span_manager",
        "//intrinsic/executive/clips/cc:clips_cel",
        "//third_party/imported/cpp_libraries/clock",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/types:span",
    ],
)
