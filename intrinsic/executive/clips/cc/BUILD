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
    name = "cel_utils",
    srcs = [
        "cel_utils.cc",
    ],
    hdrs = [
        "cel_utils.h",
    ],
    deps = [
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_cel_cpp//common:operators",
        "@com_google_cel_cpp//eval/public:builtin_func_registrar",
        "@com_google_cel_cpp//eval/public:cel_expr_builder_factory",
        "@com_google_cel_cpp//eval/public:cel_expression",
        "@com_google_cel_cpp//internal:strings",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "clips_cel",
    srcs = [
        "cel.cc",
    ],
    hdrs = [
        "cel.h",
    ],
    deps = [
        ":cel_utils",
        "//intrinsic/executive/clips_cpp",
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/executive/proto:behavior_call_cc_proto",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:get_extended_status",
        "//intrinsic/util/status:return",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_google_cel_cpp//eval/public:activation",
        "@com_google_cel_cpp//eval/public:cel_attribute",
        "@com_google_cel_cpp//eval/public:cel_expression",
        "@com_google_cel_cpp//eval/public:cel_value",
        "@com_google_cel_cpp//eval/public:unknown_function_result_set",
        "@com_google_cel_cpp//eval/public:unknown_set",
        "@com_google_cel_cpp//eval/public/structs:cel_proto_wrapper",
        "@com_google_cel_cpp//parser",
        "@com_google_cel_spec//proto/cel/expr:syntax_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/executive/proto:any_list_cc_proto",
        "@intrinsic_apis//intrinsic/executive/proto:any_with_assignments_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
        "@or_tools//ortools/base:strong_int",
    ],
)

cc_library(
    name = "id_handling",
    srcs = ["id_handling.cc"],
    hdrs = ["id_handling.h"],
    data = [
    ],
    deps = [
        "//intrinsic/executive/cc:behavior_tree_visitor",
        "//intrinsic/executive/clips_cpp",
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/executive/proto:behavior_tree_cc_proto",
        "//intrinsic/util:unique_id",
        "//intrinsic/util/status:get_extended_status",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:bit_gen_ref",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_googlesource_code_re2//:re2",
    ],
)

cc_library(
    name = "recovery",
    srcs = ["recovery.cc"],
    hdrs = ["recovery.h"],
    data = [
    ],
    deps = [
        ":id_handling",
        ":tracing",
        "//intrinsic/executive/cc:behavior_tree_visitor",
        "//intrinsic/executive/clips_cpp",
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/executive/clips_cpp:clips_trace_span_manager",
        "//intrinsic/executive/proto:behavior_call_cc_proto",
        "//intrinsic/executive/proto:behavior_tree_cc_proto",
        "//intrinsic/util/status:get_extended_status",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "operation_error",
    srcs = ["operation_error.cc"],
    hdrs = ["operation_error.h"],
    deps = [
        "//intrinsic/executive/clips_cpp",
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/util/status:status_conversion_rpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic/util/status:status_specs",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_googleapis//google/longrunning:longrunning_cc_proto",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)

cc_library(
    name = "skill_info",
    srcs = ["skill_info.cc"],
    hdrs = ["skill_info.h"],
    deps = [
        "//intrinsic/executive/clips_cpp",
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "time",
    srcs = ["time.cc"],
    hdrs = ["time.h"],
    deps = [
        "//intrinsic/executive/clips_cpp",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
    ],
)

cc_library(
    name = "tracing",
    srcs = ["tracing.cc"],
    hdrs = ["tracing.h"],
    data = [
    ],
    deps = [
        "//intrinsic/executive/clips_cpp",
        "//intrinsic/executive/clips_cpp:clips_protobuf",
        "//intrinsic/executive/clips_cpp:clips_trace_span_manager",
        "//intrinsic/executive/proto:behavior_call_cc_proto",
        "//intrinsic/executive/proto:behavior_tree_cc_proto",
        "//intrinsic/util/status:return",
        "//intrinsic/util/status:status_builder",
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@io_opentelemetry_cpp//api",
    ],
)
