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

package(default_visibility = ["//visibility:public"])

alias(
    name = "repeated_field_util",
    actual = "//intrinsic_sdk/intrinsic/util/proto:repeated_field_util",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "any",
    actual = "//intrinsic_sdk/intrinsic/util/proto:any",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

cc_library(
    name = "compile_utils",
    srcs = ["compile_utils.cc"],
    hdrs = ["compile_utils.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
    ],
)

go_library(
    name = "descriptorcompatibility",
    srcs = ["descriptor_compatibility.go"],
    importpath = "intrinsic/util/proto/descriptorcompatibility",
    deps = [
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

alias(
    name = "dynamic_message_parser",
    actual = "//intrinsic_sdk/intrinsic/util/proto:dynamic_message_parser",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "descriptor",
    actual = "//intrinsic_sdk/intrinsic/util/proto:descriptor",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "descriptors",
    actual = "//intrinsic_sdk/intrinsic/util/proto:descriptors",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "descriptor_pools",
    actual = "//intrinsic_sdk/intrinsic/util/proto:descriptor_pools",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "descriptors_py",
    actual = "//intrinsic_sdk/intrinsic/util/proto:descriptors_py",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "get_text_proto",
    actual = "//intrinsic_sdk/intrinsic/util/proto:get_text_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

cc_library(
    name = "pb_hash",
    hdrs = ["pb_hash.h"],
    deps = [
        "@com_google_protobuf//:differencer",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io",
    ],
)

alias(
    name = "protoio",
    actual = "//intrinsic_sdk/intrinsic/util/proto:protoio",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "registryutil",
    actual = "//intrinsic_sdk/intrinsic/util/proto:registryutil",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "type_url",
    actual = "//intrinsic_sdk/intrinsic/util/proto:type_url",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "typeurl",
    actual = "//intrinsic_sdk/intrinsic/util/proto:typeurl",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "error_collector",
    actual = "//intrinsic_sdk/intrinsic/util/proto:error_collector",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "parsed_type_url",
    actual = "//intrinsic_sdk/intrinsic/util/proto:parsed_type_url",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "parse_text_proto",
    actual = "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "sourcecodeinfoview",
    actual = "//intrinsic_sdk/intrinsic/util/proto:sourcecodeinfoview",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "names",
    actual = "//intrinsic_sdk/intrinsic/util/proto:names",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "walkmessages",
    actual = "//intrinsic_sdk/intrinsic/util/proto:walkmessages",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

cc_library(
    name = "walk_messages",
    srcs = ["walk_messages.cc"],
    hdrs = ["walk_messages.h"],
    deps = [
        "//intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_google_protobuf//:protobuf",
    ],
)

alias(
    name = "fieldbehavior",
    actual = "//intrinsic_sdk/intrinsic/util/proto:fieldbehavior",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)
