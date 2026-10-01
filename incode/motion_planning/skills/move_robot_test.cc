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

#include "incode/motion_planning/skills/move_robot.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "google/protobuf/util/message_differencer.h"
#include "internal/testing.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/assets/testing/id_test_utils.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/actions/adio_info.h"
#include "intrinsic/icon/actions/trajectory_tracking_action_info.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/cc_client/state_variable_path.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_channel_fake.h"
#include "intrinsic/icon/cc_client/testing/rtcl_controller_fake_session.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/common/id_types.h"
#include "intrinsic/icon/equipment/channel_factory.h"
#include "intrinsic/icon/equipment/equipment_utils.h"
#include "intrinsic/icon/equipment/icon_equipment.pb.h"
#include "intrinsic/icon/equipment/testing/equipment_test_utils.h"
#include "intrinsic/icon/equipment/testing/fake_channel_factory.h"
#include "intrinsic/icon/proto/cart_space_conversion.h"
#include "intrinsic/icon/proto/generic_part_config.pb.h"
#include "intrinsic/icon/proto/io_block.pb.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/skills/update_robot_joint_positions.pb.h"
#include "intrinsic/kinematics/types/cartesian_limits.h"
#include "intrinsic/kinematics/types/joint_limits.h"
#include "intrinsic/kinematics/types/joint_limits_matcher.h"
#include "intrinsic/kinematics/types/joint_limits_xd.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto_conversion.h"
#include "intrinsic/motion_planning/service/motion_planner_service.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/service/motion_planner_service_proxy.h"
#include "intrinsic/motion_planning/service/motion_planner_service_utils.h"
#include "intrinsic/motion_planning/skills/move_robot.pb.h"
#include "intrinsic/motion_planning/service/motion_planner_service_in_process.h"
#include "intrinsic/skills/apps/testing/test_world.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_data.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/internal/skill_service_client.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skill_manifest.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/testing/skill_test_utils.h"
#include "intrinsic/util/eigen.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/thread.h"
#include "intrinsic/world/component/robot_component.h"
#include "intrinsic/world/entity.h"
#include "intrinsic/world/entity_id.h"
#include "intrinsic/world/grouping.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/test/object_world_test_utils.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"

