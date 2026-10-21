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
    name = "clienthelpers",
    srcs = ["client_helpers.go"],
    importpath = "intrinsic/storage/content_addressable_storage/pkg/clienthelpers",
    visibility = [
        "//intrinsic/assets/artifacts:__pkg__",
        "//intrinsic/kubernetes/data_store/firestore_shim:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
        "//intrinsic/storage/content_addressable_storage/pkg/crossproject:__pkg__",
        "//intrinsic/storage/content_addressable_storage/pkg/filetocas:__pkg__",
        "//intrinsic/storage/onprem_cas:__subpackages__",
    ],
    deps = [
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)
