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
    name = "syncset",
    srcs = [
        "controller.go",
        "metrics.go",
        "statustracking.go",
        "store.go",
        "syncset.go",
    ],
    importpath = "intrinsic/storage/onprem_cas/internal/syncset",
    visibility = ["//intrinsic/storage/onprem_cas:__subpackages__"],
    deps = [
        "//intrinsic/storage/onprem_cas/internal/activeset",
        "//intrinsic/storage/onprem_cas/internal/transfer",
        "@io_opencensus_go//stats:go_default_library",
        "@io_opencensus_go//stats/view:go_default_library",
        "@io_opencensus_go//tag:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
    ],
)
