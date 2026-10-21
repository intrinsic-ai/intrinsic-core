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
load("//bazel:migration_locations.bzl", "GOOGLE3_OR_IOC_LOCATIONS")

package(default_visibility = ["//visibility:public"])

genrule(
    name = "migration_locations_go_gen",
    outs = ["migration_locations.go"],
    cmd = """
cat > $@ <<'EOF'
package pathresolver

var migrationLocations = []string{
%s,
}
EOF
""" % ",\n".join(['\t"%s"' % loc for loc in GOOGLE3_OR_IOC_LOCATIONS]),
)

cc_library(
    name = "path_resolver",
    srcs = [
        "path_resolver.cc",
    ],
    hdrs = [
        "path_resolver.h",
    ],
    deps = [
        "@abseil-cpp//absl/debugging:leak_check",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
        "@bazel_tools//tools/cpp/runfiles",
        "@or_tools//ortools/base:path",
    ],
)

go_library(
    name = "pathresolver",  # Coding style suggests to avoid using underscores in package names: http://golang.org/doc/effective_go.html#package-names
    srcs = [
        "path_resolver.go",
        ":migration_locations_go_gen",
    ],
    importpath = "intrinsic/util/path_resolver/pathresolver",
    deps = [
        "@io_bazel_rules_go//go/runfiles:go_default_library",
    ],
)
