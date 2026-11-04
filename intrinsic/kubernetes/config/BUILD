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

# Deployment configuration related libraries

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//intrinsic/kubernetes:intrinsic_kubernetes_users"])

go_library(
    name = "crcconfig",
    srcs = ["crc_config.go"],
    importpath = "intrinsic/kubernetes/config/crcconfig",
    deps = [
        "@com_google_cloud_go_storage//:storage",
        "@org_golang_google_api//option:go_default_library",
    ],
)
