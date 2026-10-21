load("//bazel:container.bzl", "container_image", "container_layer")
load("//bazel:container_structure_test.bzl", "container_structure_test")

package(default_visibility = [
    "//intrinsic_runtime/kubernetes/intrinsic_base:__pkg__",
    "//incode/ml/backends/ml_train_backend:__subpackages__",
    "//incode/ml/services:__subpackages__",
])

exports_files(["toybox_dynamic_link.patch"])

container_layer(
    name = "shell",
    files = [
        "@toybox",
    ],
    symlinks = {
        "bin/cat": "../toybox",
        "bin/chgrp": "../toybox",
        "bin/chmod": "../toybox",
        "bin/chown": "../toybox",
        "bin/cp": "../toybox",
        "bin/echo": "../toybox",
        "bin/false": "../toybox",
        "bin/find": "../toybox",
        "bin/grep": "../toybox",
        "bin/insmod": "../toybox",
        "bin/kill": "../toybox",
        "bin/ln": "../toybox",
        "bin/ls": "../toybox",
        "bin/mkdir": "../toybox",
        "bin/mv": "../toybox",
        "bin/netstat": "../toybox",
        "bin/ps": "../toybox",
        "bin/pwd": "../toybox",
        "bin/rm": "../toybox",
        "bin/rmdir": "../toybox",
        "bin/sed": "../toybox",
        "bin/sh": "../toybox",
        "bin/sleep": "../toybox",
        "bin/top": "../toybox",
        "bin/touch": "../toybox",
        "bin/true": "../toybox",
        "bin/which": "../which",
        "bin/xargs": "../toybox",
    },
)

container_image(
    name = "image_with_shell",
    base = "//intrinsic/kubernetes:base-image-oci",
    data_path = "/",
    layers = [
        ":shell",
    ],
)

container_structure_test(
    name = "image_with_shell_test",
    timeout = "long",
    configs = [":container_test.yaml"],
    image = ":image_with_shell",
)
