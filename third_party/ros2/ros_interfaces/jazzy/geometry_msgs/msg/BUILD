load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(
    default_applicable_licenses = ["//third_party/ros2/ros_interfaces/jazzy/geometry_msgs:license"],
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],
)

proto_library(
    name = "point_proto",
    srcs = ["point.proto"],
)

py_proto_library(
    name = "point_py_pb2",
    deps = ["point_proto"],
)

cc_proto_library(
    name = "point_cc_proto",
    deps = ["point_proto"],
)

go_proto_library(
    name = "point_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg/point_go_proto",
    protos = ["point_proto"],
)

proto_library(
    name = "pose_proto",
    srcs = ["pose.proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:point_proto",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:quaternion_proto",
    ],
)

py_proto_library(
    name = "pose_py_pb2",
    deps = ["pose_proto"],
)

cc_proto_library(
    name = "pose_cc_proto",
    deps = ["pose_proto"],
)

go_proto_library(
    name = "pose_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg/pose_go_proto",
    protos = ["pose_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:point_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:quaternion_go_proto",
    ],
)

proto_library(
    name = "quaternion_proto",
    srcs = ["quaternion.proto"],
)

py_proto_library(
    name = "quaternion_py_pb2",
    deps = ["quaternion_proto"],
)

cc_proto_library(
    name = "quaternion_cc_proto",
    deps = ["quaternion_proto"],
)

go_proto_library(
    name = "quaternion_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg/quaternion_go_proto",
    protos = ["quaternion_proto"],
)

proto_library(
    name = "twist_proto",
    srcs = ["twist.proto"],
    deps = ["//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:vector3_proto"],
)

py_proto_library(
    name = "twist_py_pb2",
    deps = ["twist_proto"],
)

cc_proto_library(
    name = "twist_cc_proto",
    deps = ["twist_proto"],
)

go_proto_library(
    name = "twist_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg/twist_go_proto",
    protos = ["twist_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:vector3_go_proto",
    ],
)

proto_library(
    name = "vector3_proto",
    srcs = ["vector3.proto"],
)

py_proto_library(
    name = "vector3_py_pb2",
    deps = ["vector3_proto"],
)

cc_proto_library(
    name = "vector3_cc_proto",
    deps = ["vector3_proto"],
)

go_proto_library(
    name = "vector3_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg/vector3_go_proto",
    protos = ["vector3_proto"],
)
