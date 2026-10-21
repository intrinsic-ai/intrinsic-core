// Copyright 2026 Intrinsic Innovation LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "google/protobuf/message.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/proto/joint_trajectory_conversion.h"
#include "intrinsic/kinematics/types/joint_trajectories.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/path_planning/path_segment.h"
#include "intrinsic/motion_planning/proto/v1/motion_blending_parameter.pb.h"
#include "intrinsic/motion_planning/proto/v1/motion_planning_limits.pb.h"
#include "intrinsic/motion_planning/proto/v1/robot_specification.pb.h"
#include "intrinsic/motion_planning/service/cache/cache_utils.h"
#include "intrinsic/motion_planning/service/nonvolatile_cache/motion_planner_nonvolatile_cache.pb.h"
#include "intrinsic/storage/content_addressable_storage/cpp/client_helpers.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto/pb_hash.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/object_world_proto_utils.h"
#include "intrinsic/world/objects/world_object_internal.h"

namespace intrinsic {

absl::StatusOr<MotionPlannerNonvolatileCacheKey>
MotionPlannerNonvolatileCacheKey::Create(
    const object_world::ObjectWorld& object_world,
    const intrinsic_proto::motion_planning::v1::MotionSpecification&
        motion_specification,
    const intrinsic_proto::motion_planning::v1::RobotSpecification&
        robot_specification,
    absl::string_view uuid, absl::string_view mps_asset_major_version) {
  std::vector<absl::flat_hash_map<ObjectWorldResourceId, Pose3d>>
      poses_of_all_related_transform_nodes;
  for (const auto& motion_segment : motion_specification.motion_segments()) {
    poses_of_all_related_transform_nodes.push_back(
        absl::flat_hash_map<ObjectWorldResourceId, Pose3d>());
    INTR_RETURN_IF_ERROR(ExtractIdWithPoseFromMotionSegment(
        object_world, motion_segment,
        poses_of_all_related_transform_nodes.back()));
  }

  eigenmath::VectorXd starting_robot_configuration;
  if (robot_specification.has_start_configuration()) {
    // Extract the starting robot configuration.
    starting_robot_configuration = RepeatedDoubleToVectorXd(
        robot_specification.start_configuration().joints());
  } else {
    INTR_ASSIGN_OR_RETURN(
        const object_world::KinematicObject* robot,
        object_world::GetKinematicObjectByReference(
            object_world, robot_specification.robot_reference().object_id()));
    INTR_ASSIGN_OR_RETURN(starting_robot_configuration,
                          robot->GetJointPositions());
  }

  intrinsic_proto::motion_planning::v1::RobotSpecification
      robot_specification_proto = robot_specification;
  robot_specification_proto.clear_start_configuration();

  const google::protobuf::Descriptor* motion_specification_descriptor =
      motion_specification.GetDescriptor();
  const google::protobuf::FileDescriptor* motion_specification_file_descriptor =
      motion_specification_descriptor->file();

  return MotionPlannerNonvolatileCacheKey{
      .motion_specification = motion_specification,
      .robot_specification = robot_specification_proto,
      .poses_of_all_related_transform_nodes =
          poses_of_all_related_transform_nodes,
      .starting_robot_configuration = starting_robot_configuration,
      .uuid = std::string(uuid),
      .mps_asset_major_version = std::string(mps_asset_major_version),
      .motion_planning_proto_version =
          std::string(motion_specification_file_descriptor->package())};
};

bool MotionPlannerNonvolatileCacheKey::IsApproximate(
    const MotionPlannerNonvolatileCacheKey& other_key,
    const absl::flat_hash_set<uint32_t>& ignore_motion_segment_ids,
    double max_diff_in_rad_for_starting_robot_configuration,
    double max_position_diff_for_poses,
    double max_rotation_diff_for_poses) const {
  intrinsic::pb_equals pb_equals{};

  if (mps_asset_major_version != other_key.mps_asset_major_version) {
    LOG(WARNING)
        << "MotionPlannerService (MPS) Asset major version mismatch between "
        << uuid << " (Asset major version " << mps_asset_major_version
        << ") and " << other_key.uuid << " (Asset major version "
        << other_key.mps_asset_major_version << ").";
    // TODO(b/518007653): Re-consider whether an MPS Asset Major Version
    // difference shall trigger a Non-Volatile Cache-miss.
  }
  if (motion_planning_proto_version !=
      other_key.motion_planning_proto_version) {
    LOG(INFO) << "Motion Planning proto version mismatch between " << uuid
              << " (proto version " << motion_planning_proto_version << ") and "
              << other_key.uuid << " (proto version "
              << other_key.motion_planning_proto_version << ").";
    return false;
  }
  if (motion_specification.motion_segments_size() !=
      other_key.motion_specification.motion_segments_size()) {
    LOG(INFO) << "Motion segment size mismatch. " << uuid << " has "
              << motion_specification.motion_segments_size()
              << " motion segments, while " << other_key.uuid << " has "
              << other_key.motion_specification.motion_segments_size()
              << " motion segments";
    return false;
  }
  for (int i = 0; i < motion_specification.motion_segments_size(); ++i) {
    if (ignore_motion_segment_ids.contains(i)) {
      continue;
    }
    if (!pb_equals(motion_specification.motion_segments(i),
                   other_key.motion_specification.motion_segments(i))) {
      LOG(INFO) << "Motion segment " << i << " mismatch between " << uuid
                << " and " << other_key.uuid;
      return false;
    }
  }
  if (!pb_equals(motion_specification.curve_parameters(),
                 other_key.motion_specification.curve_parameters())) {
    LOG(INFO) << "Curve parameters mismatch between " << uuid << " and "
              << other_key.uuid;
    return false;
  }
  if (!pb_equals(robot_specification, other_key.robot_specification)) {
    LOG(INFO) << "Robot specification mismatch between " << uuid << " and "
              << other_key.uuid;
    return false;
  }

  // We only check the starting robot configuration if the first motion segment
  // is not ignored.
  if (!ignore_motion_segment_ids.contains(0)) {
    const double diff_in_rad_for_starting_robot_configuration =
        (starting_robot_configuration - other_key.starting_robot_configuration)
            .cwiseAbs()
            .maxCoeff();
    if (diff_in_rad_for_starting_robot_configuration >=
        max_diff_in_rad_for_starting_robot_configuration) {
      LOG(INFO) << "Difference in starting robot configuration is too large. "
                   "Actual:  "
                << diff_in_rad_for_starting_robot_configuration
                << " Expected: Less than "
                << max_diff_in_rad_for_starting_robot_configuration;
      return false;
    }
  }

  // Iterate over each segment. Two key must have the same number of segments
  // here since we checked it above.
  for (int i = 0; i < poses_of_all_related_transform_nodes.size(); ++i) {
    if (ignore_motion_segment_ids.contains(i)) {
      continue;
    }
    // Iterate over each transform node referred in that that segment.
    for (const auto& pose_with_id : poses_of_all_related_transform_nodes[i]) {
      const auto it = other_key.poses_of_all_related_transform_nodes[i].find(
          pose_with_id.first);
      if (it == other_key.poses_of_all_related_transform_nodes[i].end()) {
        LOG(INFO) << "Mismatch transform node ID: "
                  << pose_with_id.first.value() << " in motion segment " << i;
        return false;
      }
      if (!(pose_with_id.second.isApprox(it->second,
                                         max_position_diff_for_poses,
                                         max_rotation_diff_for_poses))) {
        const double diff_in_translation =
            (it->second.translation() - pose_with_id.second.translation())
                .norm();
        const double diff_in_rotation =
            1.0 - std::pow(it->second.quaternion().normalized().dot(
                               pose_with_id.second.quaternion().normalized()),
                           2);
        LOG(INFO) << "Mismatch pose for transform node ID: "
                  << pose_with_id.first.value() << " in motion segment " << i
                  << " Diff in translation: " << diff_in_translation
                  << " should be less than " << max_position_diff_for_poses
                  << " Diff in rotation: " << diff_in_rotation
                  << " should be less than " << max_rotation_diff_for_poses;
        return false;
      }
    }
  }

  return true;
}

intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheKey ToProto(
    const MotionPlannerNonvolatileCacheKey& key) {
  intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheKey proto;
  proto.set_uuid(key.uuid);
  proto.set_mps_asset_major_version(key.mps_asset_major_version);
  proto.set_motion_planning_proto_version(key.motion_planning_proto_version);
  *proto.mutable_motion_specification() = key.motion_specification;
  *proto.mutable_robot_specification() = key.robot_specification;
  for (const auto& poses_of_transform_nodes_per_motion_segment :
       key.poses_of_all_related_transform_nodes) {
    intrinsic_proto::motion_planning::
        MotionPlannerNonvolatileCacheKey_PosesOfTransformNodes*
            poses_of_transform_nodes_per_motion_segment_proto =
                proto.add_poses_of_all_related_transform_nodes();
    for (const auto& [id, pose] : poses_of_transform_nodes_per_motion_segment) {
      (*poses_of_transform_nodes_per_motion_segment_proto
            ->mutable_poses_of_transform_nodes())[id.value()] =
          intrinsic::ToProto(pose);
    }
  }
  VectorXdToRepeatedDouble(
      key.starting_robot_configuration,
      proto.mutable_starting_robot_configuration()->mutable_joints());
  return proto;
}

absl::StatusOr<MotionPlannerNonvolatileCacheKey> FromProto(
    const intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheKey&
        key_proto) {
  std::vector<absl::flat_hash_map<ObjectWorldResourceId, Pose3d>>
      poses_of_all_related_transform_nodes;
  poses_of_all_related_transform_nodes.reserve(
      key_proto.poses_of_all_related_transform_nodes_size());
  for (const auto& poses_of_transform_nodes_per_motion_segment_proto :
       key_proto.poses_of_all_related_transform_nodes()) {
    absl::flat_hash_map<ObjectWorldResourceId, Pose3d>
        poses_of_transform_nodes_per_motion_segment;
    for (const auto& [id, pose] :
         poses_of_transform_nodes_per_motion_segment_proto
             .poses_of_transform_nodes()) {
      INTR_ASSIGN_OR_RETURN(
          poses_of_transform_nodes_per_motion_segment[ObjectWorldResourceId(
              id)],
          intrinsic_proto::FromProto(pose));
    }
    poses_of_all_related_transform_nodes.push_back(
        poses_of_transform_nodes_per_motion_segment);
  }

  return MotionPlannerNonvolatileCacheKey{
      .motion_specification = key_proto.motion_specification(),
      .robot_specification = key_proto.robot_specification(),
      .poses_of_all_related_transform_nodes =
          poses_of_all_related_transform_nodes,
      .starting_robot_configuration = RepeatedDoubleToVectorXd(
          key_proto.starting_robot_configuration().joints()),
      .uuid = key_proto.uuid(),
      .mps_asset_major_version = key_proto.mps_asset_major_version(),
      .motion_planning_proto_version =
          key_proto.motion_planning_proto_version(),
  };
}

absl::StatusOr<
    intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheValue>
ToProto(const MotionPlannerNonvolatileCacheValue& value) {
  intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheValue proto;
  for (const PathSegment& path_segment : value.path_segments) {
    *proto.add_path_segments() = ToProto(path_segment);
  }
  INTR_ASSIGN_OR_RETURN(*proto.mutable_trajectory(), ToProto(value.trajectory));

  for (const intrinsic_proto::motion_planning::v1::MotionEvent& event :
       value.motion_events) {
    *proto.add_motion_events() = event;
  }

  return proto;
}

absl::StatusOr<MotionPlannerNonvolatileCacheValue> FromProto(
    const intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheValue&
        value_proto) {
  std::vector<PathSegment> path_segments;
  for (const auto& path_segment_proto : value_proto.path_segments()) {
    INTR_ASSIGN_OR_RETURN(const PathSegment path_segment,
                          FromProto(path_segment_proto));
    path_segments.push_back(path_segment);
  }
  INTR_ASSIGN_OR_RETURN(const JointTrajectoryPVA trajectory,
                        FromProto(value_proto.trajectory()));

  std::vector<intrinsic_proto::motion_planning::v1::MotionEvent> motion_events;
  for (const auto& motion_event : value_proto.motion_events()) {
    motion_events.push_back(motion_event);
  }

  return MotionPlannerNonvolatileCacheValue{
      .trajectory = trajectory,
      .path_segments = path_segments,

      .motion_events = motion_events,

  };
}

absl::StatusOr<
    intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheEntry>
ToProto(const MotionPlannerNonvolatileCacheEntry& entry) {
  intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheEntry proto;
  *proto.mutable_key() = ToProto(entry.key);
  INTR_ASSIGN_OR_RETURN(*proto.mutable_value(), ToProto(entry.value));
  return proto;
}

absl::StatusOr<MotionPlannerNonvolatileCacheEntry> FromProto(
    const intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheEntry&
        entry_proto) {
  INTR_ASSIGN_OR_RETURN(const MotionPlannerNonvolatileCacheKey key,
                        FromProto(entry_proto.key()));
  INTR_ASSIGN_OR_RETURN(const MotionPlannerNonvolatileCacheValue value,
                        FromProto(entry_proto.value()));
  return MotionPlannerNonvolatileCacheEntry{key, value};
}

absl::StatusOr<std::unique_ptr<MotionPlannerNonvolatileCache>>
MotionPlannerNonvolatileCache::CreateWithContentAddressableStorage(
    int max_num_of_volatile_cache_entries,
    absl::string_view cas_service_address) {
  if (max_num_of_volatile_cache_entries <= 0) {
    return absl::InvalidArgumentError(
        "max_num_of_volatile_cache_entries must be positive.");
  }

  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<grpc::Channel> channel,
      connect::CreateClientChannel(
          cas_service_address,
          absl::Now() + connect::kGrpcClientConnectDefaultTimeout,
          connect::UnlimitedMessageSizeGrpcChannelArgs()));

