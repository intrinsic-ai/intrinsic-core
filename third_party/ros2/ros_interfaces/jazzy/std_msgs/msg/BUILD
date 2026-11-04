load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(
    default_applicable_licenses = ["//third_party/ros2/ros_interfaces/jazzy/std_msgs:license"],
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],
)

proto_library(
    name = "color_rgba_proto",
    srcs = ["color_rgba.proto"],
)

py_proto_library(
    name = "color_rgba_py_pb2",
    deps = ["color_rgba_proto"],
)

cc_proto_library(
    name = "color_rgba_cc_proto",
    deps = ["color_rgba_proto"],
)

go_proto_library(
    name = "color_rgba_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/std_msgs/msg/color_rgba_go_proto",
    protos = ["color_rgba_proto"],
)

proto_library(
    name = "header_proto",
    srcs = ["header.proto"],
    deps = ["//third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg:time_proto"],
)

py_proto_library(
    name = "header_py_pb2",
    deps = ["header_proto"],
)

cc_proto_library(
    name = "header_cc_proto",
    deps = ["header_proto"],
)

go_proto_library(
    name = "header_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/std_msgs/msg/header_go_proto",
    protos = ["header_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg:time_go_proto",
    ],
)
