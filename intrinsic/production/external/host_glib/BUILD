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

"""Host glib system library stubs multiplexer for Qt6 transitions."""

package(
    default_visibility = [
        "//intrinsic_perception/intrinsic/perception/cameras/aravis:__pkg__",
    ],
)

alias(
    name = "glib",
    actual = select({
        "@platforms//cpu:aarch64": "@host_glib_arm64//:glib",
        "//conditions:default": "@host_glib_amd64//:glib",
    }),
)

alias(
    name = "gobject",
    actual = select({
        "@platforms//cpu:aarch64": "@host_glib_arm64//:gobject",
        "//conditions:default": "@host_glib_amd64//:gobject",
    }),
)

alias(
    name = "gio",
    actual = select({
        "@platforms//cpu:aarch64": "@host_glib_arm64//:gio",
        "//conditions:default": "@host_glib_amd64//:gio",
    }),
)

alias(
    name = "gmodule",
    actual = select({
        "@platforms//cpu:aarch64": "@host_glib_arm64//:gmodule",
        "//conditions:default": "@host_glib_amd64//:gmodule",
    }),
)
