load("@rules_license//rules:license.bzl", "license")
load("//bazel:cc_macros.bzl", "cc_library")

package(
    default_visibility = [
        "//visibility:public",
    ],
)

# Copied from https://github.com/google/sling/blob/master/LICENSE
exports_files(["LICENSE"])

license(
    name = "license",
    package_name = "sling",
    license_kinds = [
        "@rules_license//licenses/spdx:Apache-2.0",
    ],
    license_text = "LICENSE",
    package_url = "https://github.com/google/sling",
)

# Forked from https://github.com/google/sling/blob/master/sling/string/numbers.h

# TODO(b/289938628) Use go/insrc-g3-to-oss with "sed" to select lines instead of manual copy.

cc_library(
    name = "sort",
    srcs = ["sort.cc"],
    hdrs = ["sort.h"],
    applicable_licenses = [":license"],
    deps = ["@abseil-cpp//absl/strings"],
)