namespace intrinsic {
namespace skills {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::Not;

constexpr absl::string_view kRobotLabel = "agilus-04";
constexpr char kObjectName[] = "tool_target";
char const kOriginalWorldId[] = "my_world";
char const kExecutionWorldId[] = "execute_test_world";
constexpr char kEchoManifestPath[] =
    "incode/motion_planning/skills/move_robot_manifest.pbbin";

constexpr char kDioBlock[] = "digital_in";
constexpr uint32_t kSignalIndex = 0;

constexpr std::string_view kTestMotionPlannerServiceAssetVersion =
    "0.20260427.5-RC17";
struct MoveRobotTestParams {
  absl::string_view world_id = kExecutionWorldId;
  double report_elapsed_stop_time_seconds = 0.0;
  bool report_is_settled = true;
  bool test_move_to_signal = false;
  bool report_tracking_done = true;
  std::vector<icon::ActionWill> event_action_will_list = {};
  icon::RtclControllerFakeSession::ValidationMode validation_mode =
      icon::RtclControllerFakeSession::ValidationMode::kNice;
};

absl::StatusOr<intrinsic_proto::status::ExtendedStatus> GetExtendedStatus(
    absl::Status status) {
  std::optional<absl::Cord> result_payload = status.GetPayload(AddTypeUrlPrefix(
      intrinsic_proto::status::ExtendedStatus::default_instance()));
  if (!result_payload.has_value()) {
    return absl::NotFoundError("ExtendedStatus not found.");
  }
  intrinsic_proto::status::ExtendedStatus result_proto;
  if (!result_proto.ParseFromString(*result_payload)) {
    return absl::NotFoundError("ExtendedStatus could not be parsed.");
  }
  return result_proto;
}

absl::Status SetCartesianLimitsInWorld(World& world,
                                       std::string_view robot_name,
                                       CartesianLimits const& cart_limits) {
  for (auto const& robot_id :
       world.GetTypedEntityIds<RobotCollectionsEntityId>()) {
    INTR_ASSIGN_OR_RETURN(WorldEntity const* entity,
                          world.GetEntityById(robot_id));
    if (entity->GetLocalName() == robot_name ||
        entity->GetAlias() == robot_name) {
      INTR_ASSIGN_OR_RETURN(
          RobotComponent * component,
          world.GetComponentByEntityId<RobotComponent>(robot_id));
      INTR_RETURN_IF_ERROR(component->SetCartesianLimits(cart_limits));

      return absl::OkStatus();
    }
  }
  return absl::NotFoundError(
      absl::StrCat("Robot ", robot_name, " not found in world."));
}

TEST(MoveRobotTest, ConstructDestruct) {
  MoveRobot move_robot(std::make_unique<icon::DefaultChannelFactory>());
}

TEST(MoveRobotTest, CreateSkill) {
  std::unique_ptr<SkillInterface> planned_move = MoveRobot::CreateSkill();
}

struct MoveRobotTestParameter {
  std::string test_name;
  bool use_motion_planner_service_asset;
};

class MoveRobotFixtureTest
    : public ::testing::TestWithParam<MoveRobotTestParameter> {
 protected:
  MoveRobotFixtureTest()
      : manifest_(GetManifestOrDie(kEchoManifestPath)),
        original_world_(World::CreateEmptyWorld()) {}

  void SetUp() override {
    use_motion_planner_service_asset_ =
        GetParam().use_motion_planner_service_asset;
    ASSERT_OK_AND_ASSIGN(world_service_, FakeWorldService::Create());
    world_service_stub_ = world_service_->NewStub();
    object_world_service_stub_ = world_service_->NewObjectStub();
    motion_planner_service_in_process_ = MotionPlannerServiceInProcess::Create(
        world_service_stub_.get(), object_world_service_stub_.get(),
        world_service_->GetGeometryLibrary());
    motion_planner_stub_ = motion_planner_service_in_process_->GetStub();
    basic_params_ = intrinsic_proto::skills::MoveRobotParams();
    if (use_motion_planner_service_asset_) {
      LOG(INFO) << "Initiating ResolvedDependency on MotionPlannerService for "
                   "MoveRobotParams ...";
      std::unique_ptr<intrinsic_proto::motion_planning::v1::
                          MotionPlannerService::StubInterface>
          mps_stub_for_mps_proxy =
              motion_planner_service_in_process_->GetUniqueStub();
      ASSERT_OK_AND_ASSIGN(motion_planner_service_proxy_,
                           MotionPlannerServiceProxy::Create(
                               std::move(mps_stub_for_mps_proxy),
                               kTestMotionPlannerServiceAssetVersion));
      intrinsic_proto::assets::v1::ResolvedDependency::Interface interface =
          skill_test_factory_.RunService(motion_planner_service_proxy_.get(),
                                         kMotionPlannerServiceName);
      basic_params_.mutable_motion_planner_service()
          ->mutable_interfaces()
          ->insert({kMotionPlannerServiceInterface, interface});
    }

    ASSERT_OK_AND_ASSIGN(
        original_world_,
        testing::LoadTestWorld(
            "intrinsic/world/test_data/4_robots_cube_world.gzf"));

    Pose3d desired_pose = Pose3d(
        eigenmath::Quaterniond({0.00243315, -0.707967, 0.00293097, 0.706235}),
        eigenmath::Vector3d({0.285883, 0.622421, 0.862182}));
    CHECK_OK(object_world::CreateEntityForFrame(
        &original_world_, {.name = FrameName(kObjectName),
                           .parent_name = RootObjectName(),
                           .parent_t_this = desired_pose}));

    // Set the acceleration limits pretty high for testing purposes.
    for (const absl::string_view robot :
         {kRobotLabel, absl::string_view("agilus-03")}) {
      ASSERT_OK_AND_ASSIGN(
          auto dof_view,
          original_world_.GetDofKinematicViewForRobotGroupId(GroupId(robot)));
      JointLimitsXd application_limits = dof_view->GetDofApplicationLimits();
      application_limits.max_acceleration.setConstant(6, 50);
      ASSERT_OK(dof_view->SetDofApplicationLimits(application_limits,
                                                  /*enforce_limits=*/true));

      JointLimitsXd system_limits = dof_view->GetDofSystemLimits();
      system_limits.max_acceleration.setConstant(6, 50);
      ASSERT_OK(
          dof_view->SetDofSystemLimits(system_limits, /*enforce_limits=*/true));
    }

    ASSERT_OK(
        world_service_->AddWorld(kOriginalWorldId, original_world_.Clone()));
    world_ = world::ObjectWorldClient(kOriginalWorldId,
                                      world_service_->NewObjectStub());
  }

  intrinsic_proto::skills::MoveRobotParams CreateJointTargetParams() {
    intrinsic_proto::skills::MoveRobotParams params = basic_params_;
    auto* motion_segment = params.add_motion_segments();
    *motion_segment = ParseTextProtoOrDie(R"pb(
      joint_position {
        # Temporarily disabled test configurations due to high planning times
        # which causes flakiness (b/299175802)
        # joints: [ -0.9, 0.4, -0.09612, -0.705302, 0.673298, -1.554707 ]
        joints: [ -0.2, 0.4, -1.3, -0.705302, 0.773298, -1.054707 ]
      }
    )pb");
    return params;
  }

  absl::Status CreateWorldWithRobotConfig(
      absl::string_view world_id,
      eigenmath::VectorNd const& initial_joint_configuration) {
    World new_world = original_world_.Clone();
    INTR_ASSIGN_OR_RETURN(
        auto dof_view,
        new_world.GetDofKinematicViewForRobotGroupId(GroupId(kRobotLabel)));
    INTR_RETURN_IF_ERROR(dof_view->SetDofValues(initial_joint_configuration,
                                                /*enforce_limits=*/false));
    INTR_RETURN_IF_ERROR(
        world_service_->AddWorld(world_id, std::move(new_world)));

    return absl::OkStatus();
  }

  absl::StatusOr<intrinsic_proto::icon::GenericPartConfig> GetIconPartConfig(
      absl::string_view world_id) {
    EquipmentPack equipment;
    INTR_RETURN_IF_ERROR(equipment.Add(
        MoveRobot::kEquipmentSlot,
        icon::Icon2EquipmentHandleBuilder(kRobotLabel, kRobotLabel)
            .WithPositionControlledPart(icon::test_part_names::kArmName)
            .WithAdioPart(icon::test_part_names::kADIOName)
            .Build()));

    // Get arm position part from equipment.
    std::optional<intrinsic_proto::world::ObjectReference> arm_part_object;
    std::optional<std::string> arm_part_name;
    INTR_ASSIGN_OR_RETURN(
        auto position_part,
        equipment.Unpack<intrinsic_proto::icon::Icon2PositionPart>(
            MoveRobot::kEquipmentSlot, icon::kIcon2PositionPartKey),
        _ << "Failed to find Icon2PositionPart in equipment data.");

    if (position_part.object_names().empty()) {
      return absl::InvalidArgumentError(
          "No arm_part was given, and none were provided in the map.");
    }

    arm_part_object.emplace();
    arm_part_object->mutable_by_name()->set_object_name(
        position_part.object_names().begin()->second);
    arm_part_name = position_part.object_names().begin()->first;

    if (!arm_part_object.has_value()) {
      // This shouldn't happen unless a coding mistake leaves arm_part unset.
      return absl::InternalError("Failed to deduce arm part name.");
    }

    INTR_ASSIGN_OR_RETURN(
        auto world_object,
        world::ObjectWorldClient(world_id, world_service_->NewObjectStub())
            .GetObject(*arm_part_object));

    std::string object_name = world_object.Name().value();
    std::vector<absl::string_view> object_names;
    object_names.reserve(position_part.object_names().size());

    // Now do a look up against the position parts.
    for (auto const& [current_part_name, current_object_name] :
         position_part.object_names()) {
      object_names.push_back(current_object_name);
      if (current_object_name == object_name) {
        arm_part_name = current_part_name;
        break;
      }
    }

    if (!arm_part_name) {
      return absl::InvalidArgumentError("No arm_part was provided in the map.");
    }

    auto session_actions = icon::SessionWill();
    INTR_ASSIGN_OR_RETURN(auto channel_fake,
                          icon::RtclControllerChannelFake::Builder()
                              .OnNextSession(session_actions)
                              .Build(icon::DefaultPartConfigs()));
    icon::Client icon_client(channel_fake);
    INTR_ASSIGN_OR_RETURN(auto robot_config, icon_client.GetConfig());
    return robot_config.GetGenericPartConfig(arm_part_name.value());
  }

  struct PreparedSkill {
    std::unique_ptr<MoveRobot> skill;
    EquipmentPack equipment;
  };

  absl::StatusOr<PreparedSkill> PrepareMoveRobotTest(
      eigenmath::VectorNd const& initial,
      MoveRobotTestParams const& test_params) {
    INTR_RETURN_IF_ERROR(
        CreateWorldWithRobotConfig(test_params.world_id, initial));
    EquipmentPack equipment;
    INTR_RETURN_IF_ERROR(equipment.Add(
        MoveRobot::kEquipmentSlot,
        icon::Icon2EquipmentHandleBuilder(kRobotLabel, kRobotLabel)
            .WithPositionControlledPart(icon::test_part_names::kArmName)
            .WithAdioPart(icon::test_part_names::kADIOName)
            .Build()));

    intrinsic_proto::icon::v1::GetStatusResponse initial_status_resp;
    intrinsic_proto::icon::PartStatus part_status;
    for (int i = 0; i < initial.size(); ++i) {
      part_status.add_joint_states()->set_position_sensed(initial[i]);
    }
    initial_status_resp.mutable_part_status()->insert(
        {icon::test_part_names::kArmName, part_status});

    intrinsic_proto::icon::PartStatus adio_part_status;
    intrinsic_proto::icon::DioBlock dio_block;
    intrinsic_proto::icon::DigitalSignal signal;
    signal.set_value(true);
    dio_block.mutable_signals()->insert({kSignalIndex, signal});
    adio_part_status.mutable_adio_state()->mutable_digital_inputs()->insert(
        {kDioBlock, dio_block});
    initial_status_resp.mutable_part_status()->insert(
        {icon::test_part_names::kADIOName, adio_part_status});

    // Build our steps
    auto session_actions = icon::SessionWill();
    auto parameter_matcher = [](google::protobuf::Any const& params) {
      icon::TrajectoryTrackingActionInfo::FixedParams
          trajectory_tracking_action_params;
      if (!params.UnpackTo(&trajectory_tracking_action_params)) {
        return absl::InvalidArgumentError("Failed to unpack parameters.");
      }
      return absl::OkStatus();
    };

    {
      icon::ActionWill trajectory_action_will =
          icon::ActionWill().ExpectParameter(parameter_matcher);
      auto report_state_variables = [&](icon::ActionWill& action_will) {
        if (!test_params.test_move_to_signal) {
          action_will.ReportStateVariables({
              {icon::kIsDone, false},
              {icon::TrajectoryTrackingActionInfo::kTrajectoryProgress, 0.0},
              {icon::TrajectoryTrackingActionInfo::kTrajectoryDoneForSeconds,
               0},
              {icon::TrajectoryTrackingActionInfo::kIsSettled, false},
          });
          action_will.ReportStateVariables({
              {icon::kIsDone, test_params.report_tracking_done},
              {icon::TrajectoryTrackingActionInfo::kTrajectoryProgress,
               test_params.report_tracking_done ? 1.0 : 0.0},
              {icon::TrajectoryTrackingActionInfo::kTrajectoryDoneForSeconds,
               test_params.report_elapsed_stop_time_seconds},
              {icon::TrajectoryTrackingActionInfo::kIsSettled,
               test_params.report_is_settled},
          });
        } else {
          action_will.ReportStateVariables({
              {intrinsic::icon::kIsDone, false},
              {icon::TrajectoryTrackingActionInfo::kTrajectoryProgress, 0.0},
              {icon::TrajectoryTrackingActionInfo::kTrajectoryDoneForSeconds,
               0},
              {icon::TrajectoryTrackingActionInfo::kIsSettled, false},
              {icon::ADIODigitalInputStateVariablePath(
                   icon::test_part_names::kADIOName, kDioBlock, kSignalIndex),
               true},
          });
          action_will.ReportStateVariables({
              {intrinsic::icon::kIsDone, true},
              {icon::TrajectoryTrackingActionInfo::kTrajectoryProgress, 0.0},
              {icon::TrajectoryTrackingActionInfo::kTrajectoryDoneForSeconds,
               0},
              {icon::TrajectoryTrackingActionInfo::kIsSettled, true},
              {icon::ADIODigitalInputStateVariablePath(
                   icon::test_part_names::kADIOName, kDioBlock, kSignalIndex),
               true},
          });
        }
      };

      if (test_params.event_action_will_list.empty()) {
        report_state_variables(trajectory_action_will);
        session_actions.OnAction(icon::ActionInstanceId(0),
                                 trajectory_action_will);
      } else {
        session_actions.OnAction(icon::ActionInstanceId(0),
                                 trajectory_action_will);
        // The expectations for the events. We expect the event action to start
        // before we finish the trajectory.
        int action_instance_id = 2;
        for (auto const& event_action_will :
             test_params.event_action_will_list) {
          session_actions.OnAction(icon::ActionInstanceId(action_instance_id),
                                   event_action_will);
          action_instance_id++;
          // This is for the empty action which starts directly after the event
          // action. It has no fixed parameters.
          session_actions.OnAction(icon::ActionInstanceId(action_instance_id),
                                   icon::ActionWill());
          action_instance_id++;
        }
        icon::ActionWill trajectory_action_will_after_event;
        report_state_variables(trajectory_action_will_after_event);
        session_actions.ForAlreadyRunningAction(
            icon::ActionInstanceId(0), trajectory_action_will_after_event);
      }
    }
    {
      icon::ActionWill final_stop_action_will = icon::ActionWill();
      final_stop_action_will.ReportStateVariablesInOrder({
          {
              {icon::kIsDone, true},
          },
      });
      session_actions.OnAction(icon::ActionInstanceId(1),
                               final_stop_action_will);
    }

    INTR_ASSIGN_OR_RETURN(
        auto channel_fake,
        icon::RtclControllerChannelFake::Builder(test_params.validation_mode)
            .OnNextSession(session_actions)
            .WithInitialRobotStatus(initial_status_resp)
            .Build(icon::DefaultPartConfigs()));
    INTR_RETURN_IF_ERROR(icon::Client(channel_fake).Enable());

    auto skill = std::make_unique<MoveRobot>(
        std::make_unique<icon::FakeChannelPassThroughFactory>(
            std::move(channel_fake)));

    return PreparedSkill{
        .skill = std::move(skill),
        .equipment = std::move(equipment),
    };
  }

  // Do not call twice in the same test with the same world_id. The world is
  // modified from the initial state created during SetUp().
  absl::StatusOr<intrinsic_proto::skills::MoveRobotReturnValue>
  ExecuteMoveRobotTest(
      intrinsic_proto::skills::MoveRobotParams const& params,
      eigenmath::VectorNd const& initial,
      MoveRobotTestParams const& test_params = MoveRobotTestParams(),
      bool expect_success = true) {
    INTR_ASSIGN_OR_RETURN(auto skill_and_equipment,
                          PrepareMoveRobotTest(initial, test_params));

    const ExecuteRequest execute_request =
        skill_test_factory_.MakeExecuteRequest(params);
    auto execute_context = skill_test_factory_.MakeExecuteContext({
        .equipment_pack = skill_and_equipment.equipment,
        .world_id = std::string(test_params.world_id),
        .motion_planner_service = motion_planner_stub_,
        .object_world_service = world_service_->NewObjectStub(),
    });

    intrinsic_proto::skills::MoveRobotReturnValue return_value;
    if (use_motion_planner_service_asset_ && expect_success) {
      absl::ScopedMockLog mps_asset_mock_log;
      EXPECT_CALL(mps_asset_mock_log,
                  Log(absl::LogSeverity::kInfo, _,
                      "MotionPlannerService Asset was successfully "
                      "retrieved... Creating MotionPlannerClient..."))
          .Times(1);
      EXPECT_CALL(mps_asset_mock_log,
                  Log(absl::LogSeverity::kInfo, _,
                      absl::StrCat("Installed MPS Asset Version: ",
                                   kTestMotionPlannerServiceAssetVersion)))
          .Times(1);
      mps_asset_mock_log.StartCapturingLogs();

      INTR_RETURN_IF_ERROR(ExecuteSkill(*skill_and_equipment.skill,
                                        execute_request, *execute_context,
                                        &return_value));
    } else {
      INTR_RETURN_IF_ERROR(ExecuteSkill(*skill_and_equipment.skill,
                                        execute_request, *execute_context,
                                        &return_value));
    }

    return return_value;
  }

  // Do not call twice in the same test with the same world_id. The world is
  // modified from the initial state created during SetUp().
  absl::Status PreviewMoveRobotTest(
      intrinsic_proto::skills::MoveRobotParams const& params,
      eigenmath::VectorNd const& initial,
      absl::string_view world_id = "preview_test_world") {
    MoveRobotTestParams test_params{
        .world_id = world_id,
        .report_elapsed_stop_time_seconds = 1.0,
        .report_is_settled = false,
        .test_move_to_signal = false,
    };
    INTR_ASSIGN_OR_RETURN(auto skill_and_equipment,
                          PrepareMoveRobotTest(initial, test_params));

    PreviewRequest preview_request =
        skill_test_factory_.MakePreviewRequest(params);
    auto preview_context = skill_test_factory_.MakePreviewContext({
        .equipment_pack = skill_and_equipment.equipment,
        .world_id = std::string(world_id),
        .motion_planner_service = motion_planner_stub_,
        .object_world_service = world_service_->NewObjectStub(),
    });

    return PreviewSkill(*skill_and_equipment.skill, preview_request,
                        *preview_context);
  }

  // Do not call twice in the same test with the same world_id. The world is
  // modified from the initial state created during SetUp().
  absl::StatusOr<intrinsic_proto::skills::MoveRobotInternalData>
  ComputePlanMoveRobotTest(
      intrinsic_proto::skills::MoveRobotParams const& params,
      eigenmath::VectorNd const& initial,
      absl::string_view world_id = "compute_plan_test_world",
      bool expect_success = true) {
    MoveRobotTestParams test_params{
        .world_id = world_id,
        .report_elapsed_stop_time_seconds = 1.0,
        .report_is_settled = false,
        .test_move_to_signal = false,
    };
    INTR_ASSIGN_OR_RETURN(auto skill_and_equipment,
                          PrepareMoveRobotTest(initial, test_params));

    auto* move_robot =
        dynamic_cast<MoveRobot*>(skill_and_equipment.skill.get());
    CHECK(move_robot != nullptr);

    auto world =
        world::ObjectWorldClient(world_id, world_service_->NewObjectStub());
    INTR_ASSIGN_OR_RETURN(
        auto robot, world.GetKinematicObject(WorldObjectName(kRobotLabel)));

    motion_planning::MotionPlannerClient planner(world_id,
                                                 motion_planner_stub_);
    if (use_motion_planner_service_asset_ && expect_success) {
      absl::ScopedMockLog mps_asset_mock_log;
      EXPECT_CALL(mps_asset_mock_log,
                  Log(absl::LogSeverity::kInfo, _,
                      "MotionPlannerService Asset was successfully "
                      "retrieved... Creating MotionPlannerClient..."))
          .Times(1);
      EXPECT_CALL(mps_asset_mock_log,
                  Log(absl::LogSeverity::kInfo, _,
                      absl::StrCat("Installed MPS Asset Version: ",
                                   kTestMotionPlannerServiceAssetVersion)))
          .Times(1);
      mps_asset_mock_log.StartCapturingLogs();
      return move_robot->ComputePlan(params, world, planner, robot);
    }

    return move_robot->ComputePlan(params, world, planner, robot);
  }

  // Do not call twice in the same test with the same world_id. The world is
  // modified from the initial state created during SetUp(). If `update_limits`
  // is `true`, the World's robot limits are updated to those of ICON part's
  // limits, such that the preplanning results of `GetFootprint()` will
  // be used when calling `Execute()` later.
  absl::StatusOr<intrinsic_proto::skills::MoveRobotReturnValue>
  GetFootprintThenExecuteMoveRobotTest(
      intrinsic_proto::skills::MoveRobotParams const& params,
      eigenmath::VectorNd const& initial, bool update_limits,
      absl::string_view world_id = "get_footprint_then_execute_test_world") {
    MoveRobotTestParams test_params{
        .world_id = world_id,
        .report_elapsed_stop_time_seconds = 1.0,
        .report_is_settled = false,
        .test_move_to_signal = false,
    };
    INTR_ASSIGN_OR_RETURN(auto skill_and_equipment,
                          PrepareMoveRobotTest(initial, test_params));

    if (update_limits) {
      // Get the joint limits and cartesian limits from the ICON part config.
      auto part_config = GetIconPartConfig(world_id);
      EXPECT_TRUE(part_config->has_joint_limits_config());
      INTR_ASSIGN_OR_RETURN(
          const JointLimits icon_application_limits,
          FromProto(part_config->joint_limits_config().application_limits()));
      INTR_ASSIGN_OR_RETURN(
          const JointLimits icon_system_limits,
          FromProto(part_config->joint_limits_config().system_limits()));
      EXPECT_TRUE(part_config->has_cartesian_limits_config());
      INTR_ASSIGN_OR_RETURN(
          const CartesianLimits icon_cartesian_limits,
          icon::FromProto(part_config->cartesian_limits_config()
                              .default_cartesian_limits()));

      // Update the World's robot limits to those of ICON part's (above), such
      // that the preplanning results of `GetFootprint()` will be used
      // when calling `Execute()` later.
      auto world =
          world::ObjectWorldClient(world_id, world_service_->NewObjectStub());
      INTR_ASSIGN_OR_RETURN(
          auto robot, world.GetKinematicObject(WorldObjectName(kRobotLabel)));
      INTR_RETURN_IF_ERROR(world.UpdateJointLimits(
          robot, JointLimitsXd::Create(icon_application_limits),
          JointLimitsXd::Create(icon_system_limits)));
      INTR_RETURN_IF_ERROR(
          world.UpdateCartesianLimits(robot, icon_cartesian_limits));
    }

    INTR_ASSIGN_OR_RETURN(
        skill_and_equipment,
        PrepareMoveRobotTest(initial, /*test_params=*/MoveRobotTestParams()));

    auto execute_context = skill_test_factory_.MakeExecuteContext({
        .equipment_pack = skill_and_equipment.equipment,
        .world_id = std::string(kExecutionWorldId),
        .motion_planner_service = motion_planner_stub_,
        .object_world_service = world_service_->NewObjectStub(),
    });

    GetFootprintRequest footprint_request =
        skill_test_factory_.MakeGetFootprintRequest(params);
    auto footprint_context = skill_test_factory_.MakeGetFootprintContext({
        .equipment_pack = skill_and_equipment.equipment,
        .world_id = std::string(world_id),
        .motion_planner_service = motion_planner_stub_,
        .object_world_service = world_service_->NewObjectStub(),
        .context_id = std::string(execute_context->context_id()),
    });
    INTR_RETURN_IF_ERROR(
        skill_and_equipment.skill
            ->GetFootprint(footprint_request, *footprint_context)
            .status());

    ExecuteRequest execute_request =
        skill_test_factory_.MakeExecuteRequest(params);

    intrinsic_proto::skills::MoveRobotReturnValue return_value;
    INTR_RETURN_IF_ERROR(ExecuteSkill(*skill_and_equipment.skill,
                                      execute_request, *execute_context,
                                      &return_value));
    return return_value;
  }

  bool use_motion_planner_service_asset_;
  std::unique_ptr<MotionPlannerServiceProxy> motion_planner_service_proxy_;
  intrinsic_proto::skills::MoveRobotParams basic_params_;
  intrinsic_proto::skills::SkillManifest manifest_;
  SkillTestFactory skill_test_factory_;
  std::shared_ptr<FakeWorldService> world_service_;
  std::unique_ptr<intrinsic_proto::world::internal::WorldService::StubInterface>
      world_service_stub_;
  std::unique_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
      object_world_service_stub_;
  std::unique_ptr<MotionPlannerServiceInProcess>
      motion_planner_service_in_process_;
  std::shared_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>
      motion_planner_stub_;
  std::optional<world::ObjectWorldClient> world_;
  World original_world_;
};

TEST_P(MoveRobotFixtureTest, ExecuteWorksWithValidMotionSegment) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  ASSERT_OK(ExecuteMoveRobotTest(params, initial));
}

TEST_P(MoveRobotFixtureTest, ExecuteFailsWhenAssetIsNotSetCorrectly) {
  if (use_motion_planner_service_asset_) {
    GTEST_SKIP() << "Skipping test when MotionPlannerService Asset is defined.";
  }
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  // Intentionally setting the MotionPlannerService Asset incorrectly.
  std::unique_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::StubInterface>
      mps_stub_for_mps_proxy =
          motion_planner_service_in_process_->GetUniqueStub();
  ASSERT_OK_AND_ASSIGN(
      motion_planner_service_proxy_,
      MotionPlannerServiceProxy::Create(std::move(mps_stub_for_mps_proxy),
                                        kTestMotionPlannerServiceAssetVersion));
  intrinsic_proto::assets::v1::ResolvedDependency::Interface interface =
      skill_test_factory_.RunService(motion_planner_service_proxy_.get(),
                                     kMotionPlannerServiceName);
  params.mutable_motion_planner_service()->mutable_interfaces()->insert(
      {"Erroneous MotionPlannerService Interface", interface});

  EXPECT_THAT(
      ExecuteMoveRobotTest(params, initial,
                           /*test_params=*/MoveRobotTestParams(),
                           /*expect_success=*/false),
      StatusIs(absl::StatusCode::kNotFound,
               HasSubstr("Interface not found in resolved dependency")));
}

TEST_P(MoveRobotFixtureTest,
       ExecuteWorksOnBackwardCompatibilityWhenAssetInterfaceIsEmpty) {
  if (use_motion_planner_service_asset_) {
    GTEST_SKIP() << "Skipping test when MotionPlannerService Asset is defined.";
  }
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  // Intentionally setting the MotionPlannerService Asset interfaces to empty.
  params.mutable_motion_planner_service()->mutable_interfaces()->clear();

  ASSERT_OK(ExecuteMoveRobotTest(params, initial));
}

TEST_P(MoveRobotFixtureTest, ExecuteWorksWithInvalidMotionSegment) {
  intrinsic_proto::skills::MoveRobotParams params = basic_params_;
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  // TODO(b/404922724): Ensure all errors in move robot are extended status
  // errors.
  EXPECT_THAT(ExecuteMoveRobotTest(params, initial,
                                   /*test_params=*/MoveRobotTestParams(),
                                   /*expect_success=*/false),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("specify at least one motion segment")));
}

