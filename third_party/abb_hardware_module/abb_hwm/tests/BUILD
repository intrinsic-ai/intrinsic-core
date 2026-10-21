load("//bazel:cc_macros.bzl", "cc_test")

package(
    default_applicable_licenses = ["//third_party/abb_hardware_module:license"],
    default_visibility = ["//visibility:public"],
)

cc_test(
    name = "rws_egm_utils_test",
    srcs = ["rws_egm_utils_test.cc"],
    copts = [
        "-fexceptions",
    ],
    features = ["-use_header_modules"],  # Incompatible with -fexceptions.
    deps = [
        "//third_party/abb_hardware_module/abb_hwm:abb_config_cc_proto",
        "//third_party/abb_hardware_module/abb_hwm:rws_egm_utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings:str_format",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "egm_serialization_test",
    srcs = ["egm_serialization_test.cc"],
    deps = [
        "//third_party/abb_egm:egm_cc_proto",
        "//third_party/abb_hardware_module/abb_hwm:egm_serialization",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "egm_server_test",
    srcs = ["egm_server_test.cc"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/thread:thread_options",
        "//third_party/abb_hardware_module/abb_hwm:egm_serialization",
        "//third_party/abb_hardware_module/abb_hwm:egm_server",
        "//third_party/abb_hardware_module/abb_hwm:udp_server",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
        "@com_google_googletest//:gtest_main",
        "@com_google_protobuf//:protobuf",
    ],
)

cc_test(
    name = "abb_elog_test",
    srcs = ["abb_elog_test.cc"],
    deps = [
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//third_party/abb_hardware_module/abb_hwm:abb_elog",
        "@abseil-cpp//absl/strings:string_view",
        "@com_github_nlohmann_json//:json",
        "@com_google_googletest//:gtest_main",
    ],
)
