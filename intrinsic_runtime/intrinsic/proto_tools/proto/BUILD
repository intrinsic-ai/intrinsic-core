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
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(default_visibility = ["//intrinsic_runtime/intrinsic/proto_tools/registry:__subpackages__"])

proto_library(
    name = "proto_registry_internal_proto",
    srcs = ["proto_registry_internal.proto"],
)

cc_proto_library(
    name = "proto_registry_internal_cc_proto",
    deps = [":proto_registry_internal_proto"],
)

go_proto_library(
    name = "proto_registry_internal_go_proto",
    importpath = "github.com/intrinsic-ai/insrc/intrinsic_runtime/intrinsic/proto_tools/proto",
    proto = ":proto_registry_internal_proto",
)

proto_library(
    name = "proto_registry_pubsub_proto",
    srcs = ["proto_registry_pubsub.proto"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
)

go_proto_library(
    name = "proto_registry_pubsub_go_proto",
    importpath = "github.com/intrinsic-ai/insrc/intrinsic_runtime/intrinsic/proto_tools/proto/proto_registry_pubsub_go_proto",
    proto = ":proto_registry_pubsub_proto",
)

cc_proto_library(
    name = "proto_registry_pubsub_cc_proto",
    deps = [":proto_registry_pubsub_proto"],
)
