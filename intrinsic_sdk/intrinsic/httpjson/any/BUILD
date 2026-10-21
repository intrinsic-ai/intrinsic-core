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

load("//bazel:go_macros.bzl", "go_library")

go_library(
    name = "any",
    srcs = [
        "anyresolver.go",
        "filedescriptorsetresolver.go",
        "greedyresolver.go",
        "installedassetsresolver.go",
        "protoregistryresolver.go",
        "resolver.go",
    ],
    importpath = "intrinsic/httpjson/any",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@intrinsic_apis//intrinsic/proto_tools/proto:proto_registry_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_go_proto",
        "@io_bazel_rules_go//go/runfiles:go_default_library",
        "@org_golang_google_grpc//:grpc",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure",
        "@org_golang_google_grpc//grpclog:go_default_library",
        "@org_golang_google_grpc//metadata",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)
