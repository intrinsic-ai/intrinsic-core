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

load("//bazel:container.bzl", "container_image")
load("//bazel:go_macros.bzl", "go_binary", "go_library")

go_binary(
    name = "code_execution_service",
    srcs = ["code_execution_service_main.go"],
    importpath = "intrinsic/executive/code/code_execution_service",
    deps = [
        ":codeexecutionservice",
        "//intrinsic/executive/proto:code_execution_service_go_proto",
        "//intrinsic/kubernetes:intrinsic",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_info_service_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

go_library(
    name = "jupyterclient",
    srcs = [
        "jupyter_client.go",
        "websocket_client.go",
    ],
    importpath = "intrinsic/executive/code/jupyterclient",
    visibility = ["//intrinsic/executive/code:__subpackages__"],
    deps = [
        ":jupyter",
        "//intrinsic_sdk/intrinsic/util/status:extstatus",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_gorilla_websocket//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@org_golang_google_genproto_googleapis_rpc//status",
    ],
)

go_library(
    name = "codeexecutionworker",
    srcs = ["code_execution_worker.go"],
    importpath = "intrinsic/executive/code/codeexecutionworker",
    visibility = ["//intrinsic/executive/code:__subpackages__"],
    deps = [
        ":jupyterclient",
        ":pycode",
        "//intrinsic/executive/proto:code_execution_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "codeexecutionservice",
    srcs = [
        "code_execution_service.go",
    ],
    importpath = "intrinsic/executive/code/codeexecutionservice",
    deps = [
        ":codeexecutionworker",
        ":jupyterclient",
        ":pycode",
        "//intrinsic/executive/proto:code_execution_service_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/executive/proto:code_execution_info_service_go_proto",
        "@org_golang_google_genproto_googleapis_rpc//status",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_library(
    name = "jupyter",
    srcs = ["jupyter.go"],
    importpath = "intrinsic/executive/code/jupyter",
    visibility = ["//intrinsic/executive/code:__subpackages__"],
)

go_library(
    name = "pycode",
    srcs = ["pycode.go"],
    embedsrcs = [
        "templates/displayed_code.py.template",  # keep
        "templates/executed_code.py.template",  # keep
        "templates/seed_code.py.template",  # keep
    ],
    importpath = "intrinsic/executive/code/pycode",
    visibility = ["//intrinsic/executive/code:__subpackages__"],
    deps = [
        "@com_github_golang_glog//:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

container_image(
    name = "code-execution-service-image",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    files = [":code_execution_service"],
    visibility = [
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)
