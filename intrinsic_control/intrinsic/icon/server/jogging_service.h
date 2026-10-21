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

#ifndef INTRINSIC_ICON_SERVER_JOGGING_SERVICE_H_
#define INTRINSIC_ICON_SERVER_JOGGING_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/sync_stream.h"
#include "grpcpp/support/time.h"
#include "intrinsic/icon/actions/joint_jogging_info.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/v1/jogging_service.grpc.pb.h"
#include "intrinsic/icon/proto/v1/jogging_service.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/objects/frame.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"

namespace intrinsic::icon {

// This class implements the ICON jogging service which provides a gRPC service
// for jogging a robot with a simplified interface.
// A dummy service is started when no world service is available.
class IconJoggingService
    : public ::intrinsic_proto::icon::v1::JoggingService::Service {
 public:
  // Tracks cancellable grpc stream `context` during its lifetime.
  class ScopedCancellableGrpcContext {
   public:
    ScopedCancellableGrpcContext(IconJoggingService& service,
                                 ::grpc::ServerContext* context);

    ~ScopedCancellableGrpcContext();

    ScopedCancellableGrpcContext(const ScopedCancellableGrpcContext&) = delete;
    ScopedCancellableGrpcContext& operator=(
        const ScopedCancellableGrpcContext&) = delete;

   private:
    IconJoggingService& service_;
    ::grpc::ServerContext* context_;
    std::shared_ptr<absl::Notification> shutdown_notification_;
  };

  ~IconJoggingService() override;

  // Configuration for a part that supports jogging. This information is assumed
  // constant for the lifetime of the service. This does not include the
  // jogging frames which are calculated on demand since the world may be
  // altered.
  struct PartJoggingConfig {
    int num_dof = 0;
    std::vector<std::string> dof_names;
    bool cartesian_jogging_available = false;
    std::string kinematic_object_name;
    std::string kinematic_object_full_path;
    JointLimits joint_limits;
    std::optional<CartesianLimits> cartesian_limits;
  };

  ::grpc::Status GetAvailableParts(
      ::grpc::ServerContext* context,
      const ::intrinsic_proto::icon::v1::AvailablePartsRequest* request,
      ::intrinsic_proto::icon::v1::AvailablePartsResponse* response) override;

  ::grpc::Status JogRobot(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<::intrinsic_proto::icon::v1::JoggingResponse,
                                 ::intrinsic_proto::icon::v1::JoggingRequest>*
          stream) override;

  // Creates an `IconJoggingService` instance.
  //
  // Inspects the ICON server via `icon_channel` and the world model via
  // `object_world_client` to discover and configure parts available for joint
  // and Cartesian jogging.
  //
  // If `object_world_client` is `nullptr`, a dummy jogging service instance is
  // returned which reports no available parts and rejects jogging commands.
  //
  // Returns:
  // - FailedPreconditionError: If a part is missing a hardware resource name or
  //   has no joint entity IDs.
  // - InvalidArgumentError: If a world object lacks a kinematic object
  // component
  //   or limit proto conversion fails.
  // - InternalError: If clamped Cartesian limits are invalid.
  static absl::StatusOr<
      std::unique_ptr<::intrinsic_proto::icon::v1::JoggingService::Service>>
  Create(std::shared_ptr<intrinsic::ChannelInterface> icon_channel,
         std::shared_ptr<const world::ObjectWorldClient> object_world_client);

 private:
  // The jogging frames available for a part.
  struct AvailableJoggingFrames {
    std::vector<intrinsic_proto::world::FrameReference> static_jogging_frames;
    std::vector<intrinsic_proto::world::FrameReference> tool_jogging_frames;
  };

  explicit IconJoggingService(
      std::shared_ptr<intrinsic::ChannelInterface> icon_channel,
      std::unique_ptr<icon::Client> icon_client,
      std::shared_ptr<const world::ObjectWorldClient> object_world_client,
      absl::flat_hash_map<std::string, PartJoggingConfig> parts);

  absl::Status JogRobotImpl(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<::intrinsic_proto::icon::v1::JoggingResponse,
                                 ::intrinsic_proto::icon::v1::JoggingRequest>*
          stream);

  absl::Status RunJointJogging(
      ::grpc::ServerContext* context,
      ::grpc::ServerReaderWriter<::intrinsic_proto::icon::v1::JoggingResponse,
                                 ::intrinsic_proto::icon::v1::JoggingRequest>*
          stream,
      const std::string& part_name,
      const JointJoggingInfo::FixedParams& fixed_params);

  absl::StatusOr<AvailableJoggingFrames> GetValidJoggingFrames(
      absl::string_view arm_part_name);

  absl::StatusOr<std::vector<std::string>> GetPartOrderByKinematicPath();

  absl::StatusOr<world::Frame> GetFrameIfAvailable(
      const intrinsic_proto::world::FrameReference& frame_reference,
      const std::vector<intrinsic_proto::world::FrameReference>&
          available_frames,
      std::string frame_type_for_error_message);

  ScopedCancellableGrpcContext RegisterCancellableGrpcStream(
      ::grpc::ServerContext* context);

  bool HasNoOpenStreams() const;

  // Returns a `shared_ptr` to a `Notification` that fires when the
  // `JoggingService` gets destroyed.
  //
  // `ScopedCancellableGrpcContext` may outlive the `JoggingService`,
  // so this must be a shared_ptr so that any surviving
  // `ScopedCancellableGrpcContext`s can still read it after the
  // `JoggingService`'s death.
  std::shared_ptr<absl::Notification> GetShutdownNotification() {
    return shutdown_notification_;
  }

  std::shared_ptr<intrinsic::ChannelInterface> icon_channel_;
  std::unique_ptr<icon::Client> icon_client_;
  std::shared_ptr<const world::ObjectWorldClient> object_world_client_;
  const absl::flat_hash_map<std::string, PartJoggingConfig> parts_;

  absl::Mutex open_streams_mutex_;
  absl::flat_hash_set<::grpc::ServerContext*> open_grpc_streams_
      ABSL_GUARDED_BY(open_streams_mutex_);
  std::shared_ptr<absl::Notification> shutdown_notification_;
};

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_SERVER_JOGGING_SERVICE_H_
