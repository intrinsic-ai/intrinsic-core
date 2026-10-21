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
    name = "filetocas",
    srcs = ["filetocas.go"],
    importpath = "intrinsic/storage/content_addressable_storage/pkg/filetocas",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/config:__pkg__",
        "//intrinsic/tools/inctl/cmd:__pkg__",
        "//intrinsic/tools/inctl/util:__pkg__",
        "//intrinsic_sdk/intrinsic/assets/catalog:__pkg__",
        "//intrinsic_sdk/intrinsic/assets/inctl:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:__pkg__",
    ],
    deps = [
        "//intrinsic/kubernetes/workcell_spec:author",
        "//intrinsic/storage/content_addressable_storage/pkg/clienthelpers",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_google_cloud_go_storage//:storage",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_opencensus_go//trace:go_default_library",
    ],
)
