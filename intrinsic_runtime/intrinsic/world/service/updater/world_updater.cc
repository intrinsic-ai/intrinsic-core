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

#include "intrinsic/world/service/updater/world_updater.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/call_once.h"
#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/util/time_util.h"
#include "grpcpp/server_context.h"
#include "grpcpp/support/status.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/common/topic_names.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/logging/proto/log_item.pb.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/math/tf2_convert_intrinsic.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/resources/proto/resource_registry.pb.h"
#include "intrinsic/skills/cc/skill_utils.h"
#include "intrinsic/stats/metrics_utils.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/ret_check.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/status/status_macros_grpc.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/component/kinematics_component.h"
#include "intrinsic/world/objects/kinematic_object_internal.h"
#include "intrinsic/world/objects/object_world.h"
#include "intrinsic/world/objects/world_object_internal.h"
#include "intrinsic/world/print_world.h"
#include "intrinsic/world/service/objects/object_world_updates_utils.h"
#include "intrinsic/world/service/updater/world_updater.pb.h"
#include "intrinsic/world/service/updater/world_updater_config.pb.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/util/ros_joint_state_util.h"
#include "intrinsic/world/util/tf_frame_util.h"
#include "opentelemetry/common/attribute_value.h"
#include "opentelemetry/metrics/sync_instruments.h"

