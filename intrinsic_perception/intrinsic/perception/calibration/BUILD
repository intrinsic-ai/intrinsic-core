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
    name = "charuco_pattern_detector",
    srcs = ["charuco_pattern_detector.cc"],
    hdrs = ["charuco_pattern_detector.h"],
    deps = [
        ":charuco_pattern_utils",
        ":pattern_detector_interface",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result_helper",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/proto_conversion:vector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/perception/proto/v1:charuco_pattern_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_result_cc_proto",
        "@opencv//:core",
        "@opencv//:objdetect",
    ],
)

cc_library(
    name = "charuco_pattern_detector_register",
    srcs = ["charuco_pattern_detector_register.cc"],
    deps = [
        ":charuco_pattern_detector",
        ":pattern_detector_interface",
    ],
    alwayslink = True,
)

cc_library(
    name = "charuco_pattern_utils",
    srcs = ["charuco_pattern_utils.cc"],
    hdrs = ["charuco_pattern_utils.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:conversion",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:distortion_params",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:mesh",
        "//intrinsic_perception/intrinsic/perception/core:opencv_wrapper",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/perception/proto/v1:charuco_pattern_cc_proto",
        "@opencv//:core",
        "@opencv//:objdetect",
    ],
)

cc_library(
    name = "intrinsic_calibration_utils",
    srcs = ["intrinsic_calibration_utils.cc"],
    hdrs = ["intrinsic_calibration_utils.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:color_gradient",
        "//intrinsic_perception/intrinsic/perception/core:coordinate",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:distortion_params",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:intrinsic_params",
        "//intrinsic_perception/intrinsic/perception/core:opencv_wrapper",
        "//intrinsic_perception/intrinsic/perception/core:operators",
        "//intrinsic_perception/intrinsic/perception/core:rectangle",
        "//intrinsic_perception/intrinsic/perception/proto_conversion:vector",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf_lite",
        "@intrinsic_apis//intrinsic/perception/proto:vector_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_result_cc_proto",
        "@opencv//:calib3d",
        "@opencv//:core",
        "@opencv//:imgproc",
    ],
)

cc_library(
    name = "camera_to_robot_calibration",
    srcs = ["camera_to_robot_calibration.cc"],
    hdrs = ["camera_to_robot_calibration.h"],
    deps = [
        ":metrics",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:opencv_wrapper",
        "//intrinsic_perception/intrinsic/perception/proto_conversion:eigen_conversions",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/eigenmath:ceres",
        "//intrinsic_sdk/intrinsic/eigenmath:pose3_utils",
        "//intrinsic_sdk/intrinsic/math:almost_equals",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_gitlab_libeigen_eigen//:eigen",
        "@com_google_ceres_solver//:ceres",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_to_robot_calibration_cc_proto",
        "@opencv//:calib3d",
        "@opencv//:core",
    ],
)

cc_library(
    name = "intrinsic_calibration",
    srcs = ["intrinsic_calibration.cc"],
    hdrs = ["intrinsic_calibration.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:dimensions",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:opencv_wrapper",
        "//intrinsic_perception/intrinsic/perception/core:range_tools",
        "//intrinsic_perception/intrinsic/perception/proto_conversion:vector",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_result_cc_proto",
        "@opencv//:calib3d",
        "@opencv//:core",
    ],
)

cc_library(
    name = "pattern_detector_factory",
    srcs = ["pattern_detector_factory.cc"],
    hdrs = ["pattern_detector_factory.h"],
    deps = [
        ":pattern_detector_interface",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_config_cc_proto",
    ],
)

# This target is tested implicitly through the individual pattern detector tests which
# use it to instantiate pattern detectors.
cc_library(
    name = "pattern_detector",
    srcs = ["pattern_detector.cc"],
    hdrs = ["pattern_detector.h"],
    deps = [
        ":pattern_detector_factory",
        ":pattern_detector_interface",
        "//intrinsic/stats:scoped_span",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result_helper",
        "//intrinsic_perception/intrinsic/perception/cameras:sensor_image",
        "//intrinsic_perception/intrinsic/perception/core:config_name_utils",
        "//intrinsic_perception/intrinsic/perception/core:drawing",
        "//intrinsic_perception/intrinsic/perception/core:encoding",
        "//intrinsic_perception/intrinsic/perception/core:image_publisher",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/logging:pattern_detection",
        "//intrinsic_perception/intrinsic/perception/proto_conversion:vector",
        "//intrinsic_perception/intrinsic/perception/proto_conversion/v1:image_buffer",
        "//intrinsic_sdk/intrinsic/platform/pubsub",
        "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
        "//intrinsic_sdk/intrinsic/util/status:ret_check",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/base:nullability",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@com_github_grpc_grpc//:grpc++",
        "@intrinsic_apis//intrinsic/perception/proto/v1:image_buffer_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_result_cc_proto",
    ],
)

# This class is implicitly tested through individual detector tests which build
# on the public Detector and thus the DetectorInterface.
cc_library(
    name = "pattern_detector_interface",
    srcs = ["pattern_detector_interface.cc"],
    hdrs = ["pattern_detector_interface.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/cameras:capture_result",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/status:statusor",
        "@cppregpattern//:registry",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_config_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:pattern_detection_result_cc_proto",
    ],
)

cc_library(
    name = "pose_sampling",
    srcs = ["pose_sampling.cc"],
    hdrs = ["pose_sampling.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "@abseil-cpp//absl/random",
    ],
)

cc_library(
    name = "metrics",
    srcs = ["metrics.cc"],
    hdrs = ["metrics.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:math_utils",
        "//intrinsic_perception/intrinsic/perception/core:pose_utils",
        "//intrinsic_perception/intrinsic/perception/proto_conversion:eigen_conversions",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status:statusor",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_setup_cc_proto",
        "@intrinsic_apis//intrinsic/perception/proto/v1:camera_to_robot_calibration_cc_proto",
    ],
)

cc_library(
    name = "stereo_calibration_cc",
    srcs = ["stereo_calibration.cc"],
    hdrs = ["stereo_calibration.h"],
    features = ["-use_header_modules"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:camera_params",
        "//intrinsic_perception/intrinsic/perception/core:distortion_params",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:opencv_wrapper",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@opencv//:calib3d",
        "@opencv//:core",
    ],
)

cc_library(
    name = "image_quality_check",
    srcs = ["image_quality_check.cc"],
    hdrs = ["image_quality_check.h"],
    deps = [
        "//intrinsic_perception/intrinsic/perception/core:argument_checks",
        "//intrinsic_perception/intrinsic/perception/core:conversion",
        "//intrinsic_perception/intrinsic/perception/core:eigen_types",
        "//intrinsic_perception/intrinsic/perception/core:image",
        "//intrinsic_perception/intrinsic/perception/core:image_traits",
        "//intrinsic_perception/intrinsic/perception/core:opencv_wrapper",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@opencv//:core",
    ],
)
