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

"""Forwarding definitions to intrinsic_sdk."""

# Forwarding definitions were added in support of go/intrinsic-ioc-transition-tdd.

# buildifier: disable=bzl-visibility
load(
    "//intrinsic_sdk/bazel:go_macros.bzl",
    _go_binary = "go_binary",
    _go_grpc_library = "go_grpc_library",
    _go_library = "go_library",
    _go_proto_library = "go_proto_library",
    _go_test = "go_test",
    _importpath_matches_go_package_test = "importpath_matches_go_package_test",
)

go_binary = _go_binary
go_library = _go_library
go_grpc_library = _go_grpc_library
go_proto_library = _go_proto_library
go_test = _go_test
importpath_matches_go_package_test = _importpath_matches_go_package_test
