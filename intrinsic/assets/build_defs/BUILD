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

package(default_visibility = [
    "//visibility:public",
])

go_library(
    name = "cascopyprocessor",
    srcs = ["cascopyprocessor.go"],
    importpath = "intrinsic/assets/build_defs/cascopyprocessor",
    deps = [
        "//intrinsic/storage/content_addressable_storage/pkg/crossproject",
        "//intrinsic/storage/content_addressable_storage/pkg/primordial",
        "//intrinsic/tools/inctl/util:gcpauth",
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)

go_binary(
    name = "solution_update",
    srcs = ["solution_update.go"],
    importpath = "intrinsic/assets/build_defs/solution_update",
    deps = [
        ":cascopyprocessor",
        "//intrinsic/assets/conversion:solutionconversion",
        "//intrinsic/config/proto:application_go_proto",
        "//intrinsic/config/proto:common_go_proto",
        "//intrinsic/storage/content_addressable_storage/pkg/filetocas",
        "//intrinsic/tools/inctl/cmd:apputil",
        "//intrinsic/tools/inctl/util:casgeometryuploader",
        "//intrinsic/tools/inctl/util:gcpauth",
        "//intrinsic/tools/inctl/util:introspection",
        "//intrinsic/tools/inctl/util:releaseutil",
        "//intrinsic/tools/inctl/util:traceutil",
        "//intrinsic/util/go:build",
        "//intrinsic_sdk/intrinsic/assets:bundle",
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/assets:imagetransfer",
        "//intrinsic_sdk/intrinsic/assets:referenceddata",
        "//intrinsic_sdk/intrinsic/assets:throttle",
        "//intrinsic_sdk/intrinsic/assets/build_defs:asset_go_proto",
        "//intrinsic_sdk/intrinsic/assets/build_defs:assetprocessor",
        "//intrinsic_sdk/intrinsic/assets/platformlevelswitch",
        "//intrinsic_sdk/intrinsic/assets/proto/v1:solution_deployment_go_proto",
        "//intrinsic_sdk/intrinsic/assets/scene_objects:gzfprocessor",
        "//intrinsic_sdk/intrinsic/assets/services:bundleimages",
        "//intrinsic_sdk/intrinsic/config:operationmode",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_go_proto",
        "//intrinsic_sdk/intrinsic/kubernetes/workcell_spec/proto:deploy_go_proto",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd/directupload",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:color",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "//intrinsic_sdk/intrinsic/util/status:extstatus",
        "@com_github_cenkalti_backoff_v4//:go_default_library",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto/v1:asset_artifacts_go_proto",
        "@intrinsic_apis//intrinsic/config/proto:operation_mode_go_proto",
        "@intrinsic_apis//intrinsic/frontend/solution_service/proto:status_go_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