// Tests that the expected settling behaviour can be configured in the skill
// parameter and produces the expected results. Motion termination condition
// expects the robot to settle and testing helper reports the motion has
// settled.
TEST_P(MoveRobotFixtureTest, MotionShouldSettledAndRobotReportsItSettles) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  params.mutable_execution_parameters()->set_ignore_is_settled_condition(false);
  EXPECT_OK(ExecuteMoveRobotTest(
      params, initial,
      {.report_elapsed_stop_time_seconds = 0.0, .report_is_settled = true}));
}

// Tests that the expected settling behaviour can be configured in the skill
// parameter and produces the expected results. Motion termination condition
// does not wait for the robot to settle and testing helper reports the motion
// has not settled.
TEST_P(MoveRobotFixtureTest,
       MotionShouldTerminateWhenDoneNotSettledAndRobotReportsNotSettled) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  params.mutable_execution_parameters()->set_ignore_is_settled_condition(true);
  EXPECT_OK(ExecuteMoveRobotTest(
      params, initial,
      {.report_elapsed_stop_time_seconds = 0.0, .report_is_settled = false}));
}

// Tests that the expected settling behaviour can be configured in the skill
// parameter and produces the expected results. Motion termination condition
// does not wait for the robot to settle and testing helper reports the motion
// has settled. This test is less constraining than the previous one and should
// succeed if the previous one does, but is maintained here for completeness.
TEST_P(MoveRobotFixtureTest,
       MotionShouldTerminateWhenDoneNotSettledAndRobotReportsSettled) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  params.mutable_execution_parameters()->set_ignore_is_settled_condition(true);
  EXPECT_OK(ExecuteMoveRobotTest(
      params, initial,
      {.report_elapsed_stop_time_seconds = 0.0, .report_is_settled = true}));
}

