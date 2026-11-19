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

#ifndef INTRINSIC_WORLD_SERVICE_UPDATER_WORLD_UPDATER_H_
#define INTRINSIC_WORLD_SERVICE_UPDATER_WORLD_UPDATER_H_

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/utils/async_buffer.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/math/proto/transform_stamped.pb.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/proto/v1/pubsub_world_updates.pb.h"
#include "intrinsic/world/pubsub/tf_echo_guard_interfaces.h"
#include "intrinsic/world/service/updater/world_updater.grpc.pb.h"
#include "intrinsic/world/service/updater/world_updater.pb.h"
#include "intrinsic/world/service/updater/world_updater_config.pb.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/util/tf_frame_util.h"
#include "third_party/ros2/ros_interfaces/jazzy/sensor_msgs/msg/joint_state.pb.h"

namespace intrinsic {

class WorldUpdater : public intrinsic_proto::world::WorldUpdater::Service {
 public:
  struct Options {
    Options(std::string belief_world_id, std::string sim_world_id,
            std::shared_future<std::unique_ptr<intrinsic::WorldStorage>>
                world_storage,
            resources::ResourceRegistryClientInterface* absl_nonnull
                resource_registry_client,
            icon::ChannelFactory* absl_nonnull icon_channel_factory,
            TfEchoGuardSubscriberInterface* absl_nonnull tf_echo_guard)
        : belief_world_id(std::move(belief_world_id)),
          sim_world_id(std::move(sim_world_id)),
          world_storage(std::move(world_storage)),
          resource_registry_client(resource_registry_client),
          icon_channel_factory(icon_channel_factory),
          tf_echo_guard(tf_echo_guard) {}

    Options() = delete;

    std::string belief_world_id;
    // TODO(b/521823269): Use world_id from WorldUpdater::Reset instead of hard
    // coded option.
    std::string sim_world_id;
    std::shared_future<std::unique_ptr<intrinsic::WorldStorage>> world_storage;
    resources::ResourceRegistryClientInterface* resource_registry_client;
    icon::ChannelFactory* icon_channel_factory;
    TfEchoGuardSubscriberInterface* tf_echo_guard;
    absl::Duration reconfigure_interval = absl::Seconds(3);
    absl::Duration idle_subscription_timeout = absl::Seconds(3);
    absl::Duration world_update_interval = absl::Milliseconds(30);
  };

  static absl::StatusOr<std::unique_ptr<WorldUpdater>> Create(Options options);

  // Do not allow copying.
  WorldUpdater(const WorldUpdater&) = delete;
  // Do not allow moving because the `this` pointer is used when registering
  // callbacks with the pubsub system.
  WorldUpdater(WorldUpdater&&) = delete;
  ~WorldUpdater() override;

  void Shutdown();

  void ReconfigureFromResourceRegistry() ABSL_LOCKS_EXCLUDED(subscriptions_mu_);

  absl::Status ReconfigureFromResourceRegistryOnce()
      ABSL_LOCKS_EXCLUDED(subscriptions_mu_);

  absl::Status AddTfSubscription() ABSL_LOCKS_EXCLUDED(subscriptions_mu_);
  absl::Status AddSimWorldPubsubUpdatesSubscription()
      ABSL_LOCKS_EXCLUDED(subscriptions_mu_);

  grpc::Status Reset(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ResetUpdaterRequest* request,
      intrinsic_proto::world::ResetUpdaterResponse* response) override;

  grpc::Status Pause(
      grpc::ServerContext* context,
      const intrinsic_proto::world::PauseUpdaterRequest* request,
      intrinsic_proto::world::PauseUpdaterResponse* response) override;

  grpc::Status Resume(
      grpc::ServerContext* context,
      const intrinsic_proto::world::ResumeUpdaterRequest* request,
      intrinsic_proto::world::ResumeUpdaterResponse* response) override;

  size_t NumRobotSubscriptions() ABSL_LOCKS_EXCLUDED(subscriptions_mu_) {
    absl::MutexLock l(subscriptions_mu_);
    return robot_subscriptions_.size();
  }

  size_t NumJointStateSubscriptions() ABSL_LOCKS_EXCLUDED(subscriptions_mu_) {
    absl::MutexLock l(subscriptions_mu_);
    return ros_joint_state_subscriptions_.size();
  }

  struct JointStateSubscriptionConfig {
    std::string resource_name;
    std::string object_name;
  };

  struct RosJointStateSubscription {
    std::string resource_name;
    std::string object_name;

    absl::Time last_update = absl::InfinitePast();

    absl::Status HandleJointState(
        const sensor_msgs::msg::pb::jazzy::JointState& joint_state);

    using JointUpdate = sensor_msgs::msg::pb::jazzy::JointState;
    AsyncBuffer<JointUpdate> update_buffer;
    intrinsic::Subscription subscription;

    static absl::StatusOr<std::unique_ptr<RosJointStateSubscription>> Create(
        const JointStateSubscriptionConfig& config, intrinsic::PubSub* pubsub);
  };

 protected:
  explicit WorldUpdater(Options options);

  // Add a robot subscription based on the given config.
  absl::Status AddRobotSubscription(
      const intrinsic_proto::world::IconRobotSubscriptionConfig& config)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(subscriptions_mu_);

  absl::Status ReconfigureRobotSubscriptions()
      ABSL_LOCKS_EXCLUDED(subscriptions_mu_);

  absl::Status ReconfigureJointStateSubscriptions()
      ABSL_LOCKS_EXCLUDED(subscriptions_mu_);

  absl::Status UpdateWorldOnce();
  absl::Status UpdateRobotsOnce();
  absl::Status UpdateJointStatesOnce();
  absl::Status UpdateTransformsOnce();
  absl::Status UpdateSimWorldOnce();

