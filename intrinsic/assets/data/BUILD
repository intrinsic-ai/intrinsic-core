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
    name = "dataassetsservice",
    srcs = ["data_assets_service.go"],
    importpath = "intrinsic/assets/data/dataassetsservice",
    visibility = ["//intrinsic/kubernetes/workcell_spec:__pkg__"],
    deps = [
        "//intrinsic/assets:pageutils",
        "//intrinsic/resources/service:resourcetyperuntime",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "//intrinsic_sdk/intrinsic/assets/data:utils",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:referenced_data_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
    ],
)
