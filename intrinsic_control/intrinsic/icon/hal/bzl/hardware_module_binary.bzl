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
Create an executable for a given plugin implementation.

The rule adds necessary driver code around the plugin interface implementation
to guarantee a guided execution of the plugin. This further lets a plugin developer focus on
the essential implementation of the interface rather than duplicating boilerplate code.
"""

load("//bazel:cc_macros.bzl", "cc_binary")

def hardware_module_binary(
        name,
        hardware_module_lib,
        **kwargs):
    """Creates a binary for a hardware module.

    This can be run directly, as a standard hardware module, or as a resource.

    Args:
      name: The name of the binary.
      hardware_module_lib: The C++ library that defines the hardware module to
          generate an image for.
      **kwargs: Additional arguments to pass to cc_binary.
    """
    cc_binary(
        name = name,
        srcs = [Label("//intrinsic_control/intrinsic/icon/hal:hardware_module_main")],
        deps = [hardware_module_lib] + [
            Label("//intrinsic_control/intrinsic/icon/control:realtime_clock_interface"),
            Label("//intrinsic_control/intrinsic/icon/hal:hardware_module_config_cc_proto"),
            Label("//intrinsic_control/intrinsic/icon/hal:hardware_module_health_service"),
            Label("//intrinsic_control/intrinsic/icon/hal:hardware_module_init_context"),
            Label("//intrinsic_control/intrinsic/icon/hal:hardware_module_main_util"),
            Label("//intrinsic_control/intrinsic/icon/hal:hardware_module_registry"),
            Label("//intrinsic_control/intrinsic/icon/hal:hardware_module_runtime"),
            Label("//intrinsic_control/intrinsic/icon/hal:hardware_module_util"),
            Label("//intrinsic_control/intrinsic/icon/hal:module_config"),
            Label("//intrinsic_control/intrinsic/icon/interprocess/shared_memory_manager"),
            Label("//intrinsic_control/intrinsic/icon/release/portable:init_intrinsic"),
            Label("//intrinsic_control/intrinsic/icon/release:file_helpers"),
            Label("//intrinsic_control/intrinsic/icon/utils:malloc_guard"),  
            Label("//intrinsic_control/intrinsic/icon/utils:shutdown_signals"),
            Label("@intrinsic_apis//intrinsic/resources/proto:resource_registry_cc_proto"),
            Label("@intrinsic_apis//intrinsic/resources/proto:runtime_context_cc_proto"),
            Label("//intrinsic_sdk/intrinsic/util/proto:any"),
            Label("//intrinsic_sdk/intrinsic/util/proto:get_text_proto"),
            Label("//intrinsic_sdk/intrinsic/util/status:status_builder"),
            Label("//intrinsic_sdk/intrinsic/util/status:status_macros"),
            Label("//intrinsic_sdk/intrinsic/util/thread:util"),
            Label("//intrinsic_sdk/intrinsic/util:memory_lock"),
            Label("@abseil-cpp//absl/base:nullability"),
            Label("@abseil-cpp//absl/container:flat_hash_set"),
            Label("@abseil-cpp//absl/flags:flag"),
            Label("@abseil-cpp//absl/log"),
            Label("@abseil-cpp//absl/log:check"),
            Label("@abseil-cpp//absl/log:flags"),
            Label("@abseil-cpp//absl/status"),
            Label("@abseil-cpp//absl/status:statusor"),
            Label("@abseil-cpp//absl/strings"),
            Label("@abseil-cpp//absl/time"),
        ],
        **kwargs
    )
