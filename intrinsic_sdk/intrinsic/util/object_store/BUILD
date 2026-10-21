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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "memoize",
    hdrs = [
        "memoize.h",
        "memoize_internal.h",
    ],
    deps = [
        ":memoize_fingerprint",
        ":object_ref",
        ":object_store",
        "//intrinsic_sdk/intrinsic/marshal:riegeli_coder",
        "//intrinsic_sdk/intrinsic/util:hash",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "memoize_fingerprint",
    hdrs = ["memoize_fingerprint.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/marshal:riegeli_coder",
        "//intrinsic_sdk/intrinsic/util:hash",
        "//intrinsic_sdk/intrinsic/util:string_type",
        "//intrinsic_sdk/intrinsic/util/thread:stop_token",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/strings",
        "@com_google_riegeli//riegeli/base:initializer",
        "@com_google_riegeli//riegeli/bytes:string_writer",
        "@com_google_riegeli//riegeli/records:record_writer",
    ],
)

cc_library(
    name = "multi_mutex",
    srcs = ["multi_mutex.cc"],
    hdrs = ["multi_mutex.h"],
    deps = [
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/synchronization",
        "@onetbb//:tbb",
    ],
)

cc_library(
    name = "object_cache",
    srcs = ["object_cache.cc"],
    hdrs = ["object_cache.h"],
    deps = [
        "//intrinsic_sdk/intrinsic/util:lru_cache",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/types:any",
    ],
)

cc_library(
    name = "object_ref",
    hdrs = ["object_ref.h"],
    deps = [
        ":memoize_fingerprint",
        ":object_cache",
        "//intrinsic_sdk/intrinsic/util:hash",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "object_store",
    srcs = ["object_store_internal.cc"],
    hdrs = [
        "object_store.h",
        "object_store_internal.h",
    ],
    deps = [
        ":multi_mutex",
        ":object_cache",
        ":object_ref",
        "//intrinsic_sdk/intrinsic/marshal:riegeli_coder",
        "//intrinsic_sdk/intrinsic/util:hash",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//third_party/thorough_hash",
        "@abseil-cpp//absl/flags:flag",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/meta:type_traits",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_riegeli//riegeli/base:initializer",
        "@com_google_riegeli//riegeli/bytes:string_writer",
        "@com_google_riegeli//riegeli/records:record_reader",
        "@com_google_riegeli//riegeli/records:record_writer",
    ],
)