// Tests that the expected settling behaviour can be configured in the skill
// parameter and produces the expected results. Motion termination condition
// expects the robot to settle, but testing helper does not report the motion
// has settled.
TEST_P(MoveRobotFixtureTest,
       MotionShouldTerminateWhenSettledButRobotReportsNotSettled) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  params.mutable_execution_parameters()->set_ignore_is_settled_condition(false);
  EXPECT_THAT(ExecuteMoveRobotTest(params, initial,
                                   {.report_elapsed_stop_time_seconds = 5.0,
                                    .report_is_settled = false}),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
}

TEST_P(MoveRobotFixtureTest, ExecutionCanBeCancelled) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();

  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  constexpr char kExecuteCancellationWorldId[] =
      "execute_cancellation_test_world";

  ASSERT_OK_AND_ASSIGN(
      auto skill_and_equipment,
      PrepareMoveRobotTest(initial, {.world_id = kExecuteCancellationWorldId,
                                     .report_elapsed_stop_time_seconds = 0.0,
                                     .report_is_settled = true,
                                     .test_move_to_signal = false,
                                     .report_tracking_done = false}));

  SkillCancellationManager canceller(absl::Seconds(10));

  Thread cancel_thread([&canceller]() {
    EXPECT_OK(canceller.WaitForReady());
    EXPECT_OK(canceller.Cancel());
  });

  ExecuteRequest execute_request =
      skill_test_factory_.MakeExecuteRequest(params);
  auto execute_context = skill_test_factory_.MakeExecuteContext({
      .canceller = &canceller,
      .equipment_pack = skill_and_equipment.equipment,
      .world_id = kExecuteCancellationWorldId,
      .motion_planner_service = motion_planner_stub_,
      .object_world_service = world_service_->NewObjectStub(),
  });

  EXPECT_THAT(
      skill_and_equipment.skill->Execute(execute_request, *execute_context),
      StatusIs(absl::StatusCode::kCancelled));

  cancel_thread.join();
}

