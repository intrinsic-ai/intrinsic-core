load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")
load("//bazel:protos.bzl", "proto_library")
load("//intrinsic_sdk/bazel:go_macros.bzl", "go_proto_library")

package(
    default_applicable_licenses = ["//third_party/ros2/ros_interfaces/jazzy/visualization_msgs:license"],
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],
)

proto_library(
    name = "marker_proto",
    srcs = ["marker.proto"],
    deps = [
        ":mesh_file_proto",
        ":uv_coordinate_proto",
        "//third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg:duration_proto",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:point_proto",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:pose_proto",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:vector3_proto",
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:compressed_image_proto",
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:color_rgba_proto",
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_proto",
    ],
)

py_proto_library(
    name = "marker_py_pb2",
    deps = ["marker_proto"],
)

cc_proto_library(
    name = "marker_cc_proto",
    deps = ["marker_proto"],
)

go_proto_library(
    name = "marker_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg/marker_go_proto",
    protos = ["marker_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/builtin_interfaces/msg:duration_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:point_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:pose_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/geometry_msgs/msg:vector3_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg:compressed_image_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:color_rgba_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/std_msgs/msg:header_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg:mesh_file_go_proto",
        "//third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg:uv_coordinate_go_proto",
    ],
)

proto_library(
    name = "marker_array_proto",
    srcs = ["marker_array.proto"],
    deps = [":marker_proto"],
)

py_proto_library(
    name = "marker_array_py_pb2",
    deps = ["marker_array_proto"],
)

cc_proto_library(
    name = "marker_array_cc_proto",
    deps = ["marker_array_proto"],
)

go_proto_library(
    name = "marker_array_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg/marker_array_go_proto",
    protos = ["marker_array_proto"],
    deps = [
        "//third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg:marker_go_proto",
    ],
)

proto_library(
    name = "mesh_file_proto",
    srcs = ["mesh_file.proto"],
)

py_proto_library(
    name = "mesh_file_py_pb2",
    deps = ["mesh_file_proto"],
)

cc_proto_library(
    name = "mesh_file_cc_proto",
    deps = ["mesh_file_proto"],
)

go_proto_library(
    name = "mesh_file_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg/mesh_file_go_proto",
    protos = ["mesh_file_proto"],
)

proto_library(
    name = "uv_coordinate_proto",
    srcs = ["uv_coordinate.proto"],
)

py_proto_library(
    name = "uv_coordinate_py_pb2",
    deps = ["uv_coordinate_proto"],
)

cc_proto_library(
    name = "uv_coordinate_cc_proto",
    deps = ["uv_coordinate_proto"],
)

go_proto_library(
    name = "uv_coordinate_go_proto",
    importpath = "third_party/ros2/ros_interfaces/jazzy/visualization_msgs/msg/uv_coordinate_go_proto",
    protos = ["uv_coordinate_proto"],
)
