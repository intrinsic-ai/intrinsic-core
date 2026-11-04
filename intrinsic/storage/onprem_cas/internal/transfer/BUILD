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
    name = "transfer",
    srcs = [
        "controller.go",
        "downloader.go",
        "dualstatter.go",
        "metrics.go",
        "queue.go",
        "statcache.go",
        "transfer.go",
        "uploader.go",
    ],
    importpath = "intrinsic/storage/onprem_cas/internal/transfer",
    visibility = ["//intrinsic/storage/onprem_cas:__subpackages__"],
    deps = [
        "//intrinsic/storage/content_addressable_storage/pkg/clienthelpers",
        "//intrinsic/storage/content_addressable_storage/pkg/idhandling",
        "@com_github_hashicorp_golang_lru_v2//:go_default_library",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_opencensus_go//stats:go_default_library",
        "@io_opencensus_go//stats/view:go_default_library",
        "@io_opencensus_go//tag:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)
