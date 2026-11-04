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

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library")

go_library(
    name = "handlers",
    srcs = [
        "connectedcas.go",
        "localcas.go",
        "lrop.go",
        "preload.go",
    ],
    importpath = "intrinsic/storage/onprem_cas/internal/handlers",
    visibility = ["//intrinsic/storage/onprem_cas:__subpackages__"],
    deps = [
        "//intrinsic/storage/content_addressable_storage/pkg/idhandling",
        "//intrinsic/storage/onprem_cas/internal",
        "//intrinsic/storage/onprem_cas/internal/syncset",
        "//intrinsic/storage/onprem_cas_preload/proto/v1:ocp_service_go_proto",
        "//intrinsic/storage/onprem_cas_preload/proto/v1:preload_set_go_proto",
        "@com_github_google_uuid//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@org_golang_google_genproto_googleapis_rpc//status:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb:go_default_library",
        "@org_golang_google_protobuf//types/known/durationpb:go_default_library",
        "@org_golang_google_protobuf//types/known/emptypb:go_default_library",
        "@org_golang_google_protobuf//types/known/timestamppb:go_default_library",
    ],
)