TEST_P(MoveRobotFixtureTest, ExecuteStopsOnSignal) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->set_dio_block(kDioBlock);
  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->set_signal_index(kSignalIndex);
  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->set_value_of_signal(true);

  ASSERT_OK_AND_ASSIGN(
      auto return_value,
      ExecuteMoveRobotTest(params, initial,
                           {.report_elapsed_stop_time_seconds = 0.0,
                            .report_is_settled = true,
                            .test_move_to_signal = true}));

  EXPECT_EQ(return_value.stopped_on_signal(), true);
}

TEST_P(MoveRobotFixtureTest, ExecuteStopsOnSignalCondition) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->mutable_move_until_signal_condition()
      ->mutable_comparison()
      ->set_state_variable_name(icon::ADIODigitalInputStateVariablePath(
          icon::test_part_names::kADIOName, kDioBlock, kSignalIndex));
  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->mutable_move_until_signal_condition()
      ->mutable_comparison()
      ->set_operation(::intrinsic_proto::icon::v1::Comparison::EQUAL);
  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->mutable_move_until_signal_condition()
      ->mutable_comparison()
      ->set_bool_value(true);

  ASSERT_OK_AND_ASSIGN(
      auto return_value,
      ExecuteMoveRobotTest(params, initial,
                           {.report_elapsed_stop_time_seconds = 0.0,
                            .report_is_settled = true,
                            .test_move_to_signal = true}));

  EXPECT_EQ(return_value.stopped_on_signal(), true);
}

TEST_P(MoveRobotFixtureTest, ExecuteStopsNotOnSignal) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->set_dio_block(kDioBlock);
  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->set_signal_index(kSignalIndex);
  params.mutable_execution_parameters()
      ->mutable_move_until_signal_parameters()
      ->set_value_of_signal(false);

  ASSERT_OK_AND_ASSIGN(
      auto return_value,
      ExecuteMoveRobotTest(params, initial,
                           {.report_elapsed_stop_time_seconds = 0.0,
                            .report_is_settled = true,
                            .test_move_to_signal = true}));

  EXPECT_EQ(return_value.stopped_on_signal(), false);
}
TEST_P(MoveRobotFixtureTest, ExecuteLockMotionId) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  params.mutable_planning_parameters()
      ->mutable_lock_motion_configuration()
      ->mutable_save_motion_command();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  // The motion planning service used for testing skills does not support
  // non-volatile cache. So it will return an error.
  auto const result = ExecuteMoveRobotTest(params, initial);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::status::ExtendedStatus es_proto,
                       GetExtendedStatus(result.status()));
  EXPECT_THAT(es_proto.user_report().message(),
              HasSubstr("Cannot save motion since "
                        "plan_trajectory_nonvolatile_cache is null"));
}

