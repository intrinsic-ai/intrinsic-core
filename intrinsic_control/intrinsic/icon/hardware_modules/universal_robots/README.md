<!--
Copyright 2026 Intrinsic Innovation LLC

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# Universal Robots Hardware Module

> [!NOTE]
> Developed and maintained by Intrinsic. Not an official product of Universal Robots.

## Overview

This directory contains the ICON Hardware Module (HWM) implementation for Universal Robots (UR) manipulators, communicating via the Real-Time Data Exchange (RTDE) protocol.

## Supported Robot Models & Device Targets

| Robot Model | `intrinsic_hardware_device` Bazel Target | Asset ID |
| :--- | :--- | :--- |
| **UR3e** | `:ur3e_hardware_module_core` | `ai.intrinsic.ur3e_hardware_module_core` |
| **UR5e** | `:ur5e_hardware_module_core` | `ai.intrinsic.ur5e_hardware_module_core` |
| **UR10e** | `:ur10e_hardware_module_core` | `ai.intrinsic.ur10e_hardware_module_core` |
| **UR16e** | `:ur16e_hardware_module_core` | `ai.intrinsic.ur16e_hardware_module_core` |

## Documentation

For documentation on setting up Universal Robots with Intrinsic Flowstate, see the [Universal Robots Setup Guide](https://flowstate.intrinsic.ai/docs/guides/commission_solution/set_up_robot/universal_robots_setup).
