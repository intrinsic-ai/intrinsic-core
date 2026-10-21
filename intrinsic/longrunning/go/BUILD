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
    name = "operations",
    srcs = [
        "filter.go",
        "map.go",
        "operation.go",
        "queue.go",
        "runner.go",
        "server.go",
    ],
    importpath = "intrinsic/longrunning/go/operations",
    visibility = [
        "//intrinsic/assets/artifacts:__pkg__",
        "//intrinsic/assets/deploy:__pkg__",
        "//intrinsic/assets/install:__pkg__",
        "//intrinsic/assets/instances:__pkg__",
        "//intrinsic/kubernetes/workcell_spec:__pkg__",
    ],
    deps = [
        "//intrinsic/util/pagination:pagetoken",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)
