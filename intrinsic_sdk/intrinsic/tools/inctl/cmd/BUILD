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
    name = "gpio",
    srcs = [
        "gpio.go",
        "gpio_list_signals.go",
        "gpio_read_signals.go",
        "gpio_write_signal.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/gpio",
    deps = [
        ":root",
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:gpio_service_go_proto",
        "@intrinsic_apis//intrinsic/hardware/gpio/v1:signal_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
    ],
)

go_library(
    name = "icon",
    srcs = [
        "icon.go",
        "icon_clear_faults.go",
        "icon_config.go",
        "icon_disable.go",
        "icon_enable.go",
        "icon_list_actions.go",
        "icon_list_parts.go",
        "icon_status.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/icon",
    deps = [
        ":root",
        "//intrinsic_control/intrinsic/icon/go:icon",
        "//intrinsic_sdk/intrinsic/assets:clientutils",
        "//intrinsic_sdk/intrinsic/assets:cmdutils",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:cobrautil",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@intrinsic_apis//intrinsic/icon/proto/v1:types_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_protobuf//encoding/prototext:go_default_library",
    ],
)

go_library(
    name = "root",
    srcs = [
        "full_help.go",
        "root.go",
    ],
    importpath = "intrinsic/tools/inctl/cmd/root",
    deps = [
        "//intrinsic/tools/inctl/util:traceutil",
        "//intrinsic_sdk/intrinsic/production:intrinsic",
        "//intrinsic_sdk/intrinsic/skills/tools/skill/cmd:dialerutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:agents",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:orgutil",
        "//intrinsic_sdk/intrinsic/tools/inctl/util:printer",
        "@com_github_golang_glog//:go_default_library",
        "@com_github_pkg_errors//:go_default_library",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_google_cloud_go_compute_metadata//:metadata",
        "@io_k8s_apimachinery//pkg/api/errors:go_default_library",
        "@io_opencensus_go//trace:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_x_exp//slices",
    ],
)

go_library(
    name = "markdown",
    srcs = ["markdown.go"],
    importpath = "intrinsic/tools/inctl/cmd/markdown",
    deps = [
        ":root",
        "//intrinsic_sdk/intrinsic/util/archive:tartooling",
        "@com_github_google_safearchive//tar",
        "@com_github_spf13_cobra//:go_default_library",
        "@com_github_spf13_cobra//doc:go_default_library",
    ],
)
