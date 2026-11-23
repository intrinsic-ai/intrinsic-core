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

"""Compatibility shim for //intrinsic_sdk/intrinsic/util/path_resolver:paths.bzl."""

load(
    "//intrinsic_sdk/intrinsic/util/path_resolver:paths.bzl",
    _BASH_RLOCATION_FUNCTION = "BASH_RLOCATION_FUNCTION",
    _WRAPPER_HEADER = "WRAPPER_HEADER",
    _to_rlocation_path = "to_rlocation_path",
)

BASH_RLOCATION_FUNCTION = _BASH_RLOCATION_FUNCTION
WRAPPER_HEADER = _WRAPPER_HEADER
to_rlocation_path = _to_rlocation_path
