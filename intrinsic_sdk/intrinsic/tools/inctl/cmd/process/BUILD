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

package(default_visibility = [
    "//intrinsic/tools:__subpackages__",
    "//intrinsic/tools/inctl:__subpackages__",
    "//intrinsic_sdk/intrinsic/tools:__subpackages__",
])

go_library(
    name = "process",
    srcs = [
        "process.go",
        "process_get.go",
        "process_set.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/process/process",
    deps = [
        "//intrinsic_sdk/intrinsic/assets:idutils",
        "//intrinsic_sdk/intrinsic/assets/platformlevelswitch",
        "//intrinsic_sdk/intrinsic/assets/processes:processbundle",
        "//intrinsic_sdk/intrinsic/assets/proto:installed_assets_go_proto",
        "//intrinsic_sdk/intrinsic/executive/go:behaviortree",
        "//intrinsic_sdk/intrinsic/executive/proto:behavior_tree_go_proto",
        "//intrinsic_sdk/intrinsic/executive/proto:executive_service_go_proto",
        "//intrinsic_sdk/intrinsic/executive/proto:run_metadata_go_proto",
        "//intrinsic_sdk/intrinsic/frontend/solution_service/proto:solution_service_go_proto",
        "//intrinsic_sdk/intrinsic/proto_tools/registry/protoregistryclient",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd:dialerutil",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd:solutionutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "//intrinsic_sdk/intrinsic/util/proto:fieldbehavior",
        "//intrinsic_sdk/intrinsic/util/proto:registryutil",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/assets/proto:view_go_proto",
        "@intrinsic_apis//intrinsic/proto_tools/proto:proto_registry_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//reflect/protodesc:go_default_library",
        "@org_golang_google_protobuf//reflect/protoreflect:go_default_library",
        "@org_golang_google_protobuf//reflect/protoregistry:go_default_library",
        "@org_golang_google_protobuf//types/descriptorpb",
        "@org_golang_google_protobuf//types/known/emptypb",
    ],
)
