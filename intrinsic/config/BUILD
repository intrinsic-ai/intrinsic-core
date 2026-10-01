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
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

# Application, process and resource set describe all user-accessible configuration of a
# solution.

load("//intrinsic/assets/build_defs:solution.bzl", "intrinsic_solution")

package(default_visibility = ["//visibility:public"])

go_library(
    name = "collections",
    srcs = ["collections.go"],
    importpath = "intrinsic/config/collections",
    visibility = ["//intrinsic:intrinsic_internal"],
)

go_library(
    name = "util",
    srcs = ["util.go"],
    importpath = "intrinsic/config/util",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic/config/proto:any_wrapper_go_proto",
        "//intrinsic/kubernetes/workcell_spec:workcellspec",
        "@com_google_cloud_go_firestore//:firestore",
        "@in_gopkg_yaml_v3//:go_default_library",
        "@intrinsic_apis//intrinsic/config/proto:data_files_go_proto",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protopath:go_default_library",
        "@org_golang_google_protobuf//reflect/protorange:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

cc_library(
    name = "environments_cc",
    srcs = ["environments.cc"],
    hdrs = ["environments.h"],
    visibility = [
        "//visibility:public",
    ],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
    ],
)

py_library(
    name = "environments_py",
    srcs = ["environments.py"],
    visibility = [
        "//visibility:public",
    ],
)

go_library(
    name = "environments",
    srcs = ["environments.go"],
    importpath = "intrinsic/config/environments",
    visibility = [
        "//visibility:public",
    ],
)

go_library(
    name = "convertrunnables",
    srcs = ["convert_runnables.go"],
    importpath = "intrinsic/config/convertrunnables",
    visibility = [
        "//intrinsic/assets/build_defs:__pkg__",
        "//intrinsic/assets/deploy:__subpackages__",
        "//intrinsic/kubernetes/data_store:__subpackages__",
        "//intrinsic/kubernetes/workcell_spec:__subpackages__",
        "//intrinsic/storage/hot_shared_state:__pkg__",
        "//intrinsic/tools/inctl/cmd:__pkg__",
    ],
    deps = [
        "//intrinsic/config/proto:process_go_proto",
        "//intrinsic/config/proto:process_internal_go_proto",
        "@com_github_pkg_errors//:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

intrinsic_solution(
    name = "empty_application",
    display_name = "Empty solution",
    visibility = ["//intrinsic:intrinsic_internal"],
)

go_library(
    name = "datafiles",
    srcs = ["data_files.go"],
    importpath = "intrinsic/config/datafiles",
    visibility = [
        "//intrinsic/assets/build_defs:__pkg__",
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/tools/inctl:__subpackages__",
    ],
    deps = [
        "//intrinsic/assets:referenceddata",
        "//intrinsic/assets/data:utils",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:transfer_go_proto",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_go_proto",
    ],
)

go_library(
    name = "applicationview",
    srcs = ["application_view.go"],
    importpath = "intrinsic/config/applicationview",
    visibility = [
        "//intrinsic/kubernetes/data_store:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/storage/hot_shared_state:__pkg__",
    ],
    deps = [
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:common_go_proto",
        "//intrinsic/config/proto:process_go_proto",
        "//intrinsic/config/proto:resource_set_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@org_golang_google_protobuf//proto",
    ],
)

go_library(
    name = "operationmode",
    srcs = ["operation_mode.go"],
    importpath = "intrinsic/config/operationmode",
    deps = ["@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto"],
)
