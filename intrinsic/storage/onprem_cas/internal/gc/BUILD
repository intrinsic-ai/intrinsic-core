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

load("//bazel:go_macros.bzl", "go_library")

go_library(
    name = "gc",
    srcs = ["gc.go"],
    importpath = "intrinsic/storage/onprem_cas/internal/gc",
    visibility = ["//intrinsic/storage/onprem_cas:__subpackages__"],
    deps = [
        "//intrinsic/storage/content_addressable_storage/pkg/idhandling",
        "//intrinsic/storage/onprem_cas/internal",
    ],
)
