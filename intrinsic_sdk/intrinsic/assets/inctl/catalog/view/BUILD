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
    name = "view",
    srcs = ["view.go"],
    importpath = "intrinsic/assets/inctl/catalog/view",
    visibility = [
        "//intrinsic:internal_api_users",
        "//intrinsic_sdk/intrinsic/assets/inctl:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:typeutils",
        "//intrinsic_sdk/intrinsic/assets/catalog/proto/v1:asset_catalog_go_proto",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@org_golang_google_protobuf//encoding/protojson",
    ],
)
