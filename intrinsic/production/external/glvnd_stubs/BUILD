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

"""GLVND GL/EGL and host glib system library stubs multiplexer.

Routes to the architecture-specific package repository based on target CPU.
"""

package(
    default_visibility = [
        "//visibility:public",
    ],
)

alias(
    name = "glvnd_stubs",
    actual = select({
        "@platforms//cpu:aarch64": "@glvnd_stubs_arm64//:glvnd_stubs",
        "//conditions:default": "@glvnd_stubs_amd64//:glvnd_stubs",
    }),
)
