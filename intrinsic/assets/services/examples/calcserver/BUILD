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

load("//bazel:container.bzl", "container_image")
load("//bazel:go_macros.bzl", "go_binary", "go_library")
load("//bazel:python.bzl", "py_test")
load("//intrinsic/apps/testing/e2e:app_e2e_test.bzl", "app_e2e_test")
load("//intrinsic/assets/build_defs:solution.bzl", "intrinsic_solution")
load("//intrinsic_sdk/intrinsic/assets/build_defs:asset.bzl", "intrinsic_asset_instance")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")

package(default_visibility = [
    "//visibility:public",
])

alias(
    name = "calculator_service",
    actual = "//intrinsic_sdk/intrinsic/assets/services/examples/calcserver:calculator_service",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

go_library(
    name = "calcserver",
    srcs = ["calcserver.go"],
    importpath = "intrinsic/assets/services/examples/calcserver/calcserver",
    deps = [
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_go_proto",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_binary(
    name = "calcserver_main_go",
    srcs = ["calcserver_main.go"],
    importpath = "intrinsic/assets/services/examples/calcserver/calcserver_main_go",
    deps = [
        ":calcserver",
        "//intrinsic/kubernetes:intrinsic",
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils",
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//reflection:go_default_library",
    ],
)

container_image(
    name = "calc_server_go_image",
    base = "//intrinsic/kubernetes:base-image-oci",
    cmd = [
        "intrinsic/assets/services/examples/calcserver/calcserver_main_go",
    ],
    data_path = "/",
    files = [
        ":calcserver_main_go",
    ],
)

intrinsic_service(
    name = "calculator_go",
    default_config = "//intrinsic_sdk/intrinsic/assets/services/examples/calcserver:calc_default_config.textproto",
    images = [
        ":calc_server_go_image.tar",
    ],
    manifest = "calc_manifest_go.textproto",
    deps = [
        "@intrinsic_apis//intrinsic/assets/services/examples/calcserver:calc_server_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_proto",
    ],
)

alias(
    name = "calculator_py",
    actual = "//intrinsic_sdk/intrinsic/assets/services/examples/calcserver:calculator_py",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

intrinsic_asset_instance(
    name = "my_cc_calculator",
    asset = "ai.intrinsic.calculator_service",
)

intrinsic_asset_instance(
    name = "my_go_calculator",
    asset = "ai.intrinsic.calculator_go",
)

intrinsic_asset_instance(
    name = "my_py_calculator",
    asset = "ai.intrinsic.calculator_py",
)

intrinsic_solution(
    name = "e2e_calculator_application",
    assets = [
        ":calculator_service",
        ":calculator_go",
        ":calculator_py",
        "//intrinsic/skills/examples:adder_skill",
    ],
    instances = [
        ":my_cc_calculator",
        ":my_go_calculator",
        ":my_py_calculator",
    ],
)

py_test(
    name = "e2e_calculator_test_py",
    srcs = ["e2e_calculator_test.py"],
    main = "e2e_calculator_test.py",
    tags = ["manual"],
    deps = [
        "//intrinsic_sdk/intrinsic/solutions:deployments",
    ],
)

app_e2e_test(
    name = "e2e_calculator_test",
    application = ":e2e_calculator_application",
    binary = ":e2e_calculator_test_py",
    visibility = ["//intrinsic/testing:__subpackages__"],
)