TEST_P(MoveRobotFixtureTest, ExecuteSkipFuzzyCacheCheckWorks) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();

  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  params.mutable_planning_parameters()->set_skip_fuzzy_cache_check(true);
  EXPECT_OK(ExecuteMoveRobotTest(params, initial));
}

TEST_P(MoveRobotFixtureTest, PlannedMoveExceedsDeadlineForLowTimeOut) {
  eigenmath::VectorNd target_configuration(6);
  target_configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298,
      -1.554707;

  intrinsic_proto::skills::MoveRobotParams skill_param = basic_params_;
  auto* motion_segment = skill_param.add_motion_segments();
  VectorNdToRepeatedDouble(
      target_configuration,
      motion_segment->mutable_joint_position()->mutable_joints());

  constexpr double kTimeOut = 0.001;
  skill_param.mutable_planning_parameters()->set_max_planning_time_in_sec(
      kTimeOut);

  eigenmath::VectorNd initial(6);
  initial << 1.27778, -1.58980, 1.81571, -1.78568, -1.57508, -0.28733;

  auto const result = ExecuteMoveRobotTest(
      skill_param, initial, /*test_params=*/MoveRobotTestParams(),
      /*expect_success=*/false);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kDeadlineExceeded));
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::status::ExtendedStatus es_proto,
                       GetExtendedStatus(result.status()));
  EXPECT_THAT(
      es_proto.user_report().message(),
      HasSubstr(absl::StrCat(
          "Planner could not find a path in the given time of ", kTimeOut)));
}

TEST_P(MoveRobotFixtureTest, PlannedMoveFailsForWrongStepSize) {
  eigenmath::VectorNd target_configuration(6);
  target_configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298,
      -1.554707;

  intrinsic_proto::skills::MoveRobotParams skill_param = basic_params_;
  auto* motion_segment = skill_param.add_motion_segments();
  VectorNdToRepeatedDouble(
      target_configuration,
      motion_segment->mutable_joint_position()->mutable_joints());

  skill_param.mutable_planning_parameters()->set_path_planner_step_size(-0.3);

  eigenmath::VectorNd initial(6);
  initial << 1.27778, -1.58980, 1.81571, -1.78568, -1.57508, -0.28733;

  auto const result = ExecuteMoveRobotTest(
      skill_param, initial, /*test_params=*/MoveRobotTestParams(),
      /*expect_success=*/false);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::status::ExtendedStatus es_proto,
                       GetExtendedStatus(result.status()));
  EXPECT_THAT(es_proto.user_report().message(),
              HasSubstr("Planning option 'path planning step size' is "
                        "zero or negative."));
}

TEST_P(MoveRobotFixtureTest, PlannedMoveWorksForValidStepSize) {
  eigenmath::VectorNd target_configuration(6);
  target_configuration << -0.900000, 0.4, -0.1194, -0.705302, 0.673298,
      -1.554707;

  intrinsic_proto::skills::MoveRobotParams params = basic_params_;
  auto* motion_segment = params.add_motion_segments();
  VectorNdToRepeatedDouble(
      target_configuration,
      motion_segment->mutable_joint_position()->mutable_joints());

  params.mutable_planning_parameters()->set_path_planner_step_size(0.15);

  eigenmath::VectorNd initial(6);
  initial << 1.27778, -1.58980, 1.81571, -1.78568, -1.57508, -0.28733;
  EXPECT_OK(ExecuteMoveRobotTest(params, initial));
}

TEST_P(MoveRobotFixtureTest, ComputePlanWorksWithValidMotionSegment) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  ASSERT_OK_AND_ASSIGN(auto internal_data_proto,
                       ComputePlanMoveRobotTest(params, initial));
  EXPECT_TRUE(internal_data_proto.has_execution_plan());
}

TEST_P(MoveRobotFixtureTest, PreviewWorksWithValidMotionSegment) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  ASSERT_OK(PreviewMoveRobotTest(params, initial));
}

TEST_P(MoveRobotFixtureTest, ComputePlanWorksWithNormalizedQuaternionRounded) {
  intrinsic_proto::skills::MoveRobotParams params = basic_params_;
  auto* motion_segment = params.add_motion_segments();
  // The following motion target contains a quaternion that is approximately
  // normalized (up to a rounding of 4 decimals). This should result in an
  // successful motion.
  *motion_segment = ParseTextProtoOrDie(R"pb(
    cartesian_pose {
      moving_frame {
        by_name { frame { object_name: "agilus-04" frame_name: "flange" } }
      }
      target_frame { by_name { object { object_name: "root" } } }
      target_frame_offset {
        position { x: 0.175127 y: 0.533182 z: 0.822006 }
        orientation {
          x: -0.7082  # -0.70796712066661194
          y: 0.00294  # 0.0029309704995574928
          z: 0.7062   # 0.706235120371408
          w: 0.0024   # 0.0024331504147085481
        }
      }
    }
  )pb");
  intrinsic::eigenmath::Quaterniond quaternion =
      intrinsic_proto::FromProto(params.motion_segments(0)
                                     .cartesian_pose()
                                     .target_frame_offset()
                                     .orientation());
  EXPECT_GT(quaternion.norm(), 1 + 1e-4);
  EXPECT_LT(quaternion.norm(), 1 + 1e-2);
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  EXPECT_OK(ComputePlanMoveRobotTest(params, initial));
}

TEST_P(MoveRobotFixtureTest, PreviewWorksWithNormalizedQuaternionRounded) {
  intrinsic_proto::skills::MoveRobotParams params = basic_params_;
  auto* motion_segment = params.add_motion_segments();
  // The following motion target contains a quaternion that is approximately
  // normalized (up to a rounding of 4 decimals). This should result in an
  // successful motion.
  *motion_segment = ParseTextProtoOrDie(R"pb(
    cartesian_pose {
      moving_frame {
        by_name { frame { object_name: "agilus-04" frame_name: "flange" } }
      }
      target_frame { by_name { object { object_name: "root" } } }
      target_frame_offset {
        position { x: 0.175127 y: 0.533182 z: 0.822006 }
        orientation {
          x: -0.7082  # -0.70796712066661194
          y: 0.00294  # 0.0029309704995574928
          z: 0.7062   # 0.706235120371408
          w: 0.0024   # 0.0024331504147085481
        }
      }
    }
  )pb");
  intrinsic::eigenmath::Quaterniond quaternion =
      intrinsic_proto::FromProto(params.motion_segments(0)
                                     .cartesian_pose()
                                     .target_frame_offset()
                                     .orientation());
  EXPECT_GT(quaternion.norm(), 1 + 1e-4);
  EXPECT_LT(quaternion.norm(), 1 + 1e-2);
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  EXPECT_OK(PreviewMoveRobotTest(params, initial));
}

TEST_P(MoveRobotFixtureTest, ComputePlanWorksWithLockMotion) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  params.mutable_planning_parameters()
      ->mutable_lock_motion_configuration()
      ->mutable_save_motion_command();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  // The motion planning service used for testing skills does not support
  // non-volatile cache. So it will return an error.
  auto const result = ComputePlanMoveRobotTest(params, initial);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::status::ExtendedStatus es_proto,
                       GetExtendedStatus(result.status()));
  EXPECT_THAT(es_proto.user_report().message(),
              HasSubstr("Cannot save motion since "
                        "plan_trajectory_nonvolatile_cache is null"));
}

