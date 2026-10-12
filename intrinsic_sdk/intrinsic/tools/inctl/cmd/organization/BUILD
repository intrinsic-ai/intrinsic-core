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
    name = "organization",
    srcs = [
        "connect.go",
        "invitations.go",
        "join.go",
        "list.go",
        "organization.go",
        "organizations.go",
        "rolebindings.go",
        "roles.go",
        "users.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/organization/organization",
    visibility = [
        "//intrinsic/tools:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools:__subpackages__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/config:environments",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:agents",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:viperutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util/accounts",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@intrinsic_apis//intrinsic/kubernetes/accounts/service/api/accesscontrol/v1:accesscontrol_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/accounts/service/api/invitations/v1:invitations_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/accounts/service/api/resourcemanager/v1:resourcemanager_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_x_sync//errgroup:go_default_library",
    ],
)
