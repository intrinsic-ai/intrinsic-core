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
    name = "assetconfigurationservice",
    srcs = ["asset_configuration_service.go"],
    importpath = "intrinsic/assets/configuration/assetconfigurationservice",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic/assets/dependencies:traversal",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/service:resourcereader",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "//intrinsic_sdk/intrinsic/assets:typeutils",
        "//intrinsic_sdk/intrinsic/util/proto:names",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_configuration_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:dependency_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:recommended_configuration_config_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@org_golang_google_grpc//codes",
        "@org_golang_google_grpc//status",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)