TEST_P(MoveRobotFixtureTest, ComputePlanFailsForInvalidQuaternion) {
  intrinsic_proto::skills::MoveRobotParams params = basic_params_;
  auto* motion_segment = params.add_motion_segments();
  // The following motion target contains a quaternion that is not normalized
  // and this should result in a failure because the quaternion is far from
  // being normalized.
  *motion_segment = ParseTextProtoOrDie(R"pb(
    cartesian_pose {
      moving_frame {
        by_name { frame { object_name: "agilus-04" frame_name: "flange" } }
      }
      target_frame { by_name { object { object_name: "root" } } }
      target_frame_offset {
        position { x: 0.175127 y: 0.533182 z: 0.822006 }
        orientation { x: -0.7080 y: 0.0029 z: 1.7062 w: 0.0024 }
      }
    }
  )pb");
  intrinsic::eigenmath::Quaterniond quaternion =
      intrinsic_proto::FromProto(params.motion_segments(0)
                                     .cartesian_pose()
                                     .target_frame_offset()
                                     .orientation());
  EXPECT_GT(quaternion.norm(), 1 + 1e-2);
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  auto const result = ComputePlanMoveRobotTest(params, initial);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::status::ExtendedStatus es_proto,
                       GetExtendedStatus(result.status()));
  EXPECT_THAT(es_proto.user_report().message(),
              HasSubstr("non-unit quaternion with norm"));
}

TEST_P(MoveRobotFixtureTest, PreviewFailsForInvalidQuaternion) {
  intrinsic_proto::skills::MoveRobotParams params = basic_params_;
  auto* motion_segment = params.add_motion_segments();
  // The following motion target contains a quaternion that is not normalized
  // and this should result in a failure because the quaternion is far from
  // being normalized.
  *motion_segment = ParseTextProtoOrDie(R"pb(
    cartesian_pose {
      moving_frame {
        by_name { frame { object_name: "agilus-04" frame_name: "flange" } }
      }
      target_frame { by_name { object { object_name: "root" } } }
      target_frame_offset {
        position { x: 0.175127 y: 0.533182 z: 0.822006 }
        orientation { x: -0.7080 y: 0.0029 z: 1.7062 w: 0.0024 }
      }
    }
  )pb");
  intrinsic::eigenmath::Quaterniond quaternion =
      intrinsic_proto::FromProto(params.motion_segments(0)
                                     .cartesian_pose()
                                     .target_frame_offset()
                                     .orientation());
  EXPECT_GT(quaternion.norm(), 1 + 1e-2);
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  auto const result = PreviewMoveRobotTest(params, initial);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kInvalidArgument));
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::status::ExtendedStatus es_proto,
                       GetExtendedStatus(result));
  EXPECT_THAT(es_proto.user_report().message(),
              HasSubstr("non-unit quaternion with norm"));
}

TEST_P(MoveRobotFixtureTest, FootprintChoosesArmPartFromParams) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();

  EquipmentPack equipment;
  ASSERT_OK(equipment.Add(
      MoveRobot::kEquipmentSlot,
      icon::Icon2EquipmentHandleBuilder(kRobotLabel, kRobotLabel)
          .WithPositionControlledPart(icon::test_part_names::kArmName)
          .Build()));
  absl::string_view chosen_arm_object = "agilus-03";

  *params.mutable_arm_part()->mutable_by_name()->mutable_object_name() =
      chosen_arm_object;

  ASSERT_OK_AND_ASSIGN(auto channel_fake,
                       icon::RtclControllerChannelFake::Builder().Build(
                           icon::DefaultPartConfigs()));
  ASSERT_OK(icon::Client(channel_fake).Enable());

  auto skill = std::make_unique<MoveRobot>(
      std::make_unique<icon::FakeChannelPassThroughFactory>(
          std::move(channel_fake)));

  GetFootprintRequest request =
      skill_test_factory_.MakeGetFootprintRequest(params);
  auto context = skill_test_factory_.MakeGetFootprintContext({
      .equipment_pack = equipment,
      .world_id = std::string(kOriginalWorldId),
      .motion_planner_service = motion_planner_stub_,
      .object_world_service = world_service_->NewObjectStub(),
  });

  ASSERT_OK_AND_ASSIGN(auto footprint, skill->GetFootprint(request, *context));
  EXPECT_FALSE(footprint.lock_the_universe());
  EXPECT_EQ(footprint.object_reservation_size(), 1);
  EXPECT_EQ(footprint.object_reservation(0).object().object_name(),
            chosen_arm_object);
  ASSERT_OK_AND_ASSIGN(
      auto cached_plan,
      GetSkillData().Get<intrinsic_proto::skills::MoveRobotInternalData>(
          context->context_id(), "plan"));
  EXPECT_TRUE(cached_plan.has_value());
}

TEST_P(MoveRobotFixtureTest,
       FootprintUsesArmObjectFromEquipmentIfParamIsEmpty) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();

  EquipmentPack equipment;
  ASSERT_OK(equipment.Add(
      MoveRobot::kEquipmentSlot,
      icon::Icon2EquipmentHandleBuilder(kRobotLabel, kRobotLabel)
          .WithPositionControlledPart(icon::test_part_names::kArmName)
          .Build()));

  ASSERT_OK_AND_ASSIGN(auto channel_fake,
                       icon::RtclControllerChannelFake::Builder().Build(
                           icon::DefaultPartConfigs()));
  ASSERT_OK(icon::Client(channel_fake).Enable());

  auto skill = std::make_unique<MoveRobot>(
      std::make_unique<icon::FakeChannelPassThroughFactory>(
          std::move(channel_fake)));

  GetFootprintRequest request =
      skill_test_factory_.MakeGetFootprintRequest(params);
  auto context2 = skill_test_factory_.MakeGetFootprintContext({
      .equipment_pack = equipment,
      .world_id = std::string(kOriginalWorldId),
      .motion_planner_service = motion_planner_stub_,
      .object_world_service = world_service_->NewObjectStub(),
  });

  ASSERT_OK_AND_ASSIGN(auto footprint, skill->GetFootprint(request, *context2));
  EXPECT_FALSE(footprint.lock_the_universe());
  EXPECT_EQ(footprint.object_reservation_size(), 1);
  EXPECT_EQ(footprint.object_reservation(0).object().object_name(),
            kRobotLabel);
  ASSERT_OK_AND_ASSIGN(
      auto cached_plan,
      GetSkillData().Get<intrinsic_proto::skills::MoveRobotInternalData>(
          context2->context_id(), "plan"));
  EXPECT_TRUE(cached_plan.has_value());
}

