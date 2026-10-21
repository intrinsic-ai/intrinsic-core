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

load("//bazel:go_macros.bzl", "go_binary", "go_library")

go_library(
    name = "datagen",
    srcs = ["datagen.go"],
    importpath = "intrinsic/assets/data/build_defs/datagen",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic/assets/data:__subpackages__",
        "//intrinsic_sdk/intrinsic/assets:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inbuild:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "//intrinsic_sdk/intrinsic/assets/data:databundle",
        "//intrinsic_sdk/intrinsic/assets/data:datavalidate",
        "//intrinsic_sdk/intrinsic/assets/data:utils",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_manifest_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_binary(
    name = "datagen_main",
    srcs = ["datagen_main.go"],
    importpath = "intrinsic/assets/data/build_defs/datagen_main",
    visibility = [
        "//intrinsic/assets:__subpackages__",
        "//intrinsic_sdk/intrinsic/assets:__subpackages__",
    ],
    deps = [
        ":datagen",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util:flag",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@com_github_golang_glog//:go_default_library",
    ],
)
