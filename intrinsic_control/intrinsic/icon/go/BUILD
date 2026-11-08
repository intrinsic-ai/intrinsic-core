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

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library", "go_test")

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

go_test(
    name = "icon_test",
    srcs = [
        "action_test.go",
        "client_test.go",
        "condition_test.go",
        "reaction_test.go",
        "reactiondata_test.go",
        "serverconfig_test.go",
        "session_test.go",
        "state_variable_path_test.go",
        "stream_test.go",
    ],
    embed = [":icon"],
    importpath = "intrinsic/icon/go/icon_test",
    deps = [
        "//intrinsic_control/intrinsic/icon/actions:streaming_joint_position_go_proto",
        "//intrinsic_sdk/intrinsic/testing:grpctest",
        "@com_github_google_go_cmp//cmp:go_default_library",
        "@com_github_google_go_cmp//cmp/cmpopts:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@intrinsic_apis//intrinsic/icon/actions:point_to_point_move_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:joint_space_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:part_status_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:safety_status_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto:streaming_output_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:condition_types_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:service_go_proto",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:context_go_proto",
        "@org_golang_google_genproto_googleapis_rpc//status",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//testing/protocmp:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/wrapperspb",
    ],
)

go_library(
    name = "intsequence",
    srcs = [
        "intsequence.go",
    ],
    importpath = "intrinsic/icon/go/intsequence",
)

go_test(
    name = "intsequence_test",
    size = "large",
    srcs = [
        "intsequence_test.go",
    ],
    embed = [":intsequence"],
    importpath = "intrinsic/icon/go/intsequence_test",
)

go_library(
    name = "sessionutil",
    srcs = [
        "sessionutil.go",
    ],
    importpath = "intrinsic/icon/go/sessionutil",
    deps = [
        ":icon",
    ],
)

go_test(
    name = "sessionutil_test",
    srcs = [
        "sessionutil_test.go",
    ],
    embed = [":sessionutil"],
    importpath = "intrinsic/icon/go/sessionutil_test",
    deps = [":icon"],
)