TEST_P(MoveRobotFixtureTest, FootprintCachesPlanInSkillData) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  constexpr absl::string_view kContextId = "test_shared_context_id";

  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  MoveRobotTestParams test_params;
  test_params.world_id = "footprint_precompute_world";

  ASSERT_OK_AND_ASSIGN(auto skill_and_equipment,
                       PrepareMoveRobotTest(initial, test_params));

  GetFootprintRequest footprint_request =
      skill_test_factory_.MakeGetFootprintRequest(params);
  auto footprint_context = skill_test_factory_.MakeGetFootprintContext({
      .equipment_pack = skill_and_equipment.equipment,
      .world_id = std::string(test_params.world_id),
      .motion_planner_service = motion_planner_stub_,
      .object_world_service = world_service_->NewObjectStub(),
      .context_id = std::string(kContextId),
  });

  ASSERT_OK(skill_and_equipment.skill->GetFootprint(footprint_request,
                                                    *footprint_context));

  // Verify the plan is in SkillData before Execute.
  ASSERT_OK_AND_ASSIGN(
      auto cached_plan,
      GetSkillData().Get<intrinsic_proto::skills::MoveRobotInternalData>(
          kContextId, "plan"));
  ASSERT_TRUE(cached_plan.has_value());
  EXPECT_TRUE(cached_plan->execution_plan().has_planned_trajectory());

  ExecuteRequest execute_request =
      skill_test_factory_.MakeExecuteRequest(params);
  auto execute_context = skill_test_factory_.MakeExecuteContext({
      .equipment_pack = skill_and_equipment.equipment,
      .world_id = std::string(test_params.world_id),
      .motion_planner_service = motion_planner_stub_,
      .object_world_service = world_service_->NewObjectStub(),
      .context_id = std::string(kContextId),
  });

  intrinsic_proto::skills::MoveRobotReturnValue return_value;
  EXPECT_OK(ExecuteSkill(*skill_and_equipment.skill, execute_request,
                         *execute_context, &return_value));
}

TEST_P(MoveRobotFixtureTest, ExecuteClearsStaleLockMotionIdOnReplan) {
  const intrinsic_proto::skills::MoveRobotParams params =
      CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  ASSERT_OK_AND_ASSIGN(
      intrinsic_proto::skills::MoveRobotInternalData internal_data_proto,
      ComputePlanMoveRobotTest(params, initial));

  // Inject a synthetic `lock_motion_id` and invalidate the preplanned solution
  // by modifying the start position so `ValidateSolution()` triggers
  // replanning.
  constexpr absl::string_view kStaleLockMotionId = "stale_lock_motion_id";
  constexpr double kMismatchedJointPositionRad = 99.0;
  internal_data_proto.set_lock_motion_id(kStaleLockMotionId);
  intrinsic_proto::icon::JointTrajectoryPVA* trajectory =
      internal_data_proto.mutable_execution_plan()
          ->mutable_planned_trajectory();
  ASSERT_GT(trajectory->state_size(), 0);
  ASSERT_GT(trajectory->state(0).position_size(), 0);
  trajectory->mutable_state(0)->set_position(0, kMismatchedJointPositionRad);

  ASSERT_OK_AND_ASSIGN(
      const intrinsic_proto::skills::MoveRobotReturnValue return_value,
      ExecuteMoveRobotTest(params, initial));
  EXPECT_FALSE(return_value.has_lock_motion_id());
}
TEST_P(MoveRobotFixtureTest, ComputePlanWorksWithSmallJointLimitViolation) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  ASSERT_TRUE(world_.has_value());
  ASSERT_OK_AND_ASSIGN(
      auto robot, world_->GetKinematicObject(WorldObjectName(kRobotLabel)));
  initial(3) = robot.JointApplicationLimits().min_position(3) - 1e-4;
  initial(5) = robot.JointApplicationLimits().max_position(5) + 1e-4;

  ASSERT_OK_AND_ASSIGN(auto internal_data_proto,
                       ComputePlanMoveRobotTest(params, initial));
  EXPECT_TRUE(internal_data_proto.has_execution_plan());
}

TEST_P(MoveRobotFixtureTest, ComputePlanFailsWithLargeJointLimitViolation) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  ASSERT_TRUE(world_.has_value());
  ASSERT_OK_AND_ASSIGN(
      auto robot, world_->GetKinematicObject(WorldObjectName(kRobotLabel)));
  initial = robot.JointApplicationLimits().max_position.array() + 1e-2;
  auto result = ComputePlanMoveRobotTest(params, initial,
                                         /*world_id=*/"compute_plan_test_world",
                                         /*expect_success=*/false);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kFailedPrecondition));

  // Check that the error message is correct.
  ASSERT_OK_AND_ASSIGN(intrinsic_proto::status::ExtendedStatus es_proto,
                       GetExtendedStatus(result.status()));
  EXPECT_THAT(es_proto.user_report().message(),
              HasSubstr("Try to either increase the application limits or jog "
                        "the robot to a valid configuration."));
}

TEST_P(MoveRobotFixtureTest, ComputePlanFailsForInvalidCartesianLimits) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;
  ASSERT_TRUE(world_.has_value());
  ASSERT_OK_AND_ASSIGN(
      auto robot, world_->GetKinematicObject(WorldObjectName(kRobotLabel)));
  initial = robot.JointApplicationLimits().max_position.array() + 1e-2;
  CartesianLimits invalid_cart_limits =
      CreateSimpleCartesianLimits(0, 0, 0, 0, 0, 0, 0);
  ASSERT_OK(SetCartesianLimitsInWorld(original_world_, robot.Name().value(),
                                      invalid_cart_limits));
  auto result = ComputePlanMoveRobotTest(params, initial,
                                         /*world_id=*/"compute_plan_test_world",
                                         /*expect_success=*/false);
  EXPECT_THAT(result, StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_THAT(
      result.status().message(),
      HasSubstr(
          "No valid Cartesian limits defined for robot. They either equal or "
          "less than zero. To fix this error, please set the correct Cartesian "
          "limits to the robot defined within the world service."));
}

TEST_P(MoveRobotFixtureTest, ExecuteUpdateJointLimits) {
  intrinsic_proto::skills::MoveRobotParams params = CreateJointTargetParams();
  eigenmath::VectorNd initial(6);
  initial << -0.10, 0.48, -1.36, -0.77, 0.83, -1.0;

  // The motion planning service used for testing skills does not support
  // non-volatile cache. So it will return an error.
  ASSERT_OK(ExecuteMoveRobotTest(params, initial));

  // Get the joint limits from the icon part config.
  auto part_config = GetIconPartConfig(kExecutionWorldId);
  ASSERT_TRUE(part_config->has_joint_limits_config());
  ASSERT_OK_AND_ASSIGN(
      const JointLimits icon_application_limits,
      FromProto(part_config->joint_limits_config().application_limits()));

  // Start condition: Joint limits in original world are different from the
  // icon application limits.
  ASSERT_OK_AND_ASSIGN(auto original_robot, world_->GetKinematicObject(
                                                WorldObjectName(kRobotLabel)));
  EXPECT_THAT(
      JointLimitsXd::Create(icon_application_limits),
      Not(JointLimitXdIsApprox(original_robot.JointApplicationLimits())));

  // Final condition: Joint limits in updated world are the same as the icon
  // application limits.
  auto updated_world = world::ObjectWorldClient(
      kExecutionWorldId, world_service_->NewObjectStub());
  ASSERT_OK_AND_ASSIGN(auto updated_robot, updated_world.GetKinematicObject(
                                               WorldObjectName(kRobotLabel)));
  EXPECT_THAT(JointLimitsXd::Create(icon_application_limits),
              JointLimitXdIsApprox(updated_robot.JointApplicationLimits()));
}

INSTANTIATE_TEST_SUITE_P(
    MoveRobotFixtureTests, MoveRobotFixtureTest,
    ::testing::ValuesIn<MoveRobotTestParameter>({
        {.test_name = "WithMotionPlannerServiceAsset",
         .use_motion_planner_service_asset = true},
        {.test_name = "BackwardCompatibilityWithoutMotionPlannerServiceAsset",
         .use_motion_planner_service_asset = false},
    }),
    [](const ::testing::TestParamInfo<MoveRobotFixtureTest::ParamType>& info) {
      return info.param.test_name;
    });
}  // namespace
}  // namespace skills
}  // namespace intrinsic