namespace intrinsic {

using ::intrinsic_proto::data_logger::LogItem;
using ::intrinsic_proto::world::IconRobotSubscriptionConfig;
using ::intrinsic_proto::world::v1::PubsubWorldUpdate;
using ::intrinsic_proto::world::v1::PubsubWorldUpdates;

namespace {
constexpr absl::string_view kSampleDeltaTMeasureName =
    "intrinsic/world_updater/sample_delta_t";
// Instruments in this file are intentionally released so they remain alive
// for the program's lifetime, avoiding global destructor ordering errors on
// service destruction. Each is a one-time allocation, so this doesn't leak.
opentelemetry::metrics::Histogram<uint64_t>& SampleDeltaT() {
  static opentelemetry::metrics::Histogram<uint64_t>* histogram =
      stats::GetMeter()
          ->CreateUInt64Histogram(
              kSampleDeltaTMeasureName,
              "Wall clock time difference between the newest received ICON "
              "status and the previous one.",
              "us")
          .release();
  return *histogram;
}

constexpr absl::string_view kSampleLatencyMeasureName =
    "intrinsic/world_updater/sample_latency";
opentelemetry::metrics::Histogram<uint64_t>& SampleLatency() {
  static opentelemetry::metrics::Histogram<uint64_t>* histogram =
      stats::GetMeter()
          ->CreateUInt64Histogram(
              kSampleLatencyMeasureName,
              "Wall clock time difference between the newest received ICON "
              "status acquisition time and the receipt time in the updater.",
              "us")
          .release();
  return *histogram;
}

constexpr absl::string_view kDelayedSampleRejectCountMeasureName =
    "intrinsic/world_updater/delayed_samples";
opentelemetry::metrics::Counter<uint64_t>& DelayedSampleRejectCount() {
  static opentelemetry::metrics::Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(
              kDelayedSampleRejectCountMeasureName,
              "Samples that were rejected due to arriving later than another "
              "sample.",
              "")
          .release();
  return *counter;
}

constexpr absl::string_view kAcceptedSampleCountMeasureName =
    "intrinsic/world_updater/accepted_samples";
opentelemetry::metrics::Counter<uint64_t>& AcceptedSampleCount() {
  static opentelemetry::metrics::Counter<uint64_t>* counter =
      stats::GetMeter()
          ->CreateUInt64Counter(
              kAcceptedSampleCountMeasureName,
              "Samples that were accepted by the world service.", "")
          .release();
  return *counter;
}

constexpr absl::string_view kResourceNameKey = "resource_name";

void RegisterMetrics() {
  static absl::once_flag called;
  absl::call_once(called, []() {
    stats::RegisterHistogramView(
        kSampleDeltaTMeasureName,
        /*view_name=*/absl::StrCat(kSampleDeltaTMeasureName, "/distribution"),
        stats::LinearBucketBoundaries(10, 300, 100));
    stats::RegisterHistogramView(
        kSampleLatencyMeasureName,
        /*view_name=*/absl::StrCat(kSampleLatencyMeasureName, "/distribution"),
        stats::LinearBucketBoundaries(10, 300, 100));
    // Eagerly initialize the instruments to register them. The counters only
    // record counts > 0, so add 0 to export their time series from the start.
    DelayedSampleRejectCount().Add(0);
    AcceptedSampleCount().Add(0);
    SampleDeltaT();
    SampleLatency();
  });
}

absl::StatusOr<absl::Time> GetKinematicObjectTimestamp(
    const object_world::KinematicObject& ko) {
  INTR_ASSIGN_OR_RETURN(const std::vector<JointEntityId> joint_ids,
                        ko.GetJointEntityIds());
  const World& world = ko.GetEntityWorld();
  absl::Time latest_timestamp = absl::InfinitePast();
  for (JointEntityId joint_id : joint_ids) {
    INTR_ASSIGN_OR_RETURN(
        const KinematicsComponent* kinematics,
        world.GetComponentByEntityId<KinematicsComponent>(joint_id));
    auto timestamp = kinematics->GetTimestamp();
    if (timestamp.has_value() && *timestamp > latest_timestamp) {
      latest_timestamp = *timestamp;
    }
  }
  return latest_timestamp;
}

absl::StatusOr<object_world::KinematicObject*> FindKinematicObjectForJointState(
    object_world::ObjectWorld& ow,
    const sensor_msgs::msg::pb::jazzy::JointState& joint_state) {
  if (!joint_state.header().frame_id().empty()) {
    if (auto ko = ow.GetKinematicObject(
            WorldObjectName(joint_state.header().frame_id()));
        ko.ok()) {
      return *ko;
    }
  }

  return nullptr;
}

absl::Time GetPubsubWorldUpdateTimestamp(const PubsubWorldUpdate& update) {
  if (update.has_joint_state()) {
    return ToAbslTime(update.joint_state().header().stamp())
        .value_or(absl::InfinitePast());
  } else if (update.has_transform()) {
    if (update.transform().transforms_size() > 0) {
      return ToAbslTime(update.transform().transforms(0).header().stamp())
          .value_or(absl::InfinitePast());
    }
  }
  return absl::InfinitePast();
}

void SortPubsubWorldUpdatesByTime(std::vector<PubsubWorldUpdate>& updates) {
  absl::c_sort(updates, [](const PubsubWorldUpdate& l,
                           const PubsubWorldUpdate& r) {
    return GetPubsubWorldUpdateTimestamp(l) < GetPubsubWorldUpdateTimestamp(r);
  });
}

}  // namespace

absl::StatusOr<std::unique_ptr<WorldUpdater>> WorldUpdater::Create(
    Options options) {
  if (options.tf_echo_guard == nullptr) {
    return absl::InvalidArgumentError(
        "Options.tf_echo_guard must be provided.");
  }
  if (options.resource_registry_client == nullptr) {
    return absl::InvalidArgumentError(
        "Options.resource_registry_client must be provided.");
  }
  if (options.icon_channel_factory == nullptr) {
    return absl::InvalidArgumentError(
        "Options.icon_channel_factory must be provided.");
  }
  if (options.belief_world_id.empty()) {
    return absl::InvalidArgumentError(
        "Options.belief_world_id must not be empty.");
  }
  if (options.sim_world_id.empty()) {
    return absl::InvalidArgumentError(
        "Options.sim_world_id must not be empty.");
  }

  auto updater = absl::WrapUnique(new WorldUpdater(std::move(options)));

  RegisterMetrics();

  INTR_RETURN_IF_ERROR(updater->AddTfSubscription());
  INTR_RETURN_IF_ERROR(updater->AddSimWorldPubsubUpdatesSubscription());

  updater->update_thread_ =
      Thread([updater = updater.get()]() { updater->UpdateWorld(); });

  updater->reconfigure_thread_ = Thread([updater = updater.get()]() {
    updater->ReconfigureFromResourceRegistry();
  });

  updater->subscription_monitor_ = Thread([updater = updater.get()]() {
    do {
      absl::MutexLock l(updater->subscriptions_mu_);
      std::vector<std::unique_ptr<RobotSubscription>>& subs =
          updater->robot_subscriptions_;
      int i = 0;
      while (i < subs.size()) {
        if (absl::Now() - subs[i]->last_update >
            updater->idle_subscription_timeout_) {
          LOG(WARNING) << "Did not receive update from '"
                       << subs[i]->resource_name << "' for over "
                       << updater->idle_subscription_timeout_
                       << ". Removing subscription.";
          subs.erase(subs.begin() + i);
        } else {
          ++i;
        }
      }
    } while (!updater->stop_.WaitForNotificationWithTimeout(
        updater->idle_subscription_timeout_));
  });

  return std::move(updater);
}

WorldUpdater::WorldUpdater(Options options)
    : belief_world_id_(std::move(options.belief_world_id)),
      sim_world_id_(std::move(options.sim_world_id)),
      world_storage_future_(std::move(options.world_storage)),
      resource_registry_client_(options.resource_registry_client),
      icon_channel_factory_(options.icon_channel_factory),
      paused_(false),
      tf_echo_guard_(options.tf_echo_guard),
      reconfigure_interval_(options.reconfigure_interval),
      idle_subscription_timeout_(options.idle_subscription_timeout),
      world_update_interval_(options.world_update_interval) {}

WorldUpdater::~WorldUpdater() {
  stop_.Notify();

  // Threads may not have been started if we failed to create the updater.
  if (update_thread_.joinable()) {
    update_thread_.join();
  }
  if (reconfigure_thread_.joinable()) {
    reconfigure_thread_.join();
  }
  if (subscription_monitor_.joinable()) {
    subscription_monitor_.join();
  }
}

void WorldUpdater::Shutdown() { stop_.Notify(); }

grpc::Status WorldUpdater::Reset(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ResetUpdaterRequest* request,
    intrinsic_proto::world::ResetUpdaterResponse* response) {
  absl::MutexLock lock(subscriptions_mu_);
  robot_subscriptions_.clear();
  INTR_ASSIGN_OR_RETURN_GRPC(tf_subscription_, TfSubscription::Create(pubsub_));

  if (request->has_belief_world_options()) {
    belief_world_id_ = request->belief_world_options().world_id();
  }
  if (request->has_sim_world_options()) {
    sim_world_id_ = request->sim_world_options().world_id();
    // TODO(b/521816693): Setup SimulationSubscription here.
  }
  INTR_ASSIGN_OR_RETURN_GRPC(
      sim_world_updates_subscription_,
      PubsubWorldUpdatesSubscription::Create(sim_world_id_, pubsub_));
  response->set_sim_world_updates_topic_name(
      sim_world_updates_subscription_->topic_name);
  return grpc::Status::OK;
}

grpc::Status WorldUpdater::Pause(
    grpc::ServerContext* context,
    const intrinsic_proto::world::PauseUpdaterRequest* request,
    intrinsic_proto::world::PauseUpdaterResponse* response) {
  absl::MutexLock l(paused_mu_);
  if (!paused_) {
    LOG(INFO) << "Pausing updates to the world service";
    paused_ = true;
    absl::MutexLock subscriptions_lock(subscriptions_mu_);
    tf_subscription_->paused = true;
    if (sim_world_updates_subscription_ != nullptr) {
      sim_world_updates_subscription_->paused = true;
    }
  }

  return grpc::Status::OK;
}

grpc::Status WorldUpdater::Resume(
    grpc::ServerContext* context,
    const intrinsic_proto::world::ResumeUpdaterRequest* request,
    intrinsic_proto::world::ResumeUpdaterResponse* response) {
  absl::MutexLock l(paused_mu_);
  if (paused_) {
    // Mark each of the following subscriptions as stale to avoid updating the
    // world service with an update we received while we were paused. We use
    // the atomic flag here instead of committing an empty buffer because this
    // RPC will be called from a different thread than the robot subscription.
    absl::MutexLock subscriptions_lock(subscriptions_mu_);
    for (std::unique_ptr<RobotSubscription>& sub : robot_subscriptions_) {
      sub->stale = true;
    }

    // For TF subscription, we need to clear updates received before the pause
    // to avoid updating the world with a stale state.
    {
      absl::MutexLock tf_pending_updates_lock(tf_subscription_->updates_mu);
      tf_subscription_->pending_updates.clear();
      tf_subscription_->paused = false;
    }
    if (sim_world_updates_subscription_ != nullptr) {
      absl::MutexLock updates_lock(sim_world_updates_subscription_->updates_mu);
      sim_world_updates_subscription_->pending_updates.clear();
      sim_world_updates_subscription_->paused = false;
    }

    LOG(INFO) << "Resuming updates to the world service";
    paused_ = false;
  }
  return grpc::Status::OK;
}

absl::Status WorldUpdater::AddRobotSubscription(
    const IconRobotSubscriptionConfig& config) {
  LOG(INFO) << "Setting up new subscription for resource '"
            << config.equipment_name() << "' part '" << config.part_name()
            << "'";
  INTR_ASSIGN_OR_RETURN(auto subscription,
                        RobotSubscription::Create(config, &pubsub_));
  robot_subscriptions_.emplace_back(std::move(subscription));

  return absl::OkStatus();
}

absl::Status WorldUpdater::AddTfSubscription() {
  absl::MutexLock l(subscriptions_mu_);
  INTR_ASSIGN_OR_RETURN(tf_subscription_, TfSubscription::Create(pubsub_));
  return absl::OkStatus();
}

absl::Status WorldUpdater::AddSimWorldPubsubUpdatesSubscription() {
  absl::MutexLock l(subscriptions_mu_);
  INTR_ASSIGN_OR_RETURN(
      sim_world_updates_subscription_,
      PubsubWorldUpdatesSubscription::Create(sim_world_id_, pubsub_));
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<WorldUpdater::RobotSubscription>>
WorldUpdater::RobotSubscription::Create(
    const IconRobotSubscriptionConfig& config, intrinsic::PubSub* pubsub) {
  INTR_ASSIGN_OR_RETURN(
      const std::string topic_name,
      icon::TopicNameForRobotStatusThrottle(config.robot_id()));

  auto subscription = std::make_unique<RobotSubscription>();
  subscription->resource_name = config.equipment_name();
  subscription->part_name = config.part_name();
  subscription->object_name = config.object_name();
  subscription->last_update = absl::Now();

  LOG(INFO) << "Will listen on PubSub-topic[" << topic_name << "]";

  INTR_ASSIGN_OR_RETURN(
      subscription->subscription,
      pubsub->CreateSubscription<LogItem>(
          topic_name, TopicConfig(),
          [subscription = subscription.get()](const LogItem& message) {
            auto status = subscription->HandleIconStatus(message);
            if (!status.ok()) {
              LOG(ERROR) << "error handling ICON status: " << status;
            }
          },
          [](absl::string_view packet, const absl::Status& error) {
            LOG(ERROR) << error;
          }));

  return subscription;
}

absl::Status WorldUpdater::RobotSubscription::HandleIconStatus(
    const intrinsic_proto::data_logger::LogItem& robot_status) {
  if (!robot_status.payload().has_icon_robot_status()) {
    return absl::InvalidArgumentError("Status payload is missing.");
  }
  const auto& status_payload = robot_status.payload().icon_robot_status();
  RobotUpdate* update = update_buffer.GetFreeBuffer();

  update->first = absl::FromUnixNanos(status_payload.wall_clock_timestamp_ns());

  absl::Duration time_since_last_sample = update->first - last_update;
  last_update = update->first;

  const absl::Time now = absl::Now();
  const absl::Duration sample_latency = now - update->first;

  auto part_status_it = status_payload.status_map().find(part_name);
  if (part_status_it == status_payload.status_map().end()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Status payload does not have data about part '", part_name,
        "'. This could be because the robot is faulted. OperationalStatus: ",
        status_payload.operational_status()));
  }
  const intrinsic_proto::icon::PartStatus& part_status = part_status_it->second;
  update->second = eigenmath::VectorXd(part_status.joint_states_size());
  for (int i = 0; i < part_status.joint_states_size(); i++) {
    update->second[i] = part_status.joint_states(i).position_sensed();
  }

