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

"""
Starlark macro to generate a container that runs a textproto-configured ICON machine.
"""

load("//bazel:cc_macros.bzl", "cc_binary", "cc_test")
load("//bazel:container.bzl", "container_image", "container_layer")

def icon_server(
        name,
        deps = [],
        testonly = False,
        compatible_with = [],
        visibility = None):
    """Macro to generate a configured ICON server.

    Generates an ICON server binary with the listed dependencies.
    Args:
      name: Name of the rule.
      deps: Dependencies needed for the server to run
      testonly: If true, only testonly targets can depend on this target.
      compatible_with: All generated targets are marked compatible_with these platforms.
      visibility: Visibility attribute to use for all generated targets.
    """

    # Build the main_loop binary.
    cc_binary(
        name = name,
        deps = [
            "//intrinsic_control/intrinsic/icon/server:icon_main",
        ] + deps,
        testonly = testonly,
        compatible_with = compatible_with,
        visibility = visibility,
    )

def icon_machine(
        name,
        init_test_config = None,
        deps = [],
        testonly = False,
        compatible_with = [],
        visibility = None,
        skip_mainloop_init_test = False,
        fake_hwm_configs = [],
        init_test_world_gzf_path = None,
        init_test_resource_id = ""):
    """Macro to generate a configured ICON server in a Docker container.

    This consists of several subrules:
    1. Generate an icon_server rule to build the configured ICON server.
    2. Generate a pkg_tar rule for the machine and its runfiles.
    3. Generate a docker container for the machine and its runfiles.

    Args:
      name: Name of the rule.
      deps: Dependencies needed for the server to run (e.g. Service libraries)
      testonly: If true, only testonly targets can depend on this target.
      compatible_with: All generated targets are marked compatible_with these platforms.
      visibility: Visibility attribute to use for all generated targets.
      skip_mainloop_init_test: If true, do not generate a mainloop_init_test.
      fake_hwm_configs: One HAL config of type `intrinsic_proto.icon.HardwareModuleConfig`
                        for every LoopbackFake module that is used to create the HAL layer for this
                        test. An empty array means that no LoopbackFake module is created.
      init_test_config: Textproto config file to load.
                    When a `init_test_config` is provided, this macro generates an `init_test` rule
                    that ensures that ICON starts up cleanly with the given configuration.
                    Resource instances load their configuration from the ResourceContext instead.
      init_test_world_gzf_path: Optional path to a World GZF file. If provided, mainloop_init_test
                                will serve that World via gRPC and inject the connection parameters
                                into the mainloop by modifying the IconMainConfig.
      init_test_resource_id: Optional resource id to use in the init_test. Only meaningful when
                             `init_test_world_gzf_path` is defined.
    """

    if init_test_config == None:
        # This is a temporary workaround until we have dedicated resource tests.
        # b/294436192 to introduce a icon_resource_instance macro which also enables the testing functionality again.
        skip_mainloop_init_test = True

    # init_icon is a wrapper to work around the restart backoff of Kubernetes.
    init_icon_target = Label("//intrinsic_control/intrinsic/icon/utils:init_icon")

    # Build the ICON server.
    # Produces an ICON server with the name `name + "_server"`.
    icon_server(
        name = name + "_server",
        deps = deps,
        testonly = testonly,
        compatible_with = compatible_with,
        visibility = visibility,
    )

    # Package icon main loop and runfiles into a container.
    srcs = [name + "_server", init_icon_target]
    container_layer(
        name = name + "_tar",
        files = srcs,
        include_runfiles = True,
        data_path = "/",
        testonly = testonly,
        compatible_with = compatible_with,
        visibility = ["//visibility:private"],
    )

    container_image_kwargs = {
        "base": "//intrinsic/kubernetes:base-image-oci",
        "compatible_with": compatible_with,
        "layers": [
            name + "_tar",
        ],
        "testonly": testonly,
        "visibility": visibility,
    }

    # Resources use the resource_context for configuration. They should not be called with `--config_pbtxt_file`.
    resource_cmd = [
        "/intrinsic/icon/utils/init_icon",
        "--",
        native.package_name() + "/" + name + "_server",
    ]

    container_image(
        name = name + "_resource",
        cmd = resource_cmd,
        **container_image_kwargs
    )

    if not skip_mainloop_init_test:
        icon_config_init_test(
            name = name + "_init_test",
            cfg = init_test_config,
            fake_hwm_configs = fake_hwm_configs,
            init_test_world_gzf_path = init_test_world_gzf_path,
            init_test_resource_id = init_test_resource_id,
            compatible_with = compatible_with,
            visibility = visibility,
        )

def icon_config_init_test(
        name,
        cfg,
        fake_hwm_configs = [],
        init_test_world_gzf_path = None,
        init_test_resource_id = "",
        compatible_with = [],
        visibility = None):
    """Macro to generate test ICON mainloop initialization with for a specific configuration.

    Args:
      name: Name of the rule.
      cfg: Textproto config file to load. Can be a plain 'intrinsic_proto.icon.IconMainConfig',
           or wrapped into 'Any'.
      fake_hwm_configs: One HAL config of type `intrinsic_proto.icon.HardwareModuleConfig`
                        for every LoopbackFake module that is used to create the HAL layer for this
                        test. An empty array means that no LoopbackFake module is created.
      init_test_world_gzf_path: Optional path to a World GZF file. If provided, mainloop_init_test
                                will serve that World via gRPC and inject the connection parameters
                                into the mainloop by modifying the IconMainConfig.
      init_test_resource_id: Optional resource id to use in the init_test. Only meaningful when
                             `init_test_world_gzf_path` is defined.
      compatible_with: All generated targets are marked compatible_with these platforms.
      visibility: Visibility attribute to use for all generated targets.
    """
    args = [
        (("--world_gzf_path=$(location %s)" % init_test_world_gzf_path) if init_test_world_gzf_path else ""),
        "--resource_id=\"%s\"" % init_test_resource_id,
        "--fake_hwm_configs=%s" % ",".join(["$(location %s)" % config_file for config_file in fake_hwm_configs]),
    ]
    data = fake_hwm_configs + ([init_test_world_gzf_path] if init_test_world_gzf_path else [])
    args.append(
        "--config_pbtxt_file=$(location %s)" % cfg,
    )
    data.append(cfg)
    cc_test(
        name = name,
        args = args,
        deps = [
            "//intrinsic_control/intrinsic/icon/server:icon_main_init_test",
            "@com_google_googletest//:gtest_main",
        ],
        data = data,
        tags = [
            "requires-net:ipv4",  # Required for gRPC connection to world.
            "requires-net:loopback",
        ],
        compatible_with = compatible_with,
        visibility = visibility,
    )
