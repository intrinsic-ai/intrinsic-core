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

load("@bazel_skylib//lib:selects.bzl", "selects")
load("//bazel:cc_macros.bzl", "cc_dynamic_library", "cc_library")

package(default_visibility = ["//visibility:public"])

genrule(
    name = "arv_fake_camera_resources",
    srcs = [
        "arv-fake-camera.xml.genicam",
        "arv_fake_camera.gresource.xml",
    ],
    outs = [
        "arv_fake_camera_resources.c",
    ],
    cmd = "$(location @glib//gio:glib_compile_resources) --manual-register --sourcedir=$$(dirname $(location arv_fake_camera.gresource.xml)) --generate-source --target=$@ $(location arv_fake_camera.gresource.xml)",
    tools = ["@glib//gio:glib_compile_resources"],
)

cc_library(
    name = "aravis_utils",
    srcs = [
        "aravis_utils.cc",
        "arv_fake_camera_resources.c",
    ],
    hdrs = [
        "aravis_utils.h",
    ],
    deps = [
        ":aravis_glib_utils",
        ":aravis_wrapper",
        ":gio_wrapper",
        "//intrinsic_control/intrinsic/icon/release:source_location",
        "//intrinsic_perception/intrinsic/perception/cameras:camera_setting_properties",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/cameras:image_source_interface",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:distortion_params",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:opencv_wrapper",
        "//intrinsic_perception/intrinsic/perception/core:range_tools",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util:version",
        "//intrinsic_sdk/intrinsic/util/status:annotate",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/time:deadline_timeout",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@magic_enum",
        "@opencv//:core",
        "@opencv//:imgproc",
    ],
)

cc_library(
    name = "aravis_software_trigger",
    srcs = ["aravis_software_trigger.cc"],
    hdrs = ["aravis_software_trigger.h"],
    deps = [
        ":aravis_utils",
        ":aravis_wrapper",
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "aravis_ptp_utils",
    srcs = ["aravis_ptp_utils.cc"],
    hdrs = ["aravis_ptp_utils.h"],
    deps = [
        ":aravis_software_trigger",
        ":aravis_utils",
        ":aravis_wrapper",
        "//intrinsic_perception/intrinsic/perception/cameras/genicam:feature_names",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "aravis_glib_utils",
    hdrs = ["aravis_glib_utils.h"],
    deps = [
        ":aravis_wrapper",
    ],
)

# If OpenCV is compiled with Qt6 support (i.e., non-hermetically) then
# we must switch aravis to using the system glib. Otherwise, any downstream
# consumer of both the aravis driver and OpenCV will have conflicting glib
# versions, and will result in a SIGABRT.

config_setting(
    name = "use_host_glib_setting",
    flag_values = {"//bazel:use_host_glib": "True"},
)

selects.config_setting_group(
    name = "host_glib_setting",
    match_any = [
        ":use_host_glib_setting",
    ],
    visibility = ["//visibility:public"],
)

cc_dynamic_library(
    name = "glib_wrapper",
    deps = select({
        ":host_glib_setting": ["@sysroot_glvnd_stubs//:system_glib"],
        "//conditions:default": ["@glib//glib"],
    }),
)

cc_dynamic_library(
    name = "gmodule_wrapper",
    dynamic_library_deps = [":glib_wrapper"],
    deps = select({
        ":host_glib_setting": ["@sysroot_glvnd_stubs//:system_gmodule"],
        "//conditions:default": ["@glib//gmodule"],
    }),
)

cc_dynamic_library(
    name = "gobject_wrapper",
    dynamic_library_deps = [":glib_wrapper"],
    deps = select({
        ":host_glib_setting": ["@sysroot_glvnd_stubs//:system_gobject"],
        "//conditions:default": ["@glib//gobject"],
    }),
)

cc_dynamic_library(
    name = "gio_wrapper",
    dynamic_library_deps = [
        ":glib_wrapper",
        ":gmodule_wrapper",
        ":gobject_wrapper",
    ],
    exports_filter = ["@zlib//:z"],
    deps = select({
        ":host_glib_setting": ["@sysroot_glvnd_stubs//:system_gio"],
        "//conditions:default": ["@glib//gio"],
    }),
)

cc_dynamic_library(
    name = "aravis_wrapper",
    dynamic_library_deps = [
        ":gio_wrapper",
        ":glib_wrapper",
        ":gmodule_wrapper",
        ":gobject_wrapper",
    ],
    deps = ["@aravis"],
)
