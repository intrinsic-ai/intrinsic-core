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

# Perception capabilities that are exposed to clients of Intrinsic perception.

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "pose_estimator_id_utils",
    srcs = ["pose_estimator_id_utils.cc"],
    hdrs = ["pose_estimator_id_utils.h"],
    deps = [
        "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_cc_proto",
    ],
)

cc_library(
    name = "asset_utils",
    srcs = ["asset_utils.cc"],
    hdrs = ["asset_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros_grpc",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_cc_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_cc_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:perception_model_cc_proto",
    ],
)
