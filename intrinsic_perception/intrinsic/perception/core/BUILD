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

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = [
    "//visibility:public",
])

cc_library(
    name = "argument_checks",
    hdrs = ["argument_checks.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "axis_aligned_bounding_box",
    srcs = ["axis_aligned_bounding_box.cc"],
    hdrs = ["axis_aligned_bounding_box.h"],
    deps = [
        ":coordinate",
        ":eigen_types",
        ":intrinsic_params",
        ":rectangle",
    ],
)

cc_library(
    name = "camera_params",
    srcs = ["camera_params.cc"],
    hdrs = ["camera_params.h"],
    deps = [
        ":dimensions",
        ":distortion_params",
        ":intrinsic_params",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
    ],
)

cc_library(
    name = "color_gradient",
    srcs = ["color_gradient.cc"],
    hdrs = ["color_gradient.h"],
    deps = [
        ":eigen_types",
        ":image_traits",
        "@abseil-cpp//absl/log",
        "@boost.container",
    ],
)

cc_library(
    name = "compute_normals_least_sqr",
    srcs = ["compute_normals_least_sqr.cc"],
    hdrs = ["compute_normals_least_sqr.h"],
    copts = [
        "-mavx",
        "-msse2",
        "-mssse3",
        "-msse3",
        "-msse4.1",
        "-msse4.2",
    ],
    deps = [
        ":image",
        ":image_traits",
        ":intrinsic_params",
        ":parallel",
        "@abseil-cpp//absl/log:check",
    ],
)

cc_library(
    name = "config_name_utils",
    srcs = ["config_name_utils.cc"],
    hdrs = ["config_name_utils.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_googlesource_code_re2//:re2",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "conversion",
    srcs = ["conversion.cc"],
    hdrs = ["conversion.h"],
    deps = [
        ":image_traits",
        ":operators",
        "//intrinsic_sdk/intrinsic/eigenmath",
    ],
)

cc_library(
    name = "coordinate",
    hdrs = ["coordinate.h"],
    deps = [
        ":eigen_types",
        "@abseil-cpp//absl/log:absl_check",
    ],
)

cc_library(
    name = "core_io",
    srcs = ["core_io.cc"],
    hdrs = [
        "core_io.h",
    ],
    deps = [
        ":camera_params",
        ":encoding",
        ":image",
        ":image_traits",
        ":io_conversions",
        ":opencv_wrapper",
        ":operators",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_sdk/intrinsic/util:cloud",
        "//intrinsic_sdk/intrinsic/util/proto:error_collector",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//src/google/protobuf/io:tokenizer",
        "@google_cloud_cpp//:storage",
        "@opencv//:core",
        "@opencv//:imgcodecs",
        "@or_tools//ortools/base:file",
        "@or_tools//ortools/base:path",
    ],
)

cc_library(
    name = "dimensions",
    hdrs = ["dimensions.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "distortion_params",
    srcs = ["distortion_params.cc"],
    hdrs = ["distortion_params.h"],
    deps = [
        ":math_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "drawing",
    hdrs = [
        "drawing.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":eigen_types",
        ":image",
        ":image_traits",
        ":opencv_wrapper",
        ":rectangle",
        "@abseil-cpp//absl/types:span",
        "@opencv//:core",
        "@opencv//:imgproc",
    ],
)

cc_library(
    name = "eigen_types",
    hdrs = ["eigen_types.h"],
    deps = ["@com_gitlab_libeigen_eigen//:eigen"],
)

cc_library(
    name = "encoding",
    srcs = ["encoding.cc"],
    hdrs = ["encoding.h"],
    deps = [
        ":image",
        ":image_traits",
        ":io_conversions",
        ":opencv_wrapper",
        "@abseil-cpp//absl/log:check",
        "@opencv//:core",
        "@opencv//:imgcodecs",
        "@opencv//:imgproc",
    ],
)

cc_library(
    name = "hdr_utils",
    srcs = ["hdr_utils.cc"],
    hdrs = ["hdr_utils.h"],
    deps = [
        ":image",
        ":image_traits",
        ":opencv_wrapper",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_hdr:hdr_mertens",
        "//intrinsic_perception/intrinsic/perception/gpu/cuda_utils",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@magic_enum",
        "@opencv//:core",
        "@opencv//:photo",
    ],
)

cc_library(
    name = "image",
    hdrs = ["image.h"],
    deps = [
        ":coordinate",
        ":dimensions",
        ":image_traits",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:cord",
    ],
)

cc_library(
    name = "image_file_reference",
    hdrs = ["image_file_reference.h"],
    deps = [
        ":pixel_type",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "image_publisher",
    srcs = ["image_publisher.cc"],
    hdrs = ["image_publisher.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_cc_proto",
    ],
)

cc_library(
    name = "image_traits",
    hdrs = ["image_traits.h"],
    deps = [":eigen_types"],
)

cc_library(
    name = "intrinsic_params",
    srcs = ["intrinsic_params.cc"],
    hdrs = ["intrinsic_params.h"],
    deps = [
        ":dimensions",
        ":eigen_types",
        ":rectangle",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "io_conversions",
    srcs = ["io_conversions.cc"],
    hdrs = ["io_conversions.h"],
    deps = [
        ":image",
        ":image_traits",
        ":operators",
        "@abseil-cpp//absl/log",
    ],
)

cc_library(
    name = "math_utils",
    hdrs = ["math_utils.h"],
    deps = [
        ":argument_checks",
        ":eigen_types",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "mesh",
    srcs = ["mesh.cc"],
    hdrs = ["mesh.h"],
    deps = [
        ":axis_aligned_bounding_box",
        ":coordinate",
        ":eigen_types",
        ":image",
        ":image_traits",
        ":intrinsic_params",
        ":rectangle",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "opencv_wrapper",
    srcs = ["opencv_wrapper.cc"],
    hdrs = ["opencv_wrapper.h"],
    # OpenCV might throw exceptions that we need to catch to convert to absl::Status.
    copts = ["-fexceptions"],
    # We need this feature because we're handling exceptions in this library.
    features = ["-use_header_modules"],
    deps = [
        ":dimensions",
        ":distortion_params",
        ":eigen_types",
        ":image",
        ":image_traits",
        ":intrinsic_params",
        ":rectangle",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@opencv//:core",
    ],
)

cc_library(
    name = "operators",
    hdrs = ["operators.h"],
    deps = [
        ":coordinate",
        ":dimensions",
        ":eigen_types",
        ":image",
        ":image_traits",
        ":opencv_wrapper",
        ":post_processing",
        ":rectangle",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:str_format",
        "@opencv//:core",
        "@opencv//:imgproc",
    ],
)

cc_library(
    name = "parallel",
    hdrs = ["parallel.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
        "@onetbb//:tbb",
    ],
)

cc_library(
    name = "pixel_type",
    hdrs = ["pixel_type.h"],
    deps = ["@magic_enum"],
)

cc_library(
    name = "pose_utils",
    srcs = ["pose_utils.cc"],
    hdrs = ["pose_utils.h"],
    deps = [
        ":eigen_types",
        ":math_utils",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_builder",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/random:distributions",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
    ],
)

cc_library(
    name = "post_processing",
    srcs = ["post_processing.cc"],
    hdrs = ["post_processing.h"],
    deps = [":rectangle"],
)

cc_library(
    name = "range_tools",
    hdrs = ["range_tools.h"],
    deps = ["@abseil-cpp//absl/container:flat_hash_set"],
)

cc_library(
    name = "rectangle",
    hdrs = ["rectangle.h"],
    deps = [
        ":coordinate",
        ":dimensions",
        ":eigen_types",
        "@abseil-cpp//absl/log:absl_check",
    ],
)

cc_library(
    name = "single_thread_executor",
    srcs = ["single_thread_executor.cc"],
    hdrs = ["single_thread_executor.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/util/thread",
        "//intrinsic_sdk/intrinsic/util/thread:concurrent_queue",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "undistortion",
    srcs = [
        "undistortion.cc",
    ],
    hdrs = [
        "undistortion.h",
    ],
    deps = [
        ":camera_params",
        ":distortion_params",
        ":image",
        ":image_traits",
        ":intrinsic_params",
        ":opencv_wrapper",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:statusor",
        "@opencv//:calib3d",
        "@opencv//:core",
        "@opencv//:imgproc",
    ],
)
