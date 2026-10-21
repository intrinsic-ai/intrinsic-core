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

load("//bazel:python.bzl", "py_test")
load("//intrinsic/assets/build_defs:solution.bzl", "intrinsic_solution")
load("//intrinsic_sdk/intrinsic/assets/build_defs:asset.bzl", "intrinsic_asset_instance")

intrinsic_solution(
    name = "ioc_example_application",
    assets = [
        "//intrinsic/resources/catalog/resourcedata/product/building_block",
        "//intrinsic/simulation/gazebo/asset:gazebo_simulator_type",
        "//intrinsic_sdk/intrinsic/assets/services/examples/calcserver:calculator_service",
        "//intrinsic/skills/examples:adder_skill",
    ],
    instances = [
        ":building_block",
        ":calculator_service",
        ":gazebo_simulator",
    ],
)

intrinsic_asset_instance(
    name = "building_block",
    asset = "ai.intrinsic.building_block",
)

intrinsic_asset_instance(
    name = "gazebo_simulator",
    asset = "ai.intrinsic.gazebo_simulator",
)

intrinsic_asset_instance(
    name = "calculator_service",
    asset = "ai.intrinsic.calculator_service",
)

py_test(
    name = "e2e_calculator_test_py",
    srcs = ["e2e_calculator_test.py"],
    main = "e2e_calculator_test.py",
    tags = ["manual"],
    deps = [
        "//intrinsic_sdk/intrinsic/solutions:deployments",
    ],
)
