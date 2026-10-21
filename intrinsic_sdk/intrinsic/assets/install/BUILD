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

# For libraries and services covering the installation of assets.  Initial
# design was detailed in go/intrinsic-asset-install.

load("//bazel:python.bzl", "py_library")

py_library(
    name = "installed_assets_client",
    srcs = ["installed_assets_client.py"],
    visibility = [
        "//intrinsic:internal_api_users",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/assets:id_utils_py",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_pb2_grpc",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_py_pb2",
        "//intrinsic_sdk/intrinsic/util/grpc:error_handling",
        "@ai_intrinsic_sdks_pip_deps//grpcio_status",
        "@com_github_grpc_grpc//src/python/grpcio/grpc:grpcio",
        "@com_google_googleapis//google/longrunning:operations_py_proto",
        "@com_google_googleapis//google/rpc:status_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@intrinsic_apis//intrinsic/assets/proto:asset_tag_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:asset_type_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:id_py_pb2",
        "@intrinsic_apis//intrinsic/assets/proto:view_py_pb2",
    ],
)