  update_buffer.CommitFreeBuffer();
  stale = false;
  const std::array<
      std::pair<absl::string_view, opentelemetry::common::AttributeValue>, 1>
      attributes = {{{kResourceNameKey, resource_name}}};
  SampleDeltaT().Record(absl::ToInt64Microseconds(std::max(
                            absl::ZeroDuration(), time_since_last_sample)),
                        attributes);
  SampleLatency().Record(
      absl::ToInt64Microseconds(std::max(absl::ZeroDuration(), sample_latency)),
      attributes);
  return absl::OkStatus();
}

bool WorldUpdater::RobotSubscription::GetLatestSample(RobotUpdate** update) {
  return update_buffer.GetActiveBuffer(update);
}

absl::StatusOr<std::unique_ptr<WorldUpdater::RosJointStateSubscription>>
WorldUpdater::RosJointStateSubscription::Create(
    const JointStateSubscriptionConfig& config, intrinsic::PubSub* pubsub) {
  auto subscription = std::make_unique<RosJointStateSubscription>();
  subscription->resource_name = config.resource_name;
  subscription->object_name = config.object_name;
  subscription->last_update = absl::InfinitePast();

  const std::string topic_name =
      absl::StrCat("/assets/instances/", config.resource_name, "/joint_state");
  LOG(INFO) << "Will listen on PubSub-topic[" << topic_name << "]";

  INTR_ASSIGN_OR_RETURN(
      subscription->subscription,
      pubsub->CreateSubscription<sensor_msgs::msg::pb::jazzy::JointState>(
          topic_name, TopicConfig(),
          [subscription = subscription.get()](
              const sensor_msgs::msg::pb::jazzy::JointState& message) {
            auto status = subscription->HandleJointState(message);
            if (!status.ok()) {
              LOG(ERROR) << "error handling JointState: " << status;
            }
          },
          [](absl::string_view packet, const absl::Status& error) {
            LOG(ERROR) << error;
          }));

  return subscription;
}

