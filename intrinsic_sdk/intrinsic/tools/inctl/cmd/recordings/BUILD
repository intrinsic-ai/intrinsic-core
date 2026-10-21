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
    name = "recordings",
    srcs = [
        "create.go",
        "generate.go",
        "get.go",
        "json_util.go",
        "list.go",
        "recordings.go",
        "server.go",
        "visualize.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/recordings/recordings",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/logging/proto:logger_service_go_proto",
        "//intrinsic_sdk/intrinsic/logging/proto:recording_data_categories_go",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:color",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "//intrinsic_sdk/intrinsic/tools/inctl/util/promptutil",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_pborman_uuid//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/vmpool/manager/api/v1:lease_api_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:bag_metadata_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:bag_packager_service_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:recording_data_categories_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:replay_service_go_proto",
        "@intrinsic_apis//intrinsic/logging/proto:replay_service_options_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/timestamppb",
    ],
)
