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

"""List of well known proto types available to the proto builder."""

visibility("//intrinsic_runtime/intrinsic/proto_tools/registry")

WELL_KNOWN_PROTO_TARGETS = [
    "@intrinsic_apis//intrinsic/assets/proto:field_metadata_proto",
    "@intrinsic_apis//intrinsic/assets/proto:id_proto",
    "@intrinsic_apis//intrinsic/assets/proto/v1:resolved_dependency_proto",
    "@intrinsic_apis//intrinsic/icon/proto:cart_space_proto",
    "@intrinsic_apis//intrinsic/icon/proto:joint_space_proto",
    "@intrinsic_apis//intrinsic/math/proto:accel_proto",
    "@intrinsic_apis//intrinsic/math/proto:affine_proto",
    "@intrinsic_apis//intrinsic/math/proto:array_proto",
    "@intrinsic_apis//intrinsic/math/proto:matrix_proto",
    "@intrinsic_apis//intrinsic/math/proto:point_proto",
    "@intrinsic_apis//intrinsic/math/proto:pose_proto",
    "@intrinsic_apis//intrinsic/math/proto:quaternion_proto",
    "@intrinsic_apis//intrinsic/math/proto:twist_proto",
    "@intrinsic_apis//intrinsic/math/proto:vector2_proto",
    "@intrinsic_apis//intrinsic/math/proto:vector3_proto",
    "@intrinsic_apis//intrinsic/perception/proto/v1:camera_settings_proto",
    "@intrinsic_apis//intrinsic/perception/proto/v1:capture_data_proto",
    "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimate_in_root_proto",
    "@intrinsic_apis//intrinsic/perception/proto/v1:pose_estimator_id_proto",
    "@intrinsic_apis//intrinsic/skills/proto:skill_parameter_metadata_proto",
    "@intrinsic_apis//intrinsic/world/proto:object_world_refs_proto",
    "//intrinsic_sdk/intrinsic/world/proto:object_world_updates_proto",
    "@com_google_protobuf//:any_proto",
    "@com_google_protobuf//:duration_proto",
    "@com_google_protobuf//:empty_proto",
    "@com_google_protobuf//:timestamp_proto",
    "@com_google_protobuf//:wrappers_proto",
]
