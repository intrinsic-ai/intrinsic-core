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

# gazelle:prefix intrinsic/longrunning/go/proxy

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library")

go_library(
    name = "proxy",
    srcs = [
        "set.go",
        "transient.go",
    ],
    importpath = "intrinsic/longrunning/go/proxy",
    visibility = ["//intrinsic_runtime/intrinsic/longrunning/service:__pkg__"],
    deps = [
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@org_golang_google_grpc//:grpc",
        "@org_golang_google_grpc//codes",
        "@org_golang_google_grpc//status",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)
