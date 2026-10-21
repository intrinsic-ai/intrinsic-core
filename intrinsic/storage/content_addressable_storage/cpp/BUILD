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

cc_library(
    name = "client_helpers_cc",
    srcs = ["client_helpers.cc"],
    hdrs = ["client_helpers.h"],
    visibility = [
        "//intrinsic/geometry/storage:__pkg__",
        "//intrinsic/storage/content_addressable_storage/testing:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service:__pkg__",
        "//intrinsic_motion_planning/intrinsic/motion_planning/service/nonvolatile_cache:__pkg__",
    ],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_conversion_grpc",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/crc:crc32c",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/memory",
        "@abseil-cpp//absl/random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/time",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_riegeli//riegeli/bytes:pullable_reader",
        "@com_google_riegeli//riegeli/bytes:reader",
        "@com_google_riegeli//riegeli/bytes:string_reader",
        "@com_google_riegeli//riegeli/bytes:string_writer",
        "@com_google_riegeli//riegeli/bytes:writer",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_cc_grpc_proto",
        "@intrinsic_apis//intrinsic/storage/content_addressable_storage/proto:cas_service_cc_proto",
    ],
)
