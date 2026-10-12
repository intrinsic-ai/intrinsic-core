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
    "//intrinsic/tools/inctl/cmd/vm:__subpackages__",
    "//intrinsic/tools/inctl/cmd/vm/pool:__pkg__",
    "//intrinsic_sdk/intrinsic/tools:__subpackages__",
])

go_library(
    name = "pool",
    srcs = [
        "create.go",
        "describe.go",
        "leases.go",
        "list.go",
        "pool.go",
        "state.go",
        "wait.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/vm/pool/pool",
    deps = [
        "//intrinsic_sdk/intrinsic/kubernetes/acl:identity",
        "//intrinsic_sdk/intrinsic/kubernetes/vmpool/service/pkg/defaults",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:agents",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@intrinsic_apis//intrinsic/kubernetes/vmpool/manager/api/v1:lease_api_go_proto",
        "@intrinsic_apis//intrinsic/kubernetes/vmpool/service/api/v1:vmpool_api_go_proto",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//encoding/protojson:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/emptypb",
        "@org_golang_google_protobuf//types/known/fieldmaskpb",
    ] + [],
)
