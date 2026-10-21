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

load("//bazel:cc_macros.bzl", "cc_library", "pybind_extension")
load("//bazel:go_macros.bzl", "go_library")
load("//bazel:python.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "repeated_field_util",
    hdrs = ["repeated_field_util.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "any",
    hdrs = ["any.h"],
    deps = [
        ":merge",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "dynamic_message_parser",
    srcs = ["dynamic_message_parser.cc"],
    hdrs = ["dynamic_message_parser.h"],
    deps = [
        ":descriptor_pools",
        ":descriptors",
        ":parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

go_library(
    name = "descriptor",
    srcs = ["descriptor.go"],
    importpath = "intrinsic/util/proto/descriptor",
    deps = [
        ":sourcecodeinfoview",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_kylelemons_godebug//diff",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

cc_library(
    name = "descriptors",
    srcs = ["descriptors.cc"],
    hdrs = ["descriptors.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "descriptor_pools",
    srcs = ["descriptor_pools.cc"],
    hdrs = ["descriptor_pools.h"],
    deps = [
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
    ],
)

py_library(
    name = "descriptors_py",
    srcs = ["descriptors.py"],
    deps = ["@com_google_protobuf//:protobuf_python"],
)

cc_library(
    name = "get_text_proto",
    srcs = ["get_text_proto.cc"],
    hdrs = ["get_text_proto.h"],
    deps = [
        ":any",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_library(
    name = "merge",
    srcs = ["merge.cc"],
    hdrs = ["merge.h"],
    deps = [
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/types:span",
        "@com_google_protobuf//:protobuf",
    ],
)

go_library(
    name = "protoio",
    srcs = ["protoio.go"],
    importpath = "intrinsic/util/proto/protoio",
    deps = [
        "@com_github_protocolbuffers_txtpbfmt//config:go_default_library",
        "@com_github_protocolbuffers_txtpbfmt//parser:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
    ],
)

go_library(
    name = "registryutil",
    srcs = ["registryutil.go"],
    importpath = "intrinsic/util/proto/registryutil",
    deps = [
        ":protoio",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/dynamicpb:go_default_library",
    ],
)

cc_library(
    name = "source_code_info_view",
    srcs = ["source_code_info_view.cc"],
    hdrs = ["source_code_info_view.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

pybind_extension(
    name = "source_code_info_view_py",
    srcs = ["source_code_info_view_py.cc"],
    deps = [
        ":source_code_info_view",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
        "@pybind11_abseil//pybind11_abseil:absl_casters",
        "@pybind11_abseil//pybind11_abseil:status_casters",
        "@pybind11_protobuf//pybind11_protobuf:wrapped_proto_caster",
    ],
)

cc_library(
    name = "type_url",
    hdrs = ["type_url.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

go_library(
    name = "typeurl",
    srcs = ["typeurl.go"],
    importpath = "intrinsic/util/proto/typeurl",
)

cc_library(
    name = "error_collector",
    hdrs = ["error_collector.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//src/google/protobuf/io:tokenizer",
    ],
)

cc_library(
    name = "status_specs",
    hdrs = ["status_specs.h"],
    deps = [
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "parsed_type_url",
    srcs = ["parsed_type_url.cc"],
    hdrs = [
        "parsed_type_url.h",
    ],
    deps = [
        ":status_specs",
        ":type_url",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

cc_library(
    name = "parse_text_proto",
    srcs = ["parse_text_proto.cc"],
    hdrs = ["parse_text_proto.h"],
    deps = [
        ":error_collector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
        "@com_googlesource_code_re2//:re2",
    ],
)

go_library(
    name = "sourcecodeinfoview",
    srcs = ["source_code_info_view.go"],
    importpath = "intrinsic/util/proto/sourcecodeinfoview",
    deps = [
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_library(
    name = "names",
    srcs = ["names.go"],
    importpath = "intrinsic/util/proto/names",
    deps = ["@org_golang_google_protobuf//types/known/anypb"],
)

go_library(
    name = "walkmessages",
    srcs = ["walk_messages.go"],
    importpath = "intrinsic/util/proto/walkmessages",
    deps = [
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
    ],
)

go_library(
    name = "fieldbehavior",
    srcs = ["field_behavior.go"],
    importpath = "intrinsic/util/proto/fieldbehavior",
    deps = [
        "@org_golang_google_genproto_googleapis_api//annotations",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb",
    ],
)
