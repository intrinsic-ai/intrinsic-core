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

cc_library(
    name = "object_world_test_utils",
    testonly = True,
    srcs = ["object_world_test_utils.cc"],
    hdrs = ["object_world_test_utils.h"],
    visibility = [
        "//intrinsic:intrinsic_internal",
    ],
    deps = [
        "//intrinsic/resources/proto:geometric_resource_data_cc_proto",
        "//intrinsic/world",
        "//intrinsic/world:entity",
        "//intrinsic/world:kinematic_world_views",
        "//intrinsic/world/component:attachment_component",
        "//intrinsic/world/component:collections_component",
        "//intrinsic/world/component:collections_member_component",
        "//intrinsic/world/component:robot_component",
        "//intrinsic/world/objects:object_world",
        "//intrinsic/world/objects:object_world_creation_utils",
        "//intrinsic/world/proto:collections_component_cc_proto",
        "//intrinsic/world/test:world_test_utils",
        "//intrinsic_kinematics/intrinsic/kinematics/types:cartesian_limits",
        "//intrinsic_kinematics/intrinsic/kinematics/types:joint_limits_xd",
        "//intrinsic_sdk/intrinsic/eigenmath",
        "//intrinsic_sdk/intrinsic/geometry/api:shape_factory",
        "//intrinsic_sdk/intrinsic/geometry/storage:geometry_serializer",
        "//intrinsic_sdk/intrinsic/math:pose3",
        "//intrinsic_sdk/intrinsic/math:proto_conversion",
        "//intrinsic_sdk/intrinsic/scene/conversion:object_properties_conversion",
        "//intrinsic_sdk/intrinsic/util/proto:parse_text_proto",
        "//intrinsic_sdk/intrinsic/util/status:status_macros",
        "//intrinsic_sdk/intrinsic/world:entity_id",
        "//intrinsic_sdk/intrinsic/world:geometry_types",
        "//intrinsic_sdk/intrinsic/world:labels",
        "//intrinsic_sdk/intrinsic/world/component:geometry_component",
        "//intrinsic_sdk/intrinsic/world/component:kinematics_component",
        "//intrinsic_sdk/intrinsic/world/hashing",
        "//intrinsic_sdk/intrinsic/world/objects:object_world_ids",
        "//intrinsic_sdk/intrinsic/world/robot_payload",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@com_google_googletest//:gtest",
        "@intrinsic_apis//intrinsic/scene/proto/v1:scene_object_cc_proto",
        "@intrinsic_apis//intrinsic/world/proto:kinematics_component_cc_proto",
    ],
)
