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

# StatusBuilder to build Intrinsic platform error reports

load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "status_builder",
    srcs = ["status_builder.cc"],
    hdrs = ["status_builder.h"],
    deps = [
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "//intrinsic_sdk/intrinsic/util/proto:type_url",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@abseil-cpp//absl/debugging:symbolize",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:log_entry",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)

cc_library(
    name = "status_macros",
    hdrs = ["status_macros.h"],
    deps = [
        ":status_builder",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/base:core_headers",
    ],
)

cc_library(
    name = "status_conversion_proto",
    srcs = ["status_conversion_proto.cc"],
    hdrs = ["status_conversion_proto.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
        "@intrinsic_apis//intrinsic/util/status:status_cc_proto",
    ],
)

cc_library(
    name = "status_conversion_rpc",
    srcs = ["status_conversion_rpc.cc"],
    hdrs = ["status_conversion_rpc.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/grpc:limits",
        "//intrinsic_sdk/intrinsic/util/proto:type_url",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "status_conversion_grpc",
    srcs = ["status_conversion_grpc.cc"],
    hdrs = ["status_conversion_grpc.h"],
    deps = [
        ":status_conversion_rpc",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/rpc:code_cc_proto",
        "@com_google_googleapis//google/rpc:status_cc_proto",
    ],
)

cc_library(
    name = "log_if_error",
    hdrs = ["log_if_error.h"],
    deps = [
        ":status_builder",
        ":status_macros",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "return",
    hdrs = ["return.h"],
    deps = ["@abseil-cpp//absl/status"],
)

cc_library(
    name = "ret_check",
    srcs = ["ret_check.cc"],
    hdrs = ["ret_check.h"],
    deps = [
        ":status_builder",
        ":status_macros",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "annotate",
    srcs = ["annotate.cc"],
    hdrs = ["annotate.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "status_builder_grpc",
    srcs = ["status_builder_grpc.cc"],
    hdrs = ["status_builder_grpc.h"],
    deps = [
        ":status_builder",
        ":status_conversion_grpc",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/log:log_sink",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "status_macros_grpc",
    hdrs = ["status_macros_grpc.h"],
    deps = [
        ":status_builder",
        ":status_builder_grpc",
        ":status_conversion_grpc",
        ":status_macros",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "ret_check_grpc",
    srcs = ["ret_check_grpc.cc"],
    hdrs = ["ret_check_grpc.h"],
    deps = [
        ":ret_check",
        ":status_builder",
        ":status_builder_grpc",
        ":status_macros_grpc",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:log_severity",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "get_extended_status",
    srcs = ["get_extended_status.cc"],
    hdrs = ["get_extended_status.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/proto:type_url",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:cord",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_googleapis//google/rpc:code_cc_proto",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
    ],
)

cc_library(
    name = "status_specs",
    srcs = ["status_specs.cc"],
    hdrs = ["status_specs.h"],
    deps = [
        ":status_builder",
        "//intrinsic_sdk/intrinsic/util:proto_time",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_cc_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_cc_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_cc_proto",
        "@or_tools//ortools/base:file",
    ],
)

py_library(
    name = "status_exception",
    srcs = ["status_exception.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/grpc:limits_py",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_googleapis//google/rpc:code_py_proto",
        "@com_google_googleapis//google/rpc:status_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/logging/proto:context_py_pb2",
        "@intrinsic_apis//intrinsic/util/status:extended_status_py_pb2",
    ],
)

py_library(
    name = "status_specs_py",
    srcs = ["status_specs.py"],
    deps = [
        ":status_exception",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:status_spec_py_pb2",
        "@intrinsic_apis//intrinsic/util/status:extended_status_py_pb2",
    ],
)

py_library(
    name = "get_extended_status_py",
    srcs = ["get_extended_status.py"],
    deps = [
        "@ai_intrinsic_sdks_pip_deps//grpcio_status",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/util/status:extended_status_py_pb2",
    ],
)

go_library(
    name = "extstatus",
    srcs = ["extstatus.go"],
    importpath = "intrinsic/util/status/extstatus",
    deps = [
        "//intrinsic_sdk/intrinsic/util/grpc:grpclimits",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@org_golang_google_genproto_googleapis_rpc//status",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)
