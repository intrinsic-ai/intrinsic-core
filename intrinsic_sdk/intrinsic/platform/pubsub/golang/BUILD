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
load("//bazel:go_macros.bzl", "go_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "pubsub_c",
    srcs = ["pubsub_c.cc"],
    hdrs = ["pubsub_c.h"],
    visibility = ["//visibility:private"],
    deps = [
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_config",
        "//intrinsic_sdk/intrinsic/platform/pubsub/zenoh_util:zenoh_handle",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@abseil-cpp//absl/time",
        "@com_google_protobuf//:protobuf",
        "@intrinsic_apis//intrinsic/platform/pubsub/adapters:pubsub_cc_proto",
    ],
)

go_library(
    name = "pubsub",
    srcs = ["pubsub.go"],
    cdeps = [
        ":pubsub_c",
    ],
    cgo = 1,
    importpath = "intrinsic/platform/pubsub/golang/pubsub",
    deps = [
        ":kvstore",
        ":pubsubinterface",
        "@com_github_cenkalti_backoff_v4//:backoff",
        "@com_github_golang_glog//:go_default_library",
        "@intrinsic_apis//intrinsic/platform/common/proto:workcell_info_go_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub/adapters:pubsub_go_proto",
        "@intrinsic_apis//intrinsic/platform/pubsub/admin_set_grpc/v1:admin_set_go_proto",
        "@org_golang_google_grpc//:go_default_library",
        "@org_golang_google_grpc//credentials/insecure:go_default_library",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
        "@org_golang_google_protobuf//types/known/timestamppb",
        "@org_golang_x_time//rate",
    ],
)

go_library(
    name = "pubsubinterface",
    srcs = ["pubsub_interface.go"],
    importpath = "intrinsic/platform/pubsub/golang/pubsubinterface",
    deps = [
        "@intrinsic_apis//intrinsic/platform/pubsub/adapters:pubsub_go_proto",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)

go_library(
    name = "kvstore",
    srcs = ["kvstore.go"],
    importpath = "intrinsic/platform/pubsub/golang/kvstore",
    deps = [
        ":pubsubinterface",
        "@org_golang_google_protobuf//proto",
        "@org_golang_google_protobuf//types/known/anypb",
    ],
)
