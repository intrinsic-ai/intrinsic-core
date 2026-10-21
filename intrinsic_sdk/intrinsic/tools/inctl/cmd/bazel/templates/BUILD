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

# Templates used by the external inctl tool.
package(default_visibility = [
    "//intrinsic/tools:__subpackages__",
    "//intrinsic_sdk/intrinsic/tools:__subpackages__",
])

exports_files(
    srcs = [
        "MODULE.bazel.template",
    ],
    visibility = [
        "//intrinsic/tools/inctl/cmd/bazel:__subpackages__",
        "//intrinsic_sdk/intrinsic/tools/inctl/cmd/bazel:__subpackages__",
    ],
)

genrule(
    name = "bazelrc",
    srcs = ["//:.bazelrc"],
    outs = ["bazelrc.template"],
    cmd = "cat $< " +
          # Remove copyright notice if present.
          r"| perl -0777pe 's/# Copyright(.*?)\n\n//s' " +
          # Remove all lines starting with "common:" plus preceding comment and empty lines.
          r"| perl -0777pe 's/(^\s*\n)*(^#.*\n)*(^common:.*\n)+//mg' " +
          "> $@",
)

genrule(
    name = "bazelversion",
    srcs = ["//:.bazelversion"],
    outs = ["bazelversion.template"],
    cmd = "cat $< > $@",
)

genrule(
    name = "permissive_content_mirror",
    srcs = ["//bazel:content_mirror/permissive.cfg"],
    outs = ["permissive_content_mirror.template"],
    cmd = "cat $< > $@",
)
