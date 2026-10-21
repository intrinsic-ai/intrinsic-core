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
    name = "crossproject",
    srcs = ["cross_project.go"],
    importpath = "intrinsic/storage/content_addressable_storage/pkg/crossproject",
    visibility = [
        "//intrinsic/assets/artifacts:__pkg__",
        "//intrinsic/assets/build_defs:__pkg__",
        "//intrinsic/tools/inctl/util:__pkg__",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:__pkg__",
    ],
    deps = [
        "//intrinsic/storage/content_addressable_storage/pkg/clienthelpers",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_opencensus_go//trace:go_default_library",
    ],
)
