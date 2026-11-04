load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(
    default_applicable_licenses = ["//third_party/ros2/ros_interfaces/jazzy/sensor_msgs:license"],
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],
)

proto_library(
    name = "camera_info_proto",
    srcs = ["camera_info.proto"],
    deps = [
        ":region_of_interest_proto",
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_proto",
    ],
)

py_proto_library(
    name = "camera_info_py_pb2",
    deps = ["camera_info_proto"],
)

cc_proto_library(
    name = "camera_info_cc_proto",
    deps = ["camera_info_proto"],
)

go_proto_library(
    name = "camera_info_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg/camera_info_go_proto",
    protos = ["camera_info_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:region_of_interest_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_go_proto",
    ],
)

proto_library(
    name = "compressed_image_proto",
    srcs = ["compressed_image.proto"],
    deps = ["//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_proto"],
)

py_proto_library(
    name = "compressed_image_py_pb2",
    deps = ["compressed_image_proto"],
)

cc_proto_library(
    name = "compressed_image_cc_proto",
    deps = ["compressed_image_proto"],
)

go_proto_library(
    name = "compressed_image_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg/compressed_image_go_proto",
    protos = ["compressed_image_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_go_proto",
    ],
)

proto_library(
    name = "image_proto",
    srcs = ["image.proto"],
    deps = ["//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_proto"],
)

py_proto_library(
    name = "image_py_pb2",
    deps = ["image_proto"],
)

cc_proto_library(
    name = "image_cc_proto",
    deps = ["image_proto"],
)

go_proto_library(
    name = "image_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg/image_go_proto",
    protos = ["image_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_go_proto",
    ],
)

proto_library(
    name = "joint_state_proto",
    srcs = ["joint_state.proto"],
    deps = ["//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_proto"],
)

py_proto_library(
    name = "joint_state_py_pb2",
    deps = ["joint_state_proto"],
)

cc_proto_library(
    name = "joint_state_cc_proto",
    deps = ["joint_state_proto"],
)

go_proto_library(
    name = "joint_state_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg/joint_state_go_proto",
    protos = ["joint_state_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_go_proto",
    ],
)

proto_library(
    name = "region_of_interest_proto",
    srcs = ["region_of_interest.proto"],
)

py_proto_library(
    name = "region_of_interest_py_pb2",
    deps = ["region_of_interest_proto"],
)

cc_proto_library(
    name = "region_of_interest_cc_proto",
    deps = ["region_of_interest_proto"],
)

go_proto_library(
    name = "region_of_interest_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg/region_of_interest_go_proto",
    protos = ["region_of_interest_proto"],
)
