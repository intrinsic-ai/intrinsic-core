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

#include "intrinsic/simulation/gazebo/plugins/grippers/actuated_gripper_plugin.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/text_format.h"
#include "gz/common/Profiler.hh"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/Util.hh"
#include "gz/sim/components/ContactSensorData.hh"
#include "gz/sim/components/JointAxis.hh"
#include "gz/sim/components/JointForceCmd.hh"
#include "gz/sim/components/JointPosition.hh"
#include "gz/sim/components/JointPositionReset.hh"
#include "gz/sim/components/JointVelocity.hh"
#include "intrinsic/hardware/gripper/eoat/eoat_service.pb.h"
#include "intrinsic/hardware/gripper/gripper.pb.h"
#include "intrinsic/hardware/gripper/service/proto/pinch_gripper_server.pb.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/simulation/gazebo/plugins/gpio/gpio_service.h"
#include "intrinsic/simulation/gazebo/plugins/gravity_compensator.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/actuated_gripper_plugin_connection.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/fixed_joint_gripper.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/gpio_gripper_plugin_connection.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/gripper.pb.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/pid_joint_util.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/pinch_gripper_server_impl.h"
#include "intrinsic/simulation/gazebo/plugins/joint_device_util.h"
#include "intrinsic/util/macros.h"
#include "intrinsic/util/status/status_builder.h"
#include "intrinsic/util/status/status_macros.h"
#include "sdf/Element.hh"
#include "sdf/JointAxis.hh"

