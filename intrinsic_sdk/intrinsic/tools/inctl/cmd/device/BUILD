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
    name = "device",
    srcs = [
        "config.go",
        "device.go",
        "projectclient.go",
        "register.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/device/device",
    deps = [
        "//incode/cloud/devicemanager/shared",
        "//incode/frontend/cloud/api/v1:clustermanager_api_go_proto",
        "//intrinsic_sdk/intrinsic/tools/inctl/auth",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd:root",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:viperutil",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_viper//:go_default_library",
        "@com_google_cloud_go_longrunning//autogen/longrunningpb",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
    ],
)
