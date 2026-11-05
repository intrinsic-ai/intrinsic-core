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

#ifndef INTRINSIC_SIMULATION_GAZEBO_PLUGINS_GRIPPERS_ACTUATED_GRIPPER_PLUGIN_H_
#define INTRINSIC_SIMULATION_GAZEBO_PLUGINS_GRIPPERS_ACTUATED_GRIPPER_PLUGIN_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "gtest/gtest_prod.h"
#include "gz/math/PID.hh"
#include "gz/sim/Entity.hh"
#include "gz/sim/EntityComponentManager.hh"
#include "gz/sim/EventManager.hh"
#include "gz/sim/System.hh"
#include "gz/sim/Types.hh"
#include "gz/transport/Node.hh"
#include "intrinsic/hardware/gripper/eoat/gripper_config.pb.h"
#include "intrinsic/hardware/gripper/gripper.pb.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/simulation/gazebo/plugins/gravity_compensator.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/actuated_gripper_plugin_connection.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/fixed_joint_gripper.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/gpio_gripper_plugin_connection.h"
#include "intrinsic/simulation/gazebo/plugins/grippers/gripper.pb.h"
#include "sdf/Element.hh"

namespace intrinsic {
namespace simulation {

class ActuatedGripperPlugin final : public ::gz::sim::System,
                                    public ::gz::sim::ISystemConfigure,
                                    public ::gz::sim::ISystemPreUpdate,
                                    public ::gz::sim::ISystemPostUpdate {
 public:
  using Entity = ::gz::sim::Entity;
  using EntityComponentManager = ::gz::sim::EntityComponentManager;
  using EventManager = ::gz::sim::EventManager;
  using UpdateInfo = ::gz::sim::UpdateInfo;

  using PinchGripperCommand = ::intrinsic_proto::gripper::PinchGripperCommand;
  using PinchGripperStatus = ::intrinsic_proto::gripper::PinchGripperStatus;

  ~ActuatedGripperPlugin() override;

  void Configure(const Entity& entity,
                 const std::shared_ptr<const sdf::Element>& sdf,
                 EntityComponentManager& ecm, EventManager& eventMgr) override;

  void PreUpdate(const UpdateInfo& info, EntityComponentManager& ecm)
      ABSL_LOCKS_EXCLUDED(command_mutex_) override;

  // This function is called on every world update, after Gazebo has performed
  // its physics simulation step.
  void PostUpdate(const UpdateInfo& info,
                  const EntityComponentManager& ecm) override;

  absl::Status SetCommand(const PinchGripperCommand& command)
      ABSL_LOCKS_EXCLUDED(command_mutex_);

  absl::Status OnCommandGpio(
      const intrinsic_proto::simulation::gazebo::GripperCommand& command);

 private:
  double GetCurrentPosition(const EntityComponentManager& ecm) const;
  double GetCurrentPositionDelta(double command_position);

  double GetPositionFromPercentage(double percentage) const;
  double GetPositionFromCommand(const PinchGripperCommand& command) const;

  absl::Status LoadSDF(sdf::Element* LoadSDF,
                       const EntityComponentManager& ecm);
  void ControlCommand(double command_position)
      ABSL_LOCKS_EXCLUDED(command_mutex_);

  // SimulatedPinchGripperConnection: a bridge between this plugin and the
  // Simulated Pinch Gripper gRPC service
  void InitActuatedGripperConnection();
  void DestroyActuatedGripperConnection();

  void InitGPIOGripperConnection(
      const intrinsic_proto::eoat::PinchGripperConfig& pinch_gripper_config);
  void DestroyGPIOGripperConnection();

  // Given the proto joint position in SI units, return the plugin compatible
  // joint position.
  // Internally, we need a conversion factor because in simulation the gripper
  // is split into 2 separate joints, each covering half the opening.
  //
  // Example:
  //
  //     |...|   <- proto_joint_position: 6 (defines the grasp width)
  //         |.| <- plugin_joint_position: 1 (defines a single joint)
  //   |_______| <- plugin_joint_min: 0, plugin_joint_max: 4, proto_joint_max: 8
  //       |
  double CommandToPluginPosition(double command_position) const;

  // Given a plugin compatible joint position the function returns the proto
  // joint position in SI units.
  double PluginToCommandPosition(double plugin_position) const;

  // We consider the joint goal to be reached if we are 1 mm or close to it.
  static constexpr double kDefaultJointEpsilon = 0.001;  // 1mm

  // We consider the gripper motion to have stopped if it does not move more
  // than 1/100th of a mm.
  static constexpr double kDefaultJointDeltaEpsilon = 0.00001;  // 0.01mm

  // Report stopped 'kDefaultFallbackTimeout' seconds after receiving a command
  // as the `feedback_joint_` sometimes doesn't settle.
  static constexpr absl::Duration kDefaultFallbackTimeout = absl::Seconds(7);

  // Command and Status topics and/or topic prefix are settable from SDF
  // topic prefix will default to model's scoped name if omitted.
  static constexpr char kStatusTopicElementName[] = "status_topic";
  static constexpr char kTopicPrefixElementName[] = "topic_prefix";
  static constexpr char kPinchGripperHandleElementName[] =
      "pinch_gripper_handle";

  static constexpr char kStickyThresholdElementName[] =
      "sticky_position_threshold";
  // Default sticky threshold to a small positive value and will be overridden
  // by a value set in the plugin xml.
  static constexpr double kDefaultStickyThreshold = 0.02;  // meters
  static constexpr char kPinchGripperConfigElementName[] =
      "pinch_gripper_config";
  static constexpr char kServicePinchGripperConfigElementName[] =
      "service_pinch_gripper_config";
  static constexpr char kStickyLinkElementName[] = "sticky_link";

  // Whether the gripper is closed when all joints are at their lower limit.
  static constexpr char kIsDefaultClosedElementName[] = "is_default_closed";
  bool is_default_closed_ = false;

  absl::Status SetupPubSub(sdf::Element* sdf,
                           const EntityComponentManager& ecm);

  std::string plugin_name_;
  Entity parent_entity_;

  struct ControlledJoint {
    Entity joint_entity;
    ::gz::math::PID pid;
    std::unique_ptr<GravityCompensator> gravity_compensator;
  };
  std::vector<ControlledJoint> joints_;

  // Called to configure joints_ in `Configure(...)`.
  static absl::StatusOr<std::vector<ControlledJoint>> ConfigureJoints(
      Entity gripper_entity, const ::sdf::Element* sdf_element,
      ::gz::sim::EntityComponentManager& ecm);

  Entity feedback_joint_entity_ = ::gz::sim::kNullEntity;
  double feedback_joint_upper_limit_;
  double feedback_joint_lower_limit_;

  absl::Mutex initialization_status_mutex_;
  absl::Status initialization_status_
      ABSL_GUARDED_BY(initialization_status_mutex_) = absl::UnavailableError(
          "ActuatedGripperPlugin hasn't finished initializing.");

  std::unique_ptr<ActuatedGripperConnection> actuated_gripper_connection_;

  // Optionally stores the gpio signals needed to simulate a EOAT pinch
  // gripper. The signals are used to configure gpio_connection_
  std::optional<intrinsic_proto::eoat::PinchGripperConfig>
      pinch_gripper_config_;
  // Optionally contains PubSub config.
  std::optional<intrinsic_proto::gripper::PinchGripperConfig>
      service_pinch_gripper_config_;
  std::unique_ptr<GPIOGripperPluginConnection> gpio_connection_;

  PubSub pubsub_;
  std::optional<Publisher> pub_;
  std::string topic_status_ = "gripper/status";

  std::string pinch_gripper_handle_;

  absl::Mutex command_mutex_;
  absl::Time command_received_ ABSL_GUARDED_BY(command_mutex_);
  // This parameter is set during SetCommand() and it is cleared either when
  // the gripper has been successfully commanded or when for some reason the
  // gripper does not settle in the specified time kDefaultFallbackTimeout.
  std::optional<PinchGripperCommand> command_ ABSL_GUARDED_BY(command_mutex_);
  double desired_position_ ABSL_GUARDED_BY(command_mutex_) = 0.0;

  // This parameter is set during each PostUpdate(). The parameter is cleared
  // when SetCommand() is called.
  std::optional<double> last_command_position_;

  double sticky_grip_threshold_;

  ::gz::transport::Node node_;
  ::gz::transport::Node::Publisher output_pub_;

  std::string sticky_link_name_;
  std::unique_ptr<FixedJointGripper> fixed_joint_gripper_;

  FRIEND_TEST(ActuatedGripperPluginTest, ValidConfigure);
  FRIEND_TEST(ActuatedGripperPluginTest, ConfigureUsesJointPositionReset);
  FRIEND_TEST(ActuatedGripperPluginTest, InvalidConfigure);
  FRIEND_TEST(ActuatedGripperPluginTest, GraspWithoutContact);
  FRIEND_TEST(ActuatedGripperPluginTest, GraspAndRelease);
};

}  // namespace simulation
}  // namespace intrinsic

#endif  // INTRINSIC_SIMULATION_GAZEBO_PLUGINS_GRIPPERS_ACTUATED_GRIPPER_PLUGIN_H_
