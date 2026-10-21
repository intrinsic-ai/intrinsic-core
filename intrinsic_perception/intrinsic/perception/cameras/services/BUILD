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

load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("@rules_pkg//:mappings.bzl", "pkg_attributes", "pkg_files", "strip_prefix")
load("@rules_pkg//:pkg.bzl", "pkg_tar")
load("//bazel:container.bzl", "container_image")
load("//bazel:go_macros.bzl", "go_binary")
load("//intrinsic_sdk/intrinsic/assets/services/build_defs:services.bzl", "intrinsic_service")
load("//intrinsic_sdk/intrinsic/httpjson:intrinsic_http_image.bzl", "intrinsic_http_image")
load("//intrinsic_sdk/intrinsic/httpjson/openapi:protoc_gen_openapi.bzl", "protoc_gen_openapi")

# gazelle:ignore

package(default_visibility = [
    "//visibility:public",
])

go_binary(
    name = "camera_service_proxy",
    srcs = ["cameraserviceproxy.go"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/proto:protoio",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_go_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_go_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_go_proto",
        "@intrinsic_apis//intrinsic/resources/proto:runtime_context_go_proto",
        "@org_golang_google_grpc//:grpc",
        "@org_golang_google_grpc//codes:go_default_library",
        "@org_golang_google_grpc//credentials/insecure",
        "@org_golang_google_grpc//encoding:go_default_library",
        "@org_golang_google_grpc//metadata:go_default_library",
        "@org_golang_google_grpc//status:go_default_library",
        "@org_golang_google_protobuf//proto",
    ],
)

pkg_files(
    name = "camera_service_proxy_layer_files",
    srcs = [":camera_service_proxy"],
    attributes = pkg_attributes(mode = "0555"),  # all: Read + Execute
    include_runfiles = True,
    prefix = "/opt/intrinsic",
    strip_prefix = strip_prefix.from_pkg(),
)

pkg_tar(
    name = "camera_service_proxy_layer_tar",
    srcs = [":camera_service_proxy_layer_files"],
    extension = "tar.gz",
)

container_image(
    name = "camera_service_proxy_image",
    base = "@distroless_base",
    entrypoint = ["/opt/intrinsic/camera_service_proxy"],
    tars = [":camera_service_proxy_layer_tar"],
    visibility = [
        "//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:__pkg__",
        "//intrinsic_perception/intrinsic/perception/cameras/services:__pkg__",
    ],
)

protoc_gen_openapi(
    name = "camera_service_openapi",
    protos = [
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_proto",
    ],
    visibility = ["//intrinsic_perception/intrinsic/perception/cameras/services:__pkg__"],
)

intrinsic_http_image(
    name = "camera_service_http_image",
    openapi_yaml = ":camera_service_openapi",
    services = {
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_go_proto": "intrinsic_proto.perception.v1.CameraService",
    },
    visibility = [
        "//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:__pkg__",
        "//intrinsic_perception/intrinsic/perception/cameras/services:__pkg__",
    ],
)

filegroup(
    name = "genicam_gige_vision_camera_default_config_template",
    srcs = ["genicam_gige_vision_camera_default_config.pbtxt.tpl"],
    visibility = ["//intrinsic_perception/intrinsic/perception/cameras/services:__pkg__"],
)

expand_template(
    name = "genicam_gige_vision_camera_default_config",
    out = "genicam_gige_vision_camera_default_config.pbtxt",
    substitutions = {"$DEVICE_ID": ""},
    template = "genicam_gige_vision_camera_default_config_template",
    visibility = [
        "//intrinsic_perception/intrinsic/perception/cameras/hardware_devices:__pkg__",
        "//intrinsic_perception/intrinsic/perception/cameras/services:__pkg__",
    ],
)

intrinsic_service(
    name = "genicam_gige_vision_camera_service",
    default_config = "genicam_gige_vision_camera_default_config.pbtxt",
    images = [
        ":camera_service_http_image.tar",
        ":camera_service_proxy_image.tar",
        "//intrinsic_perception/intrinsic/perception/cameras/services/v1:camera-server-image.tar",
    ],
    manifest = ":genicam_gige_vision_camera_service_manifest.textproto",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_service_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_proto",
    ],
)

filegroup(
    name = "ros_camera_default_config_template",
    srcs = ["ros_camera_default_config.pbtxt.tpl"],
    visibility = ["//intrinsic_perception/intrinsic/perception/cameras/services:__pkg__"],
)

expand_template(
    name = "ros_camera_default_config",
    out = "ros_camera_default_config.pbtxt",
    substitutions = {
        "$DEVICE_ID": "",
        "$DRIVER_TYPE": "",
    },
    template = "ros_camera_default_config_template",
)

intrinsic_service(
    name = "ros_camera_service",
    default_config = "ros_camera_default_config.pbtxt",
    images = [
        ":camera_service_http_image.tar",
        ":camera_service_proxy_image.tar",
        "//intrinsic_perception/intrinsic/perception/cameras/services/v1:ros_camera_service_image.tar",
    ],
    manifest = ":ros_camera_service_manifest.textproto",
    visibility = ["//intrinsic:public_api_users"],
    deps = [
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:dynamic_reconfiguration_proto",
        "@intrinsic_apis//intrinsic/assets/services/proto/v1:service_state_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_config_service_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_service_proto",
    ],
)
