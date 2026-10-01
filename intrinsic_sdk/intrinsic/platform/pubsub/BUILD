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

# Home of pubsub, a non-real-time Publish-Subscribe messaging
# for robotics systems.

load("//bazel:cc_macros.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "zenoh_pubsub_data",
    srcs = ["zenoh_pubsub_data.cc"],
    hdrs = ["zenoh_pubsub_data.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_config",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
    ],
)

# PubSub API library. The user-facing API providing access to
# publish-subscribe system.
cc_library(
    name = "pubsub",
    srcs = [
        "zenoh_pubsub.cc",
    ],
    hdrs = [
        "pubsub.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],
    deps = [
        ":kvstore",
        ":liveliness_query",
        ":liveliness_subscription",
        ":publisher",
        ":pubsub_callbacks",
        ":queryable",
        ":subscription",
        ":topic_config",
        ":zenoh_publisher_data",
        ":zenoh_pubsub_data",
        ":zenoh_subscription_data",
        "//intrinsic/util/proto:type_url",
        "//intrinsic/util/status:status_conversion_rpc",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:liveliness_get_context",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@com_google_googleapis//google/rpc:status_cc_proto",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/platform/pubsub/adapters:pubsub_cc_proto",
    ],
)

cc_library(
    name = "liveliness_query",
    hdrs = ["liveliness_query.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:liveliness_get_context",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "pubsub_ros",
    srcs = ["pubsub_ros.cc"],
    hdrs = ["pubsub_ros.h"],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],
    deps = [
        ":pubsub",
        "//intrinsic_sdk/intrinsic/middleware/zenoh:imw_zenoh",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@ros2_rclcpp//:rclcpp",
    ],
)

# PubSub API library. The user-facing API providing access to
# publish-subscribe system.
cc_library(
    name = "kvstore",
    srcs = ["kvstore.cc"],
    hdrs = ["kvstore.h"],
    deps = [
        ":pubsub_callbacks",
        ":subscription",
        ":topic_config",
        ":zenoh_subscription_data",
        "//intrinsic/stats:scoped_span",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_helpers",
        "//intrinsic_sdk/intrinsic/util/time:deadline_timeout",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/platform/common/proto:workcell_info_cc_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub/admin_set_grpc/v1:admin_set_cc_grpc",
        "@intrinsic_apis//intrinsic/platform/pubsub/admin_set_grpc/v1:admin_set_cc_proto",
    ],
)

cc_library(
    name = "zenoh_publisher_data",
    hdrs = ["zenoh_publisher_data.h"],
)

cc_library(
    name = "publisher",
    # We need a different publisher implementation also when we move to a
    # different pubsub implementation
    srcs = [
        "zenoh_publisher.cc",
    ],
    hdrs = [
        "publisher.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],
    deps = [
        ":publisher_stats",
        ":zenoh_publisher_data",
        "//intrinsic/util:proto_time",
        "//intrinsic/util/status:ret_check",
        "//intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/platform/pubsub/adapters:pubsub_cc_proto",
        "@io_opencensus_cpp//opencensus/stats",
    ],
)

cc_library(
    name = "queryable",
    srcs =
        ["zenoh_queryable.cc"],
    hdrs = [
        "queryable.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/platform/pubsub/adapters:pubsub_cc_proto",
    ],
)

cc_library(
    name = "zenoh_subscription_data",
    hdrs = ["zenoh_subscription_data.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
    ],
)

cc_library(
    name = "subscription",
    srcs = [
        "zenoh_subscription.cc",
    ],
    hdrs = [
        "subscription.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],
    deps = [
        ":zenoh_subscription_data",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "liveliness_subscription_data",
    hdrs = ["liveliness_subscription_data.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
    ],
)

cc_library(
    name = "liveliness_subscription",
    srcs = [
        "liveliness_subscription.cc",
    ],
    hdrs = [
        "liveliness_subscription.h",
    ],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],
    deps = [
        ":liveliness_subscription_data",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "publisher_stats",
    srcs = ["publisher_stats.cc"],
    hdrs = ["publisher_stats.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
    ],
)

cc_library(
    name = "topic_config",
    srcs = ["topic_config.cc"],
    hdrs = ["topic_config.h"],
)

cc_library(
    name = "pubsub_callbacks",
    hdrs = ["pubsub_callbacks.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
    ],
)