  std::unique_ptr<ContentAddressableStorageService::Stub> cas_service_stub_ =
      ContentAddressableStorageService::NewStub(std::move(channel));
  if (!cas_service_stub_) {
    return absl::InternalError("Couldn't create stub for the CAS service.");
  }

  return absl::WrapUnique(new MotionPlannerNonvolatileCache(
      max_num_of_volatile_cache_entries, std::move(cas_service_stub_)));
}

void MotionPlannerNonvolatileCache::ClearVolatileCache() {
  absl::MutexLock lock(&mutex_);
  volatile_cache_.clear();
}

absl::StatusOr<std::string> MotionPlannerNonvolatileCache::Insert(
    const MotionPlannerNonvolatileCacheEntry& entry) {
  INTR_ASSIGN_OR_RETURN(const std::string returned_uuid,
                        SaveToNonvolatileCache(entry));

  {
    absl::MutexLock lock(&mutex_);
    volatile_cache_.insert(returned_uuid,
                           new MotionPlannerNonvolatileCacheEntry(entry),
                           /*units=*/1);
  }
  LOG(INFO) << "Successfully created a new volatile cache entry for "
            << returned_uuid;
  return returned_uuid;
}

absl::StatusOr<std::optional<MotionPlannerNonvolatileCacheEntry>>
MotionPlannerNonvolatileCache::Lookup(
    const MotionPlannerNonvolatileCacheKey& key) {
  // First lookup in the volatile cache.
  {
    absl::MutexLock lock(&mutex_);
    VolatileCacheType::ScopedLookup lookup(&volatile_cache_, key.uuid);
    if (lookup.found()) {
      LOG(INFO) << "Found a volatile cache entry for " << key.uuid;
      return *lookup.value();
    }
  }

  // Then lookup in the nonvolatile cache.
  INTR_ASSIGN_OR_RETURN(std::optional<MotionPlannerNonvolatileCacheEntry> entry,
                        LoadFromNonvolatileCache(key));
  if (entry.has_value()) {
    LOG(INFO) << "Found a nonvolatile cache entry for " << key.uuid;
  } else {
    LOG(INFO) << "Failed to find a nonvolatile cache entry for " << key.uuid;
    return std::nullopt;
  }

  // Save the entry in the volatile cache so it is faster look up next time.
  {
    absl::MutexLock lock(&mutex_);
    volatile_cache_.insert(key.uuid,
                           new MotionPlannerNonvolatileCacheEntry(*entry),
                           /*units=*/1);
    LOG(INFO) << "Successfully created a new volatile cache entry for "
              << key.uuid;
  }

  // Return the entry from the nonvolatile cache.
  return std::move(entry);
}

