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

filegroup(
    name = "fds-template",
    srcs =
        ["fds_core.yaml"],
    visibility = [
        "//intrinsic/kubernetes:__subpackages__",
        "//intrinsic/manipulation:__subpackages__",
        "//intrinsic/platform/pubsub:__subpackages__",
        "//intrinsic_perception:__subpackages__",
        "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    ],
)

# PubSub API library. The user-facing API providing access to
# publish-subscribe system.
alias(
    name = "pubsub",
    actual = "//intrinsic_sdk/intrinsic/platform/pubsub",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "publisher",
    actual = "//intrinsic_sdk/intrinsic/platform/pubsub:publisher",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "queryable",
    actual = "//intrinsic_sdk/intrinsic/platform/pubsub:queryable",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

alias(
    name = "subscription",
    actual = "//intrinsic_sdk/intrinsic/platform/pubsub:subscription",
    tags = ["go/intrinsic-ioc-transition-tdd"],
)

cc_library(
    name = "pubsub_metrics",
    srcs = ["pubsub_metrics.cc"],
    hdrs = ["pubsub_metrics.h"],
    deps = [
        "//intrinsic/stats:metrics_utils",
        "@abseil-cpp//absl/strings:string_view",
        "@io_opentelemetry_cpp//api",
    ],
)
