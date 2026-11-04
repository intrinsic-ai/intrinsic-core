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

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

go_library(
    name = "icon",
    srcs = [
        "action.go",
        "client.go",
        "condition.go",
        "event.go",
        "reaction.go",
        "reactiondata.go",
        "serverconfig.go",
        "session.go",
        "state_variable_path.go",
        "stream.go",
    ],
    importpath = "intrinsic/icon/go/icon",
    deps = [
        ":intsequence",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/icon/proto/v1:condition_types_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)

go_library(
    name = "intsequence",
    srcs = [
        "intsequence.go",
    ],
    importpath = "intrinsic/icon/go/intsequence",
)
