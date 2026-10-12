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
    name = "bundleimages",
    srcs = ["bundleimages.go"],
    importpath = "intrinsic/assets/services/bundleimages",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        ":readeropener",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:imageutils",
        "//intrinsic_sdk/intrinsic/kubernetes/workcell_spec:imagetags",
        "@com_github_google_go_containerregistry//pkg/name:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1:go_default_library",
        "@com_github_google_go_containerregistry//pkg/v1/tarball:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto",
    ],
)

go_library(
    name = "readeropener",
    srcs = ["readeropener.go"],
    importpath = "intrinsic/assets/services/readeropener",
    visibility = ["//intrinsic/assets/services:__pkg__"],
)

go_library(
    name = "servicebundle",
    srcs = ["servicebundle.go"],
    importpath = "intrinsic/assets/services/servicebundle",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        ":servicevalidate",
        "//intrinsic_sdk/intrinsic/assets:imageutils",
        "//intrinsic_sdk/intrinsic/assets:ioutils",
        "//intrinsic_sdk/intrinsic/util/archive:tartooling",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@com_github_google_safearchive//tar",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/workcell_spec/proto:image_go_proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "servicevalidate",
    srcs = ["servicevalidate.go"] + [],
    importpath = "intrinsic/assets/services/servicevalidate",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:interfaceutils",
        "//intrinsic_sdk/intrinsic/assets:metadatautils",
        "//intrinsic_sdk/intrinsic/assets/dependencies:platform",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils",
        "//intrinsic_sdk/intrinsic/assets/errors:report",
        "//intrinsic_sdk/intrinsic/util/go:validate",
        "//intrinsic_sdk/intrinsic/util/proto:names",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_volume_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ] + [],
)

go_library(
    name = "servicefix",
    srcs = ["servicefix.go"],
    importpath = "intrinsic/assets/services/servicefix",
    visibility = ["//intrinsic:internal_api_users"],
    deps = [
        "//intrinsic/util/proto:descriptorcompatibility",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
    ],
)
