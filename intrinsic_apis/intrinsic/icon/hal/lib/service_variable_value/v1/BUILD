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

load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "service_variable_config_proto",
    srcs = ["service_variable_config.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@intrinsic_apis//intrinsic/icon/hal/lib/fieldbus/v1:value_parsing_proto",
    ],
)

cc_proto_library(
    name = "service_variable_config_cc_proto",
    deps = [":service_variable_config_proto"],
)

go_proto_library(
    name = "service_variable_config_go_proto",
    importpath = "intrinsic/icon/hal/lib/service_variable_value/v1/service_variable_config_go_proto",
    protos = [":service_variable_config_proto"],
    deps = ["@intrinsic_apis//intrinsic/icon/hal/lib/fieldbus/v1:value_parsing_go_proto"],
)

proto_library(
    name = "service_variable_export_proto",
    srcs = ["service_variable_export.proto"],
    deps = [
        ":service_variable_config_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "service_variable_export_cc_proto",
    deps = [":service_variable_export_proto"],
)

go_proto_library(
    name = "service_variable_export_go_proto",
    importpath = "intrinsic/icon/hal/lib/service_variable_value/v1/service_variable_export_go_proto",
    protos = [":service_variable_export_proto"],
    deps = [":service_variable_config_go_proto"],
)
