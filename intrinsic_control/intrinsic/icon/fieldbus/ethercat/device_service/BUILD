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

load("//intrinsic_sdk/bazel:go_macros.bzl", "go_library", "go_test")

package(default_visibility = [
    "//visibility:public",
])

go_library(
    name = "deviceservice",
    srcs = [
        "device_service.go",
        "esi_handler.go",
    ],
    importpath = "intrinsic/icon/fieldbus/ethercat/device_service/deviceservice",
    deps = [
        "//intrinsic_sdk/intrinsic/assets/dependencies:utils",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/icon/fieldbus/ethercat/device_service/v1:device_service_config_go_proto",
        "@intrinsic_apis//intrinsic/icon/fieldbus/ethercat/device_service/v1:device_service_go_proto",
        "@intrinsic_apis//intrinsic/icon/fieldbus/ethercat/device_service/v1:esi_go_proto",
        "@intrinsic_apis//intrinsic/icon/hal/lib/service_variable_value/v1:service_variable_config_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

go_test(
    name = "deviceservice_test",
    srcs = ["device_service_test.go"],
    embed = [":deviceservice"],
    importpath = "intrinsic/icon/fieldbus/ethercat/device_service/deviceservice_test",
    deps = [
        "//intrinsic_sdk/intrinsic/assets/data:fakedataassets",
        "//intrinsic_sdk/intrinsic/testing:grpctest",
        "@com_github_google_go_cmp//cmp:go_default_library",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_asset_go_proto",
        "@intrinsic_apis//intrinsic/assets/data/proto/v1:data_assets_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:id_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto:metadata_go_proto",
        "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_go_proto",
        "@intrinsic_apis//intrinsic/icon/fieldbus/ethercat/device_service/v1:device_service_config_go_proto",
        "@intrinsic_apis//intrinsic/icon/fieldbus/ethercat/device_service/v1:device_service_go_proto",
        "@intrinsic_apis//intrinsic/icon/fieldbus/ethercat/device_service/v1:esi_go_proto",
        "@intrinsic_apis//intrinsic/icon/hal/lib/service_variable_value/v1:service_variable_config_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/local:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//testing/protocmp:go_default_library",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)
