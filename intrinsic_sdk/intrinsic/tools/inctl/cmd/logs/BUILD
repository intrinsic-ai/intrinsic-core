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

package(default_visibility = ["//visibility:public"])

go_library(
    name = "logs",
    srcs = [
        "logs.go",
        "logs_cp.go",
        "logs_pull.go",
        "logs_sync.go",
        "processor.go",
        "//intrinsic/tools/inctl/cmd/logs:logs_historian.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/logs/logs",
    deps = [
        "//intrinsic/simulation/gazebo/proto:introspection_go_proto",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/logging/proto:log_dispatcher_service_go_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_go_proto",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd:dialerutil",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd:solutionutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:color",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_gdamore_tcell_v2//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_rivo_tview//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@intrinsic_apis//intrinsic/hardware/gripper:gripper_go_proto",
        "@intrinsic_apis//intrinsic/icon/equipment:icon_equipment_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:blob_go_proto",
        "@intrinsic_apis//intrinsic/logging/textlogfetcher/proto/v1:textlogfetcher_go_proto",
        "@intrinsic_apis//intrinsic/skills/proto:skill_manifest_go_proto",
        "@io_opencensus_go//plugin/ocgrpc:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
        "@org_golang_x_sync//errgroup:go_default_library",
    ],
)