namespace intrinsic {
namespace simulation {

using ::intrinsic::simulation::ActuatedGripperConnection;
using ::intrinsic_proto::gripper::PinchGripperConfig;
using ::intrinsic_proto::simulation::gazebo::GripperCommand;
using ::intrinsic_proto::simulation::gazebo::GripperStatus;

using ::gz::sim::Entity;
using ::gz::sim::kNullEntity;
using ::gz::sim::components::ContactSensorData;
using ::gz::sim::components::JointAxis;
using ::gz::sim::components::JointForceCmd;
using ::gz::sim::components::JointPosition;
using ::gz::sim::components::JointPositionReset;
using ::gz::sim::components::JointVelocity;

namespace {
constexpr double kDefaultGraspDebounceTimeSeconds = 0.5;

// Returns the lower and upper limits of the joint.
absl::StatusOr<std::pair<double, double>> ValidateGripperJointLimits(
    Entity joint_entity, const gz::sim::EntityComponentManager& ecm) {
  std::optional<::sdf::JointAxis> joint_axis_sdf =
      ecm.ComponentData<JointAxis>(joint_entity);
  if (!joint_axis_sdf.has_value()) {
    return absl::InvalidArgumentError(
        "ECM does not have JointAxis component for gripper joint! Please "
        "ensure that the gripper joint is not fixed.");
  }
  double lower_limit = joint_axis_sdf->Lower();
  double upper_limit = joint_axis_sdf->Upper();
  if (upper_limit <= lower_limit) {
    // NOTE: Technically the physics engine can handle the equality case, but it
    // is not practical for a gripper finger joint to have equal upper and lower
    // limits. So we return an error here to potentially catch unintended
    // limit configurations in the SDF.
    return InvalidArgumentErrorBuilder()
           << "Gripper joint has invalid limits. Upper limit must be greater "
              "than lower limit. Got upper limit = "
           << upper_limit << " and lower limit = " << lower_limit;
  }
  return std::make_pair(lower_limit, upper_limit);
}

}  // namespace

absl::StatusOr<std::vector<ActuatedGripperPlugin::ControlledJoint>>
ActuatedGripperPlugin::ConfigureJoints(Entity gripper_entity,
                                       const ::sdf::Element* sdf_element,
                                       EntityComponentManager& ecm) {
  INTR_ASSIGN_OR_RETURN(std::vector<PIDJoint> pid_joints,
                        GetPIDJoints(gripper_entity, sdf_element, ecm));

  if (pid_joints.empty()) {
    return absl::InvalidArgumentError("No joints configured in plugin xml.");
  }

  std::vector<ActuatedGripperPlugin::ControlledJoint> joints;
  for (auto& joint : pid_joints) {
    INTR_ASSIGN_OR_RETURN(
        std::unique_ptr<GravityCompensator> gravity_compensator,
        GravityCompensator::Create(joint.joint_entity, &ecm,
                                   kJointDeviceJointNameScopeSeparator),
        _.With(ExtraMessage()
               << "Couldn't create gravity compensator for joint "
               << joint.joint_name << ": "));

    joints.push_back({.joint_entity = joint.joint_entity,
                      .pid = std::move(joint.pid),
                      .gravity_compensator = std::move(gravity_compensator)});
  }

  for (auto& joint : joints) {
    double initial_position = 0.0;
    if (const auto* pos_reset =
            ecm.Component<JointPositionReset>(joint.joint_entity);
        pos_reset != nullptr && !pos_reset->Data().empty()) {
      initial_position = pos_reset->Data()[0];
    }
    CreateECMComponent<JointPosition>(joint.joint_entity,
                                      /*joint_dof_count=*/1, ecm);
    GetECMComponentData<JointPosition>(joint.joint_entity, ecm)[0] =
        initial_position;
    CreateECMComponent<JointVelocity>(joint.joint_entity,
                                      /*joint_dof_count=*/1, ecm);
    GetECMComponentData<JointVelocity>(joint.joint_entity, ecm)[0] = 0.0;
    PrepareTorqueCmdECMComponent(joint.joint_entity, /*joint_dof_count=*/1,
                                 ecm);
  }
  return joints;
}

ActuatedGripperPlugin::~ActuatedGripperPlugin() {
  DestroyActuatedGripperConnection();
  DestroyGPIOGripperConnection();
}

void ActuatedGripperPlugin::Configure(
    const Entity& entity, const std::shared_ptr<const sdf::Element>& sdf,
    EntityComponentManager& ecm, EventManager& eventMgr) {
  parent_entity_ = entity;
  plugin_name_ = sdf->Get<std::string>("name");

  ::sdf::ElementPtr plugin_sdf = sdf->Clone();
  INTR_RETURN_IF_ERROR(LoadSDF(plugin_sdf.get(), ecm))
      .With([this](intrinsic::StatusBuilder builder) {
        builder.LogError() << "'" << plugin_name_ << "' failed to load SDF.";
        absl::MutexLock lock(initialization_status_mutex_);
        initialization_status_ = builder;
        return;
      });

  INTR_ASSIGN_OR_RETURN(
      fixed_joint_gripper_,
      FixedJointGripper::Create(entity, plugin_name_, sticky_link_name_,
                                kDefaultGraspDebounceTimeSeconds, ecm),
      _.LogError().With([this](const absl::Status& status) {
        absl::MutexLock lock(initialization_status_mutex_);
        initialization_status_ = status;
        return;
      }));

  INTR_ASSIGN_OR_RETURN(
      joints_, ConfigureJoints(parent_entity_, plugin_sdf.get(), ecm),
      _.With([this](intrinsic::StatusBuilder builder) {
        builder.LogError() << "'" << plugin_name_
                           << "' failed to configure gripper joints. ";
        absl::MutexLock lock(initialization_status_mutex_);
        initialization_status_ = builder;
        return;
      }));

  // The gripper is assumed to be symmetric if it has multiple joints.
  // Therefore we may use just the first joint for status calculations.
  feedback_joint_entity_ = joints_[0].joint_entity;
  {
    absl::MutexLock lock(command_mutex_);
    desired_position_ =
        GetECMComponentData<JointPosition>(feedback_joint_entity_, ecm)[0];
  }
  INTR_ASSIGN_OR_RETURN(
      std::tie(feedback_joint_lower_limit_, feedback_joint_upper_limit_),
      ValidateGripperJointLimits(feedback_joint_entity_, ecm),
      _.LogError().With([this](const absl::Status& status) {
        absl::MutexLock lock(initialization_status_mutex_);
        initialization_status_ = status;
        return;
      }));

  CHECK_EQ(actuated_gripper_connection_, nullptr);
  InitActuatedGripperConnection();
  if (pinch_gripper_config_.has_value()) {
    InitGPIOGripperConnection(pinch_gripper_config_.value());
  }
  // Publish an initial status. This allows clients to receive this
  // information immediately when subscribing to a topic since all topics are
  // latched in our framework.
  PinchGripperStatus gripper_status;
  gripper_status.set_position(GetCurrentPosition(ecm));
  gripper_status.set_position_reached(true);
  gripper_status.set_gripper_enabled(true);
  actuated_gripper_connection_->SetPinchGripperStatus(gripper_status);
  if (pub_.has_value()) {
    if (const absl::Status status = pub_->Publish(gripper_status);
        !status.ok()) {
      LOG(ERROR) << "Failed to send status update. Error: " << status;
    }
  } else {
    LOG(ERROR) << "Failed to send status update. Publisher not initialized.";
  }

  absl::MutexLock lock(initialization_status_mutex_);
  initialization_status_ = absl::OkStatus();
}

void ActuatedGripperPlugin::InitActuatedGripperConnection() {
  // Setup pinch gripper config from plugin parameters.
  PinchGripperConfig pinch_gripper_config;
  if (service_pinch_gripper_config_.has_value()) {
    pinch_gripper_config = *service_pinch_gripper_config_;
  }
  if (pinch_gripper_config.name().empty()) {
    pinch_gripper_config.set_name(pinch_gripper_handle_);
  }
  if (!topic_status_.empty() &&
      pinch_gripper_config.generic_pinch_gripper_config()
          .additional_config()
          .status_topic()
          .empty()) {
    pinch_gripper_config.mutable_generic_pinch_gripper_config()
        ->mutable_additional_config()
        ->set_status_topic(topic_status_);
  }

  LOG(INFO) << "Initializing pinch gripper config for handle "
            << pinch_gripper_handle_;

  // Create the simulated pinch gripper connection, which is the bridge between
  // the plugin and the gRPC service.
  actuated_gripper_connection_ = std::make_unique<ActuatedGripperConnection>(
      pinch_gripper_config, [this](const auto& command) -> absl::Status {
        return SetCommand(command);
      });
  // Register pinch gripper to service. If necessary, starts the gRPC service.
  auto& service = SimulatedPinchGripperServerImpl::
      StartSimulatedPinchGripperServiceSingleton();
  if (auto status =
          service.RegisterPinchGripper(actuated_gripper_connection_.get());
      !status.ok()) {
    LOG(ERROR) << "Failed to register pinch gripper: " << status;
  }
}

void ActuatedGripperPlugin::DestroyActuatedGripperConnection() {
  if (actuated_gripper_connection_ != nullptr) {
    auto& service = SimulatedPinchGripperServerImpl::
        StartSimulatedPinchGripperServiceSingleton();
    if (auto status = service.UnRegisterPinchGripper(
            actuated_gripper_connection_->pinch_gripper_handle());
        !status.ok()) {
      LOG(ERROR) << absl::StrFormat(
          "Failed to remove pinch gripper with handle '%s' from service. "
          "Error: "
          "%s",
          actuated_gripper_connection_->pinch_gripper_handle(),
          status.message());
    }
  }
}

void ActuatedGripperPlugin::InitGPIOGripperConnection(
    const intrinsic_proto::eoat::PinchGripperConfig& pinch_gripper_config) {
  CHECK_EQ(gpio_connection_, nullptr)
      << "Cannot initialize gpio gripper connection twice for plugin "
      << plugin_name_ << ".";

  auto set_command = [this](const GripperCommand& command) -> absl::Status {
    return OnCommandGpio(command);
  };
  auto get_status = [this]() -> GripperStatus {
    return fixed_joint_gripper_->GetStatus();
  };
  std::string gpio_connection_handle =
      absl::Substitute("$0[$1]", plugin_name_, parent_entity_);
  gpio_connection_ = std::make_unique<GPIOGripperPluginConnection>(
      pinch_gripper_config, gpio_connection_handle, set_command, get_status);
  auto& gpio_service = GPIOService::StartSingleton();
  if (auto register_status =
          gpio_service.RegisterPluginConnection(gpio_connection_.get());
      !register_status.ok()) {
    LOG(ERROR) << "Failed to register gpio gripper connection for plugin "
               << plugin_name_ << " with status " << register_status;
  } else {
    LOG(INFO) << "Successfully registered gpio gripper connection for plugin "
                 "with gripper id "
              << gpio_connection_handle;
  }
}

void ActuatedGripperPlugin::DestroyGPIOGripperConnection() {
  if (gpio_connection_ == nullptr) {
    return;
  }
  auto& gpio_service = GPIOService::StartSingleton();
  if (!gpio_service
           .UnregisterPluginConnection(gpio_connection_->plugin_handle())
           .ok()) {
    LOG(ERROR) << "Failed to unregister gpio gripper connection for plugin "
               << plugin_name_;
  } else {
    LOG(INFO) << "Successfully unregistered gpio gripper connection for "
              << plugin_name_;
  }
  gpio_connection_ = nullptr;
}

void ActuatedGripperPlugin::PreUpdate(const UpdateInfo& info,
                                      EntityComponentManager& ecm) {
  {
    absl::MutexLock lock(initialization_status_mutex_);
    if (!initialization_status_.ok()) {
      return;
    }
  }

  const double desired_position = [&]() {
    absl::MutexLock lock(command_mutex_);
    return desired_position_;
  }();

  for (auto& joint : joints_) {
    double position =
        GetECMComponentData<JointPosition>(joint.joint_entity, ecm)[0];
    double velocity =
        GetECMComponentData<JointVelocity>(joint.joint_entity, ecm)[0];
    double command = joint.pid.Update(/*_error=*/position - desired_position,
                                      /*_error_rate=*/velocity,
                                      /*_dt=*/info.dt);
    command += joint.gravity_compensator->ComputeTorque();
    auto& force_cmd =
        GetECMComponentData<JointForceCmd>(joint.joint_entity, ecm);
    force_cmd[0] = {command};
  }
  fixed_joint_gripper_->UpdateGraspedObject(ecm);
  fixed_joint_gripper_->ProcessCommand(ecm);
}

double ActuatedGripperPlugin::GetCurrentPosition(
    const EntityComponentManager& ecm) const {
  const double plugin_position =
      ecm.Component<JointPosition>(feedback_joint_entity_)->Data()[0];
  return PluginToCommandPosition(plugin_position);
}

double ActuatedGripperPlugin::GetCurrentPositionDelta(double command_position) {
  double position_delta = std::numeric_limits<double>::max();
  if (last_command_position_.has_value()) {
    position_delta =
        std::abs(command_position - last_command_position_.value());
  }
  last_command_position_ = command_position;
  return position_delta;
}

void ActuatedGripperPlugin::PostUpdate(const UpdateInfo& info,
                                       const EntityComponentManager& ecm) {
  {
    absl::MutexLock lock(initialization_status_mutex_);
    if (!initialization_status_.ok()) {
      return;
    }
  }

  GZ_PROFILE("ActuatedGripperPlugin::PostUpdate");
  absl::MutexLock lock(command_mutex_);

  // Pid joints should be a non-empty list, this is validated in configure.
  CHECK(!joints_.empty());
  // actuated_gripper_connection_ should be non-null.
  CHECK(actuated_gripper_connection_ != nullptr);

  auto gripper_status = actuated_gripper_connection_->GetPinchGripperStatus();
  const double current_position = GetCurrentPosition(ecm);
  gripper_status.set_position(current_position);
  gripper_status.set_gripper_enabled(true);

  // If there is no active command to execute, update the idle status and return
  // early.
  if (!command_.has_value()) {
    gripper_status.set_in_motion(false);
    gripper_status.set_command_id(0);
    actuated_gripper_connection_->SetPinchGripperStatus(gripper_status);
    return;
  }

  const double position_delta = GetCurrentPositionDelta(current_position);
  const double requested_position = GetPositionFromCommand(command_.value());

  const double position_eps = std::abs(current_position - requested_position);
  const bool position_reached = position_eps <= kDefaultJointEpsilon;

  // TODO(b/187423755): Use joint velocity to determine in_motion first.
  // The joint doesn't always settle. Report stopped motion anyways.
  const bool stopped_moving = position_delta <= kDefaultJointDeltaEpsilon;

  // Update the gripper status.
  gripper_status.set_position_reached(position_reached);
  gripper_status.set_position_requested(requested_position);
  gripper_status.set_in_motion(!stopped_moving);
  gripper_status.set_command_id(command_->command_id());

  // Distribute status information (both as a return value through the channel
  // and via pubsub).
  actuated_gripper_connection_->SetPinchGripperStatus(gripper_status);
  if (!pub_.has_value()) {
    LOG(ERROR) << "Update failed. Publisher not initialized.";
    return;
  }
  INTR_RETURN_IF_ERROR(pub_->Publish(gripper_status))
      .With([](StatusBuilder builder) {
        builder.LogError() << "Update failed. Failed to send status update.";
        return;
      });

  GripperCommand sticky_command;
  if (stopped_moving) {
    if (position_reached) {
      // Successful termination.
      // Notify the connecting and clear the command to ensure early
      // termination.
      LOG(INFO) << absl::StrFormat(
          "The gripper stopped and executed successfully. The commanded "
          "position "
          "has been reached with a precision of %f mm.",
          position_eps * 1000.0);
    } else {
      LOG(WARNING) << absl::StrFormat(
          "The Gripper stopped moving and failed to reach the commanded "
          "position. This may happen when the gripper is in collision, e.g. "
          "during a grasping operation.\n"
          "\tReached position: %.2f mm\n"
          "\tCommanded position: %.2f mm",
          current_position * 1000.0, requested_position * 1000.0);
    }

    // We only apply 'stickiness' if there is any likelihood for grasping
    // success and not when the operation simply times out.
    // This is quite brittle logic. We assume that all grasp operations with a
    // commanded position < 2cm (the typical sticky threshold) have the
    // semantics 'grasped object'. I.e. we cannot grasp in simulation by
    // extending pinch grippers. It also means that for tiny objects, we might
    // be in grasping mode before we actually grasp the object.
    // TODO(b/208243294): The code below is too brittle and might fail if try
    // to grasp an object with a command position which is larger than the
    // sticky threshold.
    if (requested_position <= sticky_grip_threshold_) {
      sticky_command.set_command(GripperCommand::GRASP);
      fixed_joint_gripper_->OnCommand(sticky_command);
    }
    actuated_gripper_connection_->NotifyCommandExecuted(gripper_status);
    command_ = std::nullopt;
  } else {
    // Not yet terminated, still moving, check for timeout.
    const absl::Time execution_deadline =
        command_received_ + kDefaultFallbackTimeout;
    if (absl::Now() >= execution_deadline) {
      // Resetting the command ensures early termination of PostUpdate().
      command_ = std::nullopt;

      // Log the different error cases which lead to the timeout.
      if (position_reached) {
        LOG(ERROR) << absl::StrFormat(
            "The gripper reached its position but is still moving after %.2f "
            "seconds.",
            absl::ToDoubleSeconds(kDefaultFallbackTimeout));
      } else {
        LOG(ERROR) << absl::StrFormat(
            "The Gripper is still moving but failed to reach the commanded "
            "position within %.2f seconds.\n"
            "\tReached position: %.2f mm\n"
            "\tCommanded position: %.2f mm",
            absl::ToDoubleSeconds(kDefaultFallbackTimeout),
            current_position * 1000.0, requested_position * 1000.0);
      }
    }
  }

  if (requested_position > sticky_grip_threshold_) {
    sticky_command.set_command(GripperCommand::RELEASE);
    fixed_joint_gripper_->OnCommand(sticky_command);
  }
}

absl::Status ActuatedGripperPlugin::LoadSDF(sdf::Element* sdf,
                                            const EntityComponentManager& ecm) {
  INTR_RETURN_IF_ERROR(SetupPubSub(sdf, ecm))
      .With(intrinsic::ExtraMessage() << "Failed to setup dds");

  sdf->Get(kStickyThresholdElementName, sticky_grip_threshold_,
           kDefaultStickyThreshold);

  sdf->Get(kIsDefaultClosedElementName, is_default_closed_, false);

  if (!sdf->HasElement(kStickyLinkElementName)) {
    return InvalidArgumentErrorBuilder()
           << "ActuatedGripperPlugin must be configured with a valid <"
           << kStickyLinkElementName << "> element";
  }
  sticky_link_name_ = sdf->Get<std::string>(kStickyLinkElementName);

  if (sdf->HasElement(kPinchGripperConfigElementName)) {
    std::string pinch_gripper_config =
        sdf->Get<std::string>(std::string(kPinchGripperConfigElementName));
    pinch_gripper_config_.emplace();
    if (!google::protobuf::TextFormat::ParseFromString(
            pinch_gripper_config, &pinch_gripper_config_.value())) {
      LOG(ERROR) << "Failed to parse plugin field "
                 << kPinchGripperConfigElementName
                 << " as a intrinsic_proto::eoat::PinchGripperConfig";
      pinch_gripper_config_.reset();
    }
  }

  if (sdf->HasElement(kServicePinchGripperConfigElementName)) {
    std::string service_pinch_gripper_config = sdf->Get<std::string>(
        std::string(kServicePinchGripperConfigElementName));
    service_pinch_gripper_config_.emplace();
    if (!google::protobuf::TextFormat::ParseFromString(
            service_pinch_gripper_config,
            &service_pinch_gripper_config_.value())) {
      LOG(ERROR) << "Failed to parse plugin field "
                 << kServicePinchGripperConfigElementName
                 << " as a intrinsic_proto::gripper::PinchGripperConfig";
      service_pinch_gripper_config_.reset();
    }
  }

  if (sdf->HasElement(kPinchGripperHandleElementName)) {
    pinch_gripper_handle_ =
        sdf->Get<std::string>(std::string(kPinchGripperHandleElementName));
  } else {
    pinch_gripper_handle_ = ::gz::sim::scopedName(parent_entity_, ecm);
  }

  return absl::OkStatus();
}

absl::Status ActuatedGripperPlugin::SetupPubSub(
    sdf::Element* sdf, const EntityComponentManager& ecm) {
  LOG(INFO) << "'" << plugin_name_ << "' Configuring pub/sub.";

  if (sdf->HasElement(kStatusTopicElementName) &&
      !sdf->GetElement(kStatusTopicElementName)->Get<std::string>().empty()) {
    topic_status_ =
        sdf->GetElement(kStatusTopicElementName)->Get<std::string>();
  }

  std::string topic_prefix;
  if (sdf->HasElement(kTopicPrefixElementName)) {
    topic_prefix = sdf->GetElement(kTopicPrefixElementName)->Get<std::string>();
  } else {
    topic_prefix = ::gz::sim::topicFromScopedName(parent_entity_, ecm);
  }
  if (!topic_prefix.empty()) {
    topic_status_ = absl::StrJoin({topic_prefix, topic_status_}, "/");
  }

  INTR_ASSIGN_OR_RETURN(pub_,
                        pubsub_.CreatePublisher(topic_status_, TopicConfig()));

  LOG(INFO) << "'" << plugin_name_ << "' publishing to status topic: '"
            << topic_status_ << "'.";

  LOG(INFO) << "Setup pubs.";
  return absl::OkStatus();
}

void ActuatedGripperPlugin::ControlCommand(double command_position) {
  if (feedback_joint_entity_ == kNullEntity) {
    LOG(ERROR) << "Feedback joint entity is missing. Desired target position "
                  "for gripper has not been set.";
    return;
  }
  absl::MutexLock lock(command_mutex_);
  desired_position_ = CommandToPluginPosition(command_position);
}

absl::Status ActuatedGripperPlugin::OnCommandGpio(
    const intrinsic_proto::simulation::gazebo::GripperCommand& command) {
  {
    absl::MutexLock lock(initialization_status_mutex_);
    if (absl::IsUnavailable(initialization_status_)) {
      return initialization_status_;
    } else if (!initialization_status_.ok()) {
      return FailedPreconditionErrorBuilder()
             << "ActuatedGripperPlugin initialization failed with: "
             << initialization_status_;
    }
  }

  LOG(INFO) << plugin_name_ << " received command: " << command;
  PinchGripperCommand pinch_gripper_command;
  if (command.command() == GripperCommand::GRASP) {
    pinch_gripper_command.set_position_percentage(0.0);
  } else if (command.command() == GripperCommand::RELEASE) {
    pinch_gripper_command.set_position_percentage(100.0);
  } else {
    return absl::InvalidArgumentError(
        absl::StrCat("Gripper command ", command,
                     " is not applicable for ActuatedGripperPlugin"));
  }
  return SetCommand(pinch_gripper_command);
}

double ActuatedGripperPlugin::GetPositionFromPercentage(
    double percentage) const {
  if (percentage < 0.0 || percentage > 100.0) {
    LOG(WARNING) << "Received percentage out of the range of 0.0 to 100. The "
                    "percentage will be clamped";
  }
  const double clamped_percentage = std::clamp(percentage, 0.0, 100.0);

  const double joint_range =
      feedback_joint_upper_limit_ - feedback_joint_lower_limit_;
  return 2.0 * joint_range * clamped_percentage / 100.0;
}

double ActuatedGripperPlugin::GetPositionFromCommand(
    const PinchGripperCommand& command) const {
  if (command.has_position()) {
    return command.position();
  } else {
    // Command should be either position or position_percentage. This is
    // validated in SetCommand.
    CHECK(command.has_position_percentage());
    return GetPositionFromPercentage(command.position_percentage());
  }
}

absl::Status ActuatedGripperPlugin::SetCommand(
    const PinchGripperCommand& command) {
  {
    absl::MutexLock lock(initialization_status_mutex_);
    if (absl::IsUnavailable(initialization_status_)) {
      return initialization_status_;
    } else if (!initialization_status_.ok()) {
      return FailedPreconditionErrorBuilder()
             << "ActuatedGripperPlugin initialization failed with: "
             << initialization_status_;
    }
  }

  LOG_EVERY_N_SEC(INFO, 3) << plugin_name_ << " Received command: " << command;
  if (!(command.has_position() || command.has_position_percentage())) {
    return InvalidArgumentErrorBuilder()
           << "PinchGripperCommand must have either position or "
              "position_percentage set to work in simulation.";
  }
  {
    absl::MutexLock lock(command_mutex_);
    command_ = command;
    command_received_ = absl::Now();
    last_command_position_ = std::nullopt;
  }
  ControlCommand(GetPositionFromCommand(command));
  return absl::OkStatus();
}

double ActuatedGripperPlugin::CommandToPluginPosition(
    double command_position) const {
  if (is_default_closed_) {
    return 0.5 * command_position + feedback_joint_lower_limit_;
  } else {
    return feedback_joint_upper_limit_ - 0.5 * command_position;
  }
}

double ActuatedGripperPlugin::PluginToCommandPosition(
    double plugin_position) const {
  if (is_default_closed_) {
    return 2.0 * (plugin_position - feedback_joint_lower_limit_);
  } else {
    return 2.0 * (feedback_joint_upper_limit_ - plugin_position);
  }
}

}  // namespace simulation
}  // namespace intrinsic
