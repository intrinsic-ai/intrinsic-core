load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
    default_visibility = ["//visibility:public"],
)

license(
    name = "license",
    package_name = "universal_robots",
    license_kinds = [
        "@rules_license//licenses/spdx:BSD-3-Clause",
    ],
    license_text = "LICENSE",
)

exports_files([
    "LICENSE",
    "README.md",
])

filegroup(
    name = "ur10e",
    srcs = [
        "LICENSE",
        "README.md",
    ] + glob(["ur10e/**"]),
)

filegroup(
    name = "ur16e",
    srcs = [
        "LICENSE",
        "README.md",
    ] + glob(["ur16e/**"]),
)

filegroup(
    name = "ur3e",
    srcs = [
        "LICENSE",
        "README.md",
    ] + glob(["ur3e/**"]),
)

filegroup(
    name = "ur5e",
    srcs = [
        "LICENSE",
        "README.md",
    ] + glob(["ur5e/**"]),
)