absl::StatusOr<std::string>
MotionPlannerNonvolatileCache::SaveToNonvolatileCache(
    const MotionPlannerNonvolatileCacheEntry& entry) {
  if (cas_service_stub_ != nullptr) {
    std::string proto_contents;
    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheEntry
            proto,
        ToProto(entry));
    if (!proto.SerializeToString(&proto_contents)) {
      return absl::InternalError("Failed to serialize cache entry proto.");
    }
    grpc::ClientContext cas_context;
    INTR_ASSIGN_OR_RETURN(
        const std::string uuid,
        ContentAddressableStorageCreate(&cas_context, cas_service_stub_.get(),
                                        proto_contents));
    LOG(INFO) << "Successfully created a new CAS entry with uuid " << uuid;
    // For CAS, the returned uuid is from the CAS service.
    return uuid;
  }

  return absl::InternalError("No nonvolatile cache configured.");
}

absl::StatusOr<std::optional<MotionPlannerNonvolatileCacheEntry>>
MotionPlannerNonvolatileCache::LoadFromNonvolatileCache(
    const MotionPlannerNonvolatileCacheKey& key) {
  if (cas_service_stub_ != nullptr) {
    grpc::ClientContext cas_context;
    LOG(INFO) << "Looking up a CAS entry for " << key.uuid;
    const absl::StatusOr<std::string> status_or_proto_string =
        ContentAddressableStorageGet(&cas_context, cas_service_stub_.get(),
                                     key.uuid);
    if (!status_or_proto_string.ok()) {
      LOG(INFO) << "Failed to find a CAS entry for " << key.uuid << ": "
                << status_or_proto_string.status();
      return std::nullopt;
    }
    LOG(INFO) << "Successfully retrieved a CAS entry for " << key.uuid;
    intrinsic_proto::motion_planning::MotionPlannerNonvolatileCacheEntry proto;
    if (!proto.ParseFromString(status_or_proto_string.value())) {
      return absl::InternalError("Failed to parse cache entry proto from CAS.");
    }
    INTR_ASSIGN_OR_RETURN(MotionPlannerNonvolatileCacheEntry entry,
                          FromProto(proto));
    return std::move(entry);
  }

  return absl::InternalError("No nonvolatile cache configured.");
}
}  // namespace intrinsic
