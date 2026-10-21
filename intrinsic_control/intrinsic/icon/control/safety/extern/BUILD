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

load("//intrinsic_sdk/intrinsic/platform:flatbuffers.bzl", "cc_flatbuffers_library", "flatbuffers_library")

package(
    # No package outside of "safety" should see this.
    default_visibility = ["//intrinsic_control/intrinsic/icon/control/safety"],
)

flatbuffers_library(
    name = "safety_status_fbs",
    srcs = [
        "safety_status.fbs",
    ],
)

cc_flatbuffers_library(
    name = "safety_status_fbs_cc",
    visibility = [
        "//intrinsic:public_api_users",
        "//intrinsic_control/intrinsic/icon:hardware_module_authors",
    ],
    deps = [":safety_status_fbs"],
)
