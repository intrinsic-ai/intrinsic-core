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

load("//bazel:go_macros.bzl", "go_binary")

package(
    default_visibility = ["//visibility:private"],
)

go_binary(
    name = "hal_manifest",
    srcs = ["hal_manifest.go"],
    embedsrcs = ["hal_service_manifest.textproto.tmpl"],
    importpath = "intrinsic/icon/hal/bzl/hal_manifest",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/util:flag",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/proto:service_manifest_go_proto",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
    ],
)

go_binary(
    name = "wrap_pbtxt",
    srcs = ["wrap_pbtxt.go"],
    importpath = "intrinsic/icon/hal/bzl/wrap_pbtxt",
    visibility = ["//intrinsic:intrinsic_internal"],
    deps = [
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "@com_github_golang_glog//:go_default_library",
    ],
)