  void UpdateWorld();

 private:
  // Applies the `joint_state` update to a kinematic object in `ow` if the
  // `update_stamp` is newer than the object's current state.
  //
  // Returns true if the update was applied, and false if it was ignored (e.g.,
  // if it is older or the target object is not found).
  absl::StatusOr<bool> ApplyJointStateUpdate(
      object_world::ObjectWorld& ow,
      const sensor_msgs::msg::pb::jazzy::JointState& joint_state,
      absl::Time update_stamp,
      std::optional<std::string_view> target_object_name = std::nullopt);

  // Applies a list of `tf_updates` to `ow`.
  //
  // Sorts the updates chronologically and applies them. Returns a map of child
  // frame IDs to timestamps for successfully applied updates.
  absl::StatusOr<absl::flat_hash_map<std::string, google::protobuf::Timestamp>>
  ApplyTfUpdates(object_world::ObjectWorld& ow,
                 std::vector<intrinsic_proto::TransformStamped> tf_updates);

  absl::StatusOr<absl::flat_hash_map<std::string, google::protobuf::Timestamp>>
  ApplyTfUpdates(object_world::ObjectWorld& ow,
                 const intrinsic_proto::TFMessage& tf_message);

  absl::StatusOr<absl::Time> ApplyPubsubWorldUpdate(
      object_world::ObjectWorld& ow,
      const intrinsic_proto::world::v1::PubsubWorldUpdate& update);

  std::string belief_world_id_;
  std::string sim_world_id_;
  intrinsic::PubSub pubsub_;

  std::shared_future<std::unique_ptr<intrinsic::WorldStorage>>
      world_storage_future_;
  WorldStorage* GetWorldStorage();

  resources::ResourceRegistryClientInterface* resource_registry_client_;

  icon::ChannelFactory* icon_channel_factory_;

  absl::Mutex paused_mu_;
  bool paused_ ABSL_GUARDED_BY(paused_mu_);

  absl::Notification stop_;
  intrinsic::Thread update_thread_;
  intrinsic::Thread reconfigure_thread_;
  intrinsic::Thread subscription_monitor_;

  struct RobotSubscription {
    std::string resource_name;
    std::string part_name;
    std::string object_name;

    // Acquisition time of the last sample we received.
    absl::Time last_update = absl::InfinitePast();

    absl::Status HandleIconStatus(
        const intrinsic_proto::data_logger::LogItem& robot_status);
    using RobotUpdate = std::pair<absl::Time, eigenmath::VectorXd>;
    std::atomic_bool stale = false;
    AsyncBuffer<RobotUpdate> update_buffer;
    // As long as this stays in scope, it can reference other members of this
    // struct (via the `HandleIconStatus()` member function that it uses as a
    // callback).
    // Because of that, it needs to go after all other members, so it gets
    // destroyed *first* when cleaning up this `RobotSubscription`.
    intrinsic::Subscription subscription;

    // Returns the latest sample to the consumer. This sample is guaranteed not
    // to be modified until the next call to GetLatestSample() by the consumer.
    // This may be the same sample which was returned to the last call to
    // GetLatestSample(). Returns true if a new sample was available and the
    // returned pointer points to it. Returns false if there was no new sample
    // and the returned pointer points to the sample that was already active at
    // the time of this call.
    bool GetLatestSample(RobotUpdate** update);

    static absl::StatusOr<std::unique_ptr<RobotSubscription>> Create(
        const intrinsic_proto::world::IconRobotSubscriptionConfig& config,
        intrinsic::PubSub* pubsub);
  };
  std::vector<std::unique_ptr<RobotSubscription>> robot_subscriptions_
      ABSL_GUARDED_BY(subscriptions_mu_);

  std::vector<std::unique_ptr<RosJointStateSubscription>>
      ros_joint_state_subscriptions_ ABSL_GUARDED_BY(subscriptions_mu_);

  struct PubsubWorldUpdatesSubscription {
    std::string world_id;
    std::string topic_name;

    absl::Status HandleWorldUpdates(
        const intrinsic_proto::world::v1::PubsubWorldUpdates& updates);

    absl::Mutex updates_mu;
    std::vector<intrinsic_proto::world::v1::PubsubWorldUpdate> pending_updates
        ABSL_GUARDED_BY(updates_mu);

    intrinsic::Subscription subscription;
    std::atomic_bool paused = false;

    static absl::StatusOr<std::unique_ptr<PubsubWorldUpdatesSubscription>>
    Create(absl::string_view world_id, intrinsic::PubSub& pubsub);
  };
  std::unique_ptr<PubsubWorldUpdatesSubscription>
      sim_world_updates_subscription_ ABSL_GUARDED_BY(subscriptions_mu_);

  struct TfSubscription {
    absl::Status HandleTfMessage(const intrinsic_proto::TFMessage& tf_message);

    absl::Mutex updates_mu;
    std::vector<intrinsic_proto::TransformStamped> pending_updates
        ABSL_GUARDED_BY(updates_mu);

    intrinsic::Subscription subscription;

    std::atomic_bool paused = false;

    static absl::StatusOr<std::unique_ptr<TfSubscription>> Create(
        intrinsic::PubSub& pubsub);
  };
  std::unique_ptr<TfSubscription> tf_subscription_
      ABSL_GUARDED_BY(subscriptions_mu_);

  TfEchoGuardSubscriberInterface* tf_echo_guard_;

  const absl::Duration reconfigure_interval_;
  const absl::Duration idle_subscription_timeout_;
  const absl::Duration world_update_interval_;

  absl::Mutex subscriptions_mu_ ABSL_ACQUIRED_AFTER(reconfigure_mu_);
  absl::Mutex reconfigure_mu_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_UPDATER_WORLD_UPDATER_H_
