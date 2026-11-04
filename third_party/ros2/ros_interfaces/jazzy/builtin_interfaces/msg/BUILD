load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(
    default_applicable_licenses = ["//third_party/ros2/ros_interfaces/jazzy/builtin_interfaces:license"],
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],
)

proto_library(
    name = "duration_proto",
    srcs = ["duration.proto"],
)

py_proto_library(
    name = "duration_py_pb2",
    deps = ["duration_proto"],
)

cc_proto_library(
    name = "duration_cc_proto",
    deps = ["duration_proto"],
)

go_proto_library(
    name = "duration_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg/duration_go_proto",
    protos = ["duration_proto"],
)

proto_library(
    name = "time_proto",
    srcs = ["time.proto"],
)

py_proto_library(
    name = "time_py_pb2",
    deps = ["time_proto"],
)

cc_proto_library(
    name = "time_cc_proto",
    deps = ["time_proto"],
)

go_proto_library(
    name = "time_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg/time_go_proto",
    protos = ["time_proto"],
)