absl::Status WorldUpdater::RosJointStateSubscription::HandleJointState(
    const sensor_msgs::msg::pb::jazzy::JointState& joint_state) {
  JointUpdate* update = update_buffer.GetFreeBuffer();
  *update = joint_state;

  INTR_ASSIGN_OR_RETURN(absl::Time stamp,
                        ToAbslTime(joint_state.header().stamp()));

  if (stamp < last_update) {
    return absl::OkStatus();
  }

  last_update = stamp;

  update_buffer.CommitFreeBuffer();
  return absl::OkStatus();
}

absl::Status WorldUpdater::TfSubscription::HandleTfMessage(
    const intrinsic_proto::TFMessage& tf_message) {
  absl::MutexLock l(updates_mu);
  if (paused) {
    LOG_EVERY_N_SEC(INFO, 5)
        << "Skipping TF Message while world updater is paused";
    return absl::OkStatus();
  }
  pending_updates.reserve(pending_updates.size() +
                          tf_message.transforms_size());
  for (const auto& transform_stamped : tf_message.transforms()) {
    pending_updates.push_back(transform_stamped);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<WorldUpdater::TfSubscription>>
WorldUpdater::TfSubscription::Create(intrinsic::PubSub& pubsub) {
  auto subscription = std::make_unique<TfSubscription>();
  INTR_ASSIGN_OR_RETURN(
      subscription->subscription,
      pubsub.CreateSubscription<intrinsic_proto::TFMessage>(
          "/tf", TopicConfig(),
          [subscription =
               subscription.get()](const intrinsic_proto::TFMessage& message) {
            auto status = subscription->HandleTfMessage(message);
            if (!status.ok()) {
              LOG(ERROR) << "error handling TfMessage: " << status;
            }
          },
          [](absl::string_view packet, const absl::Status& error) {
            LOG(ERROR) << error;
          }));

  return subscription;
}

absl::Status WorldUpdater::PubsubWorldUpdatesSubscription::HandleWorldUpdates(
    const PubsubWorldUpdates& updates) {
  if (paused) {
    LOG_EVERY_N_SEC(INFO, 5)
        << "Skipping PubsubWorldUpdates while world updater is paused";
    return absl::OkStatus();
  }
  absl::MutexLock l(&updates_mu);
  pending_updates.reserve(pending_updates.size() + updates.updates_size());
  for (const auto& update : updates.updates()) {
    pending_updates.push_back(update);
  }
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<WorldUpdater::PubsubWorldUpdatesSubscription>>
WorldUpdater::PubsubWorldUpdatesSubscription::Create(
    absl::string_view world_id, intrinsic::PubSub& pubsub) {
  auto subscription = std::make_unique<PubsubWorldUpdatesSubscription>();
  subscription->world_id = std::string(world_id);
  subscription->topic_name =
      absl::StrCat("/worlds/", world_id, "/world_updates");
  LOG(INFO) << "Will listen on PubSub-topic[" << subscription->topic_name
            << "]";

  INTR_ASSIGN_OR_RETURN(
      subscription->subscription,
      pubsub.CreateSubscription<PubsubWorldUpdates>(
          subscription->topic_name, TopicConfig(),
          [subscription =
               subscription.get()](const PubsubWorldUpdates& message) {
            auto status = subscription->HandleWorldUpdates(message);
            if (!status.ok()) {
              LOG(ERROR) << "error handling PubsubWorldUpdates: " << status;
            }
          },
          [](absl::string_view packet, const absl::Status& error) {
            LOG(ERROR) << error;
          }));

  return subscription;
}

absl::Status WorldUpdater::UpdateWorldOnce() {
  // TODO(b/521816693): Applying updates in type-sequence (Robots, JointStates,
  // Transforms, PubsubWorldUpdates) rather than chronological order of arrival
  // might lead to out-of-order application or incorrect state if updates are
  // interleaved across different subscription types.
  // Currently this is unlikely to happen because UpdateRobotsOnce,
  // UpdateJointStatesOnce and UpdateTransformsOnce update different subsets of
  // the belief world while UpdateSimWorldOnce updates the sim world.
  // In the future if this separation does not hold we need to address the out
  // of order error.
  INTR_RETURN_IF_ERROR(UpdateRobotsOnce());
  INTR_RETURN_IF_ERROR(UpdateJointStatesOnce());
  INTR_RETURN_IF_ERROR(UpdateTransformsOnce());
  INTR_RETURN_IF_ERROR(UpdateSimWorldOnce());
  return absl::OkStatus();
}

absl::Status WorldUpdater::UpdateRobotsOnce() {
  std::vector<std::tuple<absl::Time, std::string, eigenmath::VectorXd>>
      robot_updates;

  bool has_samples = false;
  {
    absl::ReaderMutexLock l(subscriptions_mu_);
    robot_updates.reserve(robot_subscriptions_.size());
    for (auto& robot_subscription : robot_subscriptions_) {
      RobotSubscription::RobotUpdate* update;
      if (robot_subscription->stale ||
          !robot_subscription->GetLatestSample(&update)) {
        continue;
      }
      has_samples = true;

      robot_updates.push_back(std::make_tuple(
          update->first, robot_subscription->object_name, update->second));
    }
  }
  if (!has_samples) {
    return absl::OkStatus();
  }

  const absl::Time now = absl::Now();
  auto* world_storage = GetWorldStorage();
  if (world_storage == nullptr) {
    LOG_EVERY_N_SEC(INFO, 1)
        << "World stub is not ready yet. Skipping robot updates.";
    return absl::OkStatus();
  }
  // Sort the updates by timestamp to apply updates in the correct order.
  absl::c_sort(robot_updates, [](const auto& l, const auto& r) {
    return std::get<0>(l) < std::get<0>(r);
  });
  INTR_ASSIGN_OR_RETURN(auto world, world_storage->GetWorld(belief_world_id_));
  int accepted_samples = 0;
  int rejected_samples = 0;
  {
    absl::MutexLock wl(*(world->mtx));
    INTR_ASSIGN_OR_RETURN(auto ow, world->GetObjectWorld());
    absl::Time latest_update_time = absl::InfinitePast();
    for (const auto& [time, object_name, joint_positions] : robot_updates) {
      INTR_ASSIGN_OR_RETURN(
          object_world::KinematicObject * ko,
          ow->GetKinematicObject(WorldObjectName(object_name)));
      INTR_ASSIGN_OR_RETURN(absl::Time ko_time,
                            GetKinematicObjectTimestamp(*ko));
      if (time < ko_time) {
        LOG_EVERY_N_SEC(INFO, 10)
            << "Update rejected at " << time << " for robot " << object_name
            << " because robot last updated at " << ko_time
            << ", wall clock on updater is " << now << " delta " << now - time;
        rejected_samples++;
        continue;
      }
      INTR_RETURN_IF_ERROR(ko->SetJointPositions(
          joint_positions, /*enforce_limits=*/false, time));
      latest_update_time = std::max(latest_update_time, time);
      accepted_samples++;
    }
    if (latest_update_time != absl::InfinitePast()) {
      world->UpdateTimestamp(latest_update_time);
      world_storage->MarkWorldAsChanged(belief_world_id_, world,
                                        /*has_state_change=*/true);
    } else {
      LOG_EVERY_N_SEC(ERROR, 10)
          << "All " << robot_updates.size()
          << " robot updates were stale and skipped. Not marking world as "
             "changed.";
    }
  }
  if (rejected_samples > 0) {
    DelayedSampleRejectCount().Add(rejected_samples);
  }
  if (accepted_samples > 0) {
    AcceptedSampleCount().Add(accepted_samples);
  }

  return absl::OkStatus();
}

absl::StatusOr<bool> WorldUpdater::ApplyJointStateUpdate(
    object_world::ObjectWorld& ow,
    const sensor_msgs::msg::pb::jazzy::JointState& joint_state,
    absl::Time update_stamp,
    std::optional<std::string_view> target_object_name) {
  object_world::KinematicObject* target_ko = nullptr;

  if (target_object_name.has_value()) {
    INTR_ASSIGN_OR_RETURN(
        target_ko, ow.GetKinematicObject(WorldObjectName(*target_object_name)));
  } else {
    INTR_ASSIGN_OR_RETURN(target_ko,
                          FindKinematicObjectForJointState(ow, joint_state));
  }

  if (target_ko == nullptr) {
    return false;
  }

  INTR_ASSIGN_OR_RETURN(absl::Time ko_time,
                        GetKinematicObjectTimestamp(*target_ko));
  if (update_stamp < ko_time) {
    return false;
  }

  INTR_RETURN_IF_ERROR(SetJointPositionsFromRos(*target_ko, joint_state));

  return true;
}

absl::Status WorldUpdater::UpdateJointStatesOnce() {
  std::vector<std::tuple<absl::Time, std::string,
                         sensor_msgs::msg::pb::jazzy::JointState>>
      joint_updates;

  {
    absl::ReaderMutexLock l(subscriptions_mu_);
    joint_updates.reserve(ros_joint_state_subscriptions_.size());
    for (auto& subscription : ros_joint_state_subscriptions_) {
      sensor_msgs::msg::pb::jazzy::JointState* update;
      if (!subscription->update_buffer.GetActiveBuffer(&update)) {
        continue;
      }

      INTR_ASSIGN_OR_RETURN(absl::Time stamp,
                            ToAbslTime(update->header().stamp()));

      joint_updates.push_back(
          std::make_tuple(stamp, subscription->object_name, *update));
    }
  }
  if (joint_updates.empty()) {
    return absl::OkStatus();
  }

  auto* world_storage = GetWorldStorage();
  if (world_storage == nullptr) {
    LOG_EVERY_N_SEC(INFO, 1)
        << "World stub is not ready yet. Skipping joint state updates.";
    return absl::OkStatus();
  }

  absl::c_sort(joint_updates, [](const auto& l, const auto& r) {
    return std::get<0>(l) < std::get<0>(r);
  });

  INTR_ASSIGN_OR_RETURN(auto world, world_storage->GetWorld(belief_world_id_));
  {
    absl::MutexLock wl(*(world->mtx));
    INTR_ASSIGN_OR_RETURN(auto ow, world->GetObjectWorld());
    absl::Time latest_update_time = absl::InfinitePast();

    for (const auto& [time, object_name, joint_state] : joint_updates) {
      auto applied_status =
          ApplyJointStateUpdate(*ow, joint_state, time, object_name);
      if (!applied_status.ok()) {
        LOG(ERROR) << "Failed to apply joint state update for " << object_name
                   << ": " << applied_status.status();
        continue;
      }
      if (*applied_status) {
        latest_update_time = std::max(latest_update_time, time);
      }
    }
    if (latest_update_time != absl::InfinitePast()) {
      world->UpdateTimestamp(latest_update_time);
      world_storage->MarkWorldAsChanged(belief_world_id_, world,
                                        /*has_state_change=*/true);
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_map<std::string, google::protobuf::Timestamp>>
WorldUpdater::ApplyTfUpdates(
    object_world::ObjectWorld& ow,
    std::vector<intrinsic_proto::TransformStamped> tf_updates) {
  absl::flat_hash_map<std::string, google::protobuf::Timestamp>
      successful_tf_updates;

  if (tf_updates.empty()) {
    return successful_tf_updates;
  }

  absl::c_sort(tf_updates, [](const auto& l, const auto& r) {
    return l.header().stamp() < r.header().stamp();
  });

  for (const auto& transform_stamped : tf_updates) {
    // Bypassing the movable check is a workaround to support updating cables
    // in the simulator. These cables are modeled in the simulator with
    // movable joints but represented in the world as fixed joints (using
    // AttachmentComponents). This is not generally safe, but serves as an
    // intermediate solution until cables are modeled with ball joints.
    absl::Status update_state = UpdateWorldTransform(
        ow, transform_stamped, /*bypass_movable_check=*/true);
    if (update_state.ok()) {
      successful_tf_updates.insert_or_assign(
          transform_stamped.child_frame_id(),
          transform_stamped.header().stamp());
    } else {
      LOG_EVERY_N_SEC(ERROR, 1)
          << "Failed to update transform" << transform_stamped
          << " with error: " << update_state.message();
    }
  }

  return successful_tf_updates;
}

absl::StatusOr<absl::flat_hash_map<std::string, google::protobuf::Timestamp>>
WorldUpdater::ApplyTfUpdates(object_world::ObjectWorld& ow,
                             const intrinsic_proto::TFMessage& tf_message) {
  std::vector<intrinsic_proto::TransformStamped> tf_updates;
  tf_updates.reserve(tf_message.transforms_size());
  for (const auto& transform_stamped : tf_message.transforms()) {
    tf_updates.push_back(transform_stamped);
  }
  return ApplyTfUpdates(ow, std::move(tf_updates));
}

absl::Status WorldUpdater::UpdateTransformsOnce() {
  auto* world_storage = GetWorldStorage();
  if (world_storage == nullptr) {
    LOG_EVERY_N_SEC(INFO, 1)
        << "World stub is not ready yet. Skipping transform updates.";
    return absl::OkStatus();
  }

  std::vector<intrinsic_proto::TransformStamped> updates;
  {
    absl::ReaderMutexLock sl(subscriptions_mu_);
    if (tf_subscription_ == nullptr) {
      return absl::OkStatus();
    }
    absl::MutexLock ul(tf_subscription_->updates_mu);
    updates.swap(tf_subscription_->pending_updates);
  }

  if (updates.empty()) {
    return absl::OkStatus();
  }

  // Filter updates (belief world specific)
  INTR_RETURN_IF_ERROR(tf_echo_guard_->FilterPendingUpdates(updates));

  if (updates.empty()) {
    return absl::OkStatus();
  }

  {
    INTR_ASSIGN_OR_RETURN(auto world,
                          world_storage->GetWorld(belief_world_id_));
    absl::MutexLock wl(*(world->mtx));
    INTR_ASSIGN_OR_RETURN(auto ow, world->GetObjectWorld());

    INTR_ASSIGN_OR_RETURN(auto successful_tf_updates,
                          ApplyTfUpdates(*ow, std::move(updates)));

    if (!successful_tf_updates.empty()) {
      auto timestamps = std::views::values(successful_tf_updates);
      auto latest_timestamp =
          std::max_element(timestamps.begin(), timestamps.end());
      INTR_ASSIGN_OR_RETURN(absl::Time absl_time,
                            ToAbslTime(*latest_timestamp));
      world->UpdateTimestamp(absl_time);
      world_storage->MarkWorldAsChanged(belief_world_id_, world,
                                        /*has_state_change=*/true);
      INTR_RETURN_IF_ERROR(tf_echo_guard_->RecordSuccessfulUpdates(
          std::move(successful_tf_updates)));
    }
  }

  return absl::OkStatus();
}

absl::StatusOr<absl::Time> WorldUpdater::ApplyPubsubWorldUpdate(
    object_world::ObjectWorld& ow, const PubsubWorldUpdate& update) {
  if (update.has_joint_state()) {
    INTR_ASSIGN_OR_RETURN(absl::Time joint_state_time,
                          ToAbslTime(update.joint_state().header().stamp()));
    INTR_ASSIGN_OR_RETURN(
        bool applied,
        ApplyJointStateUpdate(ow, update.joint_state(), joint_state_time));
    if (applied) {
      return joint_state_time;
    }
  } else if (update.has_transform()) {
    INTR_ASSIGN_OR_RETURN(auto successful_tf_updates,
                          ApplyTfUpdates(ow, update.transform()));
    if (!successful_tf_updates.empty()) {
      auto tf_timestamps = std::views::values(successful_tf_updates);
      auto latest_tf_timestamp_it =
          std::max_element(tf_timestamps.begin(), tf_timestamps.end());
      INTR_ASSIGN_OR_RETURN(absl::Time tf_update_time,
                            ToAbslTime(*latest_tf_timestamp_it));
      return tf_update_time;
    }
  }
  return absl::InfinitePast();
}

absl::Status WorldUpdater::UpdateSimWorldOnce() {
  auto* world_storage = GetWorldStorage();
  if (world_storage == nullptr) {
    return absl::OkStatus();
  }

  std::vector<PubsubWorldUpdate> updates;
  std::string world_id;
  {
    absl::ReaderMutexLock sl(subscriptions_mu_);
    if (sim_world_updates_subscription_ == nullptr) {
      return absl::OkStatus();
    }
    world_id = sim_world_updates_subscription_->world_id;
    absl::MutexLock ul(sim_world_updates_subscription_->updates_mu);
    updates.swap(sim_world_updates_subscription_->pending_updates);
  }

  if (updates.empty()) {
    return absl::OkStatus();
  }

  SortPubsubWorldUpdatesByTime(updates);

  INTR_ASSIGN_OR_RETURN(auto world, world_storage->GetWorld(world_id));
  {
    absl::MutexLock wl(*(world->mtx));
    INTR_ASSIGN_OR_RETURN(auto ow, world->GetObjectWorld());
    absl::Time latest_update_time = absl::InfinitePast();

    for (const auto& update : updates) {
      auto update_time = ApplyPubsubWorldUpdate(*ow, update);
      if (!update_time.ok()) {
        LOG(ERROR) << "Failed to apply pubsub world update: "
                   << update_time.status();
        continue;
      }
      if (*update_time != absl::InfinitePast()) {
        latest_update_time = std::max(latest_update_time, *update_time);
      }
    }

    if (latest_update_time != absl::InfinitePast()) {
      world->UpdateTimestamp(latest_update_time);
      world_storage->MarkWorldAsChanged(world_id, world,
                                        /*has_state_change=*/true);
    }
  }

  return absl::OkStatus();
}

void WorldUpdater::UpdateWorld() {
  while (!stop_.HasBeenNotified()) {
    absl::Duration sleep_duration = world_update_interval_;

    paused_mu_.lock();
    if (paused_) {
      paused_mu_.unlock();
      stop_.WaitForNotificationWithTimeout(sleep_duration);
      continue;
    }

    // Make sure we don't pause while we're updating.
    absl::Status update_world_status = UpdateWorldOnce();
    if (!update_world_status.ok()) {
      LOG(ERROR) << "Failed to update world: " << update_world_status.message();
      sleep_duration = absl::Seconds(3);
    }
    paused_mu_.unlock();
    stop_.WaitForNotificationWithTimeout(sleep_duration);
  }
}

WorldStorage* WorldUpdater::GetWorldStorage() {
  if (world_storage_future_.valid()) {
    return world_storage_future_.get().get();
  }
  return nullptr;
}

void WorldUpdater::ReconfigureFromResourceRegistry() {
  while (!stop_.HasBeenNotified()) {
    absl::Status reconfigure_status = ReconfigureFromResourceRegistryOnce();
    LOG_IF(ERROR, !reconfigure_status.ok())
        << "Failed to reconfigure: " << reconfigure_status.message();
    stop_.WaitForNotificationWithTimeout(reconfigure_interval_);
  }
}

absl::Status WorldUpdater::ReconfigureFromResourceRegistryOnce() {
  // This function is not reentrant, and can trigger data loss if called from
  // >1 thread concurrently. We serialize access here with a mutex that must
  // be free on every call.
  CHECK(reconfigure_mu_.try_lock())
      << "This function is not reentrant, and will trigger data loss if called "
         "from >1 thread concurrently.";

  // We can't use absl::MutexLock here because we want to "cleanly" handle the
  // case where the mutex can't be acquired.
  absl::Cleanup reconfigure_mu_releaser = [this] {
    reconfigure_mu_.AssertHeld();
    reconfigure_mu_.unlock();
  };

  INTR_RETURN_IF_ERROR(ReconfigureRobotSubscriptions());
  INTR_RETURN_IF_ERROR(ReconfigureJointStateSubscriptions());

  return absl::OkStatus();
}

absl::Status WorldUpdater::ReconfigureRobotSubscriptions() {
  auto selector =
      icon::Icon2ResourceSelectorBuilder().WithPositionControlledPart().Build();
  intrinsic_proto::resources::ListResourceInstanceRequest::StrictFilter filter;
  *filter.mutable_capability_names() = selector.capability_names();
  auto matching_resources = resource_registry_client_->ListResources(filter);

  if (!matching_resources.ok() &&
      absl::IsNotFound(matching_resources.status())) {
    // No matching equipment
    return absl::OkStatus();
  }
  INTR_RETURN_IF_ERROR(matching_resources.status());

  auto register_subscription =
      [this](const absl::string_view part_name,
             const absl::string_view object_name,
             const intrinsic_proto::resources::ResourceHandle& handle) {
        intrinsic_proto::world::IconRobotSubscriptionConfig sub_cfg;

        sub_cfg.set_part_name(part_name);
        sub_cfg.set_equipment_name(handle.name());
        if (!object_name.empty()) {
          sub_cfg.set_object_name(object_name);
        } else {
          LOG(ERROR) << "No dedicated kinematics model found for " << part_name
                     << ". Using " << handle.name();
          sub_cfg.set_object_name(handle.name());
        }
        sub_cfg.set_robot_id(handle.name());

        absl::Status add_subscription_status = [&]() -> absl::Status {
          INTR_ASSIGN_OR_RETURN(auto connection_config,
                                skills::GetConnectionParamsFromHandle(handle));
          INTR_ASSIGN_OR_RETURN(auto channel,
                                icon_channel_factory_->MakeChannel(
                                    connection_config, absl::Seconds(1)));
          icon::Client client(channel);
          INTR_ASSIGN_OR_RETURN(auto icon_config, client.GetConfig());
          sub_cfg.set_robot_id(icon_config.GetServerName());
          LOG(INFO) << "Overrode name with " << icon_config.GetServerName();

          {
            // Important: we *only* grab the write lock to subscriptions at this
            // point because the other steps in this function can be slow. Since
            // holding this lock blocks world updates, we can introduce jitter
            // in the frontend visualization of robot motion. The actual add
            // call should be fairly fast, since all the setup machinery is
            // already done.
            absl::MutexLock l(subscriptions_mu_);
            return AddRobotSubscription(sub_cfg);
          }
        }();

        if (!add_subscription_status.ok()) {
          LOG_EVERY_N_SEC(ERROR, 10)
              << "Failed to configure subscription for robot on " << part_name
              << ": " << add_subscription_status.message();
        } else {
          LOG(INFO) << "Added robot subscription: " << part_name;
        }
      };

  for (const intrinsic_proto::resources::ResourceInstance& resource :
       *matching_resources) {
    constexpr const std::string_view position_key = "Icon2PositionPart";
    const intrinsic_proto::resources::ResourceHandle& handle =
        resource.resource_handle();
    auto resource_data = handle.resource_data().at(position_key);

    intrinsic_proto::icon::Icon2PositionPart part;
    if (!resource_data.contents().UnpackTo(&part)) {
      return absl::InvalidArgumentError("Failed to unpack resource data.");
    }

    // register every individual arm part
    for (const auto& [part_name, object_name] : part.object_names()) {
      bool part_sub_exists = false;
      {
        absl::ReaderMutexLock l(subscriptions_mu_);
        part_sub_exists = std::any_of(
            robot_subscriptions_.begin(), robot_subscriptions_.end(),
            [&](const std::unique_ptr<RobotSubscription>& sub) {
              return sub->part_name == part_name &&
                     sub->resource_name == handle.name();
            });
      }
      if (part_sub_exists) {
        continue;
      }
      register_subscription(part_name, object_name, handle);
    }
  }
  return absl::OkStatus();
}

absl::Status WorldUpdater::ReconfigureJointStateSubscriptions() {
  intrinsic_proto::resources::ListResourceInstanceRequest::StrictFilter filter;
  auto matching_resources = resource_registry_client_->ListResources(filter);

  if (!matching_resources.ok() &&
      absl::IsNotFound(matching_resources.status())) {
    return absl::OkStatus();
  }
  INTR_RETURN_IF_ERROR(matching_resources.status());

  absl::flat_hash_set<std::string> active_resource_names;
  for (const auto& resource : *matching_resources) {
    active_resource_names.insert(resource.name());
  }

  {
    absl::MutexLock l(subscriptions_mu_);
    auto it = std::remove_if(
        ros_joint_state_subscriptions_.begin(),
        ros_joint_state_subscriptions_.end(),
        [&](const std::unique_ptr<RosJointStateSubscription>& sub) {
          return !active_resource_names.contains(sub->resource_name);
        });
    if (it != ros_joint_state_subscriptions_.end()) {
      LOG(INFO) << "Removing "
                << std::distance(it, ros_joint_state_subscriptions_.end())
                << " stale JointState subscriptions";
      ros_joint_state_subscriptions_.erase(
          it, ros_joint_state_subscriptions_.end());
    }
  }

  auto register_subscription = [this](const absl::string_view resource_name) {
    JointStateSubscriptionConfig sub_cfg;
    sub_cfg.resource_name = resource_name;
    sub_cfg.object_name = resource_name;

    absl::Status add_subscription_status = [&]() -> absl::Status {
      absl::MutexLock l(subscriptions_mu_);
      INTR_ASSIGN_OR_RETURN(
          auto subscription,
          RosJointStateSubscription::Create(sub_cfg, &pubsub_));
      ros_joint_state_subscriptions_.emplace_back(std::move(subscription));
      return absl::OkStatus();
    }();

    if (!add_subscription_status.ok()) {
      LOG_EVERY_N_SEC(ERROR, 10)
          << "Failed to configure JointState subscription for resource "
          << resource_name << ": " << add_subscription_status.message();
    } else {
      LOG(INFO) << "Added JointState subscription: " << resource_name;
    }
  };

  for (const std::string& resource_name : active_resource_names) {
    bool resource_sub_exists = false;
    {
      absl::ReaderMutexLock l(subscriptions_mu_);
      resource_sub_exists = std::any_of(
          ros_joint_state_subscriptions_.begin(),
          ros_joint_state_subscriptions_.end(),
          [&](const std::unique_ptr<RosJointStateSubscription>& sub) {
            return sub->resource_name == resource_name;
          });
    }
    if (resource_sub_exists) {
      continue;
    }
    register_subscription(resource_name);
  }
  return absl::OkStatus();
}

}  // namespace intrinsic
