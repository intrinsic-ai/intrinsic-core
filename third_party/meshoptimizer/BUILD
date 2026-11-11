load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:public"],
)

exports_files(["LICENSE"])

license(
    name = "license",
    package_name = "meshoptimizer",
    license_kinds = [
        "@rules_license//licenses/spdx:MIT",
    ],
    license_text = "LICENSE",
    package_url = "https://github.com/zeux/meshoptimizer",
)

cc_library(
    name = "simplifier",
    srcs = ["simplifier.cpp"],
    hdrs = ["simplifier.h"],
    deps = [
        "@meshoptimizer",
    ],
)
