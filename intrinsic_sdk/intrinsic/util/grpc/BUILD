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

"""Helper utilities for applications using grpc."""

load("//bazel:cc_macros.bzl", "cc_library")
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

package(
    default_visibility = ["//visibility:public"],
)

cc_library(
    name = "channel_interface",
    srcs = ["channel_interface.cc"],
    hdrs = ["channel_interface.h"],
    deps = [
        ":grpc",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

cc_library(
    name = "auth",
    srcs = ["auth.cc"],
    hdrs = ["auth.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/config:environments_cc",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_github_nlohmann_json//:json",
    ],
)

py_library(
    name = "auth_py",
    srcs = ["auth.py"],
    deps = [
        ":userconfig",
        "//intrinsic_sdk/intrinsic/config:environments_py",
        "//intrinsic_sdk/intrinsic/kubernetes/acl/py:jwt",
        "@ai_intrinsic_sdks_pip_deps//retrying",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
    ],
)

py_library(
    name = "dialerutil",
    srcs = ["dialerutil.py"],
    deps = [
        ":auth_py",
        "//intrinsic_sdk/intrinsic/config:environments_py",
        "//intrinsic_sdk/intrinsic/kubernetes/acl/py:identity",
        "//intrinsic_sdk/intrinsic/kubernetes/acl/py:jwt",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@intrinsic_apis//intrinsic/frontend/cloud/api/v1:solutiondiscovery_api_py_pb2",
        "@intrinsic_apis//intrinsic/frontend/cloud/api/v1:solutiondiscovery_api_py_pb2_grpc",
    ],
)

cc_library(
    name = "channel",
    srcs = ["channel.cc"],
    hdrs = ["channel.h"],
    deps = [
        ":auth",
        ":channel_interface",
        ":connection_params",
        ":grpc",
        "//intrinsic_sdk/intrinsic/connect/cc/grpc:channel",
        "//intrinsic_sdk/intrinsic/kubernetes/acl/cc:cookie_names",
        "//intrinsic_sdk/intrinsic/kubernetes/acl/cc:header_names",
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/frontend/cloud/api/v1:solutiondiscovery_api_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/frontend/cloud/api/v1:solutiondiscovery_api_cc_proto",
    ],
)

cc_library(
    name = "connection_params",
    srcs = ["connection_params.cc"],
    hdrs = ["connection_params.h"],
    deps = [
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "connection_cache",
    hdrs = ["connection_cache.h"],
    deps = [
        ":channel",
        ":connection_params",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/synchronization",
    ],
)

py_library(
    name = "connection",
    srcs = [
        "connection.py",
    ],
)

cc_library(
    name = "grpc",
    srcs = ["grpc.cc"],
    hdrs = ["grpc.h"],
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        ":limits",
        "//intrinsic_sdk/intrinsic/util/thread",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_github_grpc_grpc//:grpc_security_base",
        "@com_github_grpc_grpc//src/proto/grpc/health/v1:health_cc_proto",
    ],
)

cc_library(
    name = "limits",
    hdrs = ["limits.h"],
    visibility = ["//intrinsic:public_api_users"],
)

py_library(
    name = "limits_py",
    srcs = ["limits.py"],
    visibility = ["//intrinsic:public_api_users"],
)

go_library(
    name = "grpclimits",
    srcs = ["limits.go"],
    importpath = "intrinsic/util/grpc/grpclimits",
    visibility = ["//intrinsic:public_api_users"],
)

py_library(
    name = "interceptor",
    srcs = ["interceptor.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/util:decorators",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
    ],
)

py_library(
    name = "error_handling",
    srcs = ["error_handling.py"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:get_extended_status_py",
        "@ai_intrinsic_sdks_pip_deps//retrying",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_absl_py//absl/logging",
        "@com_google_googleapis//google/rpc:status_py_proto",
        "@com_google_protobuf//:protobuf_python",
    ],
)

go_library(
    name = "statusutil",
    srcs = ["status_util.go"],
    importpath = "intrinsic/util/grpc/statusutil",
    deps = [
        "@com_github_golang_glog//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

py_library(
    name = "userconfig",
    srcs = ["userconfig.py"],
)
