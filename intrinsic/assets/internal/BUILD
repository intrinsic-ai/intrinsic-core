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
    name = "assetinfointernal",
    srcs = ["asset_info_internal.go"],
    importpath = "intrinsic/assets/internal/assetinfointernal",
    visibility = ["//intrinsic/resources/service:__pkg__"],
    deps = [
        "//intrinsic/assets/dependencies:graph",
        "//intrinsic/assets/dependencies:runtimegraph",
        "//intrinsic/assets/proto/v1alpha1:asset_info_internal_go_proto",
        "//intrinsic/resources/proto:geometric_resource_data_go_proto",
        "//intrinsic/resources/proto:resource_type_runtime_go_proto",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic/skills/internal:skillcomposer",
        "//intrinsic/storage/hot_shared_state/proto:application_service_go_proto",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets/errors:report",
        "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:resource_registry_go_proto",
        "@intrinsic_apis//intrinsic/util/status:extended_status_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
