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

package(
    default_visibility = ["//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__"],
)

go_library(
    name = "bundle",
    srcs = ["bundle.go"],
    importpath = "intrinsic/tools/inbuild/cmd/data/bundle",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "//intrinsic_sdk/intrinsic/assets/data:utils",
        "//intrinsic_sdk/intrinsic/assets/data/build_defs:datagen",
        "//intrinsic_sdk/intrinsic/util/proto:descriptor",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_struct_go_proto",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
    ],
)

go_library(
    name = "data",
    srcs = ["data.go"],
    importpath = "intrinsic/tools/inbuild/cmd/data",
    deps = [
        ":bundle",
        "@com_github_spf13_cobra//:cobra",
    ],
)
