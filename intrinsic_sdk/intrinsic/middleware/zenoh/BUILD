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

load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "imw_zenoh",
    srcs = [
        "imw_zenoh.cc",
        "imw_zenoh_data_callback_context.cc",
        "imw_zenoh_globals.cc",
        "imw_zenoh_liveliness_subscription.cc",
        "imw_zenoh_publisher.cc",
        "imw_zenoh_queryable.cc",
        "imw_zenoh_queryable_context.cc",
        "imw_zenoh_subscription.cc",
    ],
    hdrs = [
        "imw_zenoh.h",
        "imw_zenoh_data_callback_context.h",
        "imw_zenoh_liveliness_get_context.h",
        "imw_zenoh_liveliness_subscription.h",
        "imw_zenoh_liveliness_token.h",
        "imw_zenoh_publisher.h",
        "imw_zenoh_query_context.h",
        "imw_zenoh_queryable.h",
        "imw_zenoh_queryable_context.h",
        "imw_zenoh_reply_context.h",
        "imw_zenoh_subscription.h",
    ],
    linkstatic = True,
    visibility = ["//visibility:public"],
    deps = [
        "//intrinsic_sdk/intrinsic/middleware:imw_interface",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/base:no_destructor",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/synchronization",
        "@com_github_nlohmann_json//:json",
        "@zenoh-c",
    ],
)

exports_files([
    "config/zenoh_router_config.json5",
    "config/zenoh_peer_config.json5",
])
