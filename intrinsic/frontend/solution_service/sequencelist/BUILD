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
    name = "find",
    srcs = ["find.go"],
    importpath = "intrinsic/frontend/solution_service/sequencelist/find",
    deps = [
        "//intrinsic_sdk/intrinsic/executive/go:behaviortree",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
    ],
)

go_library(
    name = "sequencelist",
    srcs = ["sequencelist.go"],
    importpath = "intrinsic/frontend/solution_service/sequencelist",
    visibility = ["//intrinsic/frontend/solution_service:__subpackages__"],
    deps = [
        ":find",
        "//intrinsic_sdk/intrinsic/executive/go:behaviortree",
        "//intrinsic_sdk/intrinsic/executive/proto:run_metadata_go_proto",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:sequence_list_go_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:log_item_go_proto",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/executive/proto:behavior_tree_go_proto",
        "@intrinsic_apis//intrinsic/executive/proto:log_items_go_proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)
