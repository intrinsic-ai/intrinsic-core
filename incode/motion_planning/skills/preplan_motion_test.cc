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

#include "incode/motion_planning/skills/preplan_motion.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/scoped_mock_log.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "incode/motion_planning/skills/preplan_motion.pb.h"
#include "internal/testing.h"
#include "intrinsic/assets/proto/id.pb.h"
#include "intrinsic/assets/proto/v1/resolved_dependency.pb.h"
#include "intrinsic/eigenmath/matchers.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/cc_client/testing/channel_fake.h"
#include "intrinsic/icon/equipment/testing/equipment_test_utils.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/math/pose3.h"
#include "intrinsic/math/proto/quaternion.pb.h"
#include "intrinsic/motion_planning/service/motion_planner_service.h"
#include "intrinsic/motion_planning/service/motion_planner_service_asset_utils.h"
#include "intrinsic/motion_planning/service/motion_planner_service_in_process.h"
#include "intrinsic/motion_planning/service/motion_planner_service_proxy.h"
#include "intrinsic/motion_planning/skills/move_robot.pb.h"
#include "intrinsic/skills/apps/attach_object_to_robot.pb.h"
#include "intrinsic/skills/apps/detach_object.pb.h"
#include "intrinsic/skills/apps/update_world.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skill_manifest.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/skills/testing/skill_test_utils.h"
#include "intrinsic/util/path_resolver/path_resolver.h"
#include "intrinsic/util/proto/parse_text_proto.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/world/gzfile/gzfile.h"
#include "intrinsic/world/objects/object_world_client.h"
#include "intrinsic/world/objects/object_world_ids.h"
#include "intrinsic/world/objects/test/object_world_test_utils.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_updates.pb.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"

namespace intrinsic {
namespace skills {
namespace {

using ::absl_testing::StatusIs;
using ::intrinsic::ParseTextProtoOrDie;
using ::intrinsic::eigenmath::testing::IsApprox;
using ::testing::_;
using ::testing::IsEmpty;

constexpr absl::string_view kRobotLabel = "agilus-04";
constexpr char kFrameName[] = "tool_target";
constexpr char kObjectName[] = "object";
char const kWorldId[] = "my_world";

constexpr char kManifestPath[] =
    "incode/motion_planning/skills/preplan_motion_manifest.pbbin";

constexpr std::string_view kTestMotionPlannerServiceAssetVersion =
    "0.20260427.7-RC17";

intrinsic_proto::world::CreateFrameRequest CreateFrameRequest() {
  intrinsic_proto::world::CreateFrameRequest request;
  request.set_new_frame_name(kFrameName);
  request.mutable_parent_object_with_filter()
      ->mutable_reference()
      ->mutable_by_name()
      ->set_object_name(RootObjectName().value());
  request.mutable_parent_t_new_frame()->mutable_position()->set_x(0.285883);
  request.mutable_parent_t_new_frame()->mutable_position()->set_y(0.622421);
  request.mutable_parent_t_new_frame()->mutable_position()->set_z(0.862182);
  request.mutable_parent_t_new_frame()->mutable_orientation()->set_x(
      0.00243315041470855);
  request.mutable_parent_t_new_frame()->mutable_orientation()->set_y(
      -0.70796712066661194);
  request.mutable_parent_t_new_frame()->mutable_orientation()->set_z(
      0.00293097049955749);
  request.mutable_parent_t_new_frame()->mutable_orientation()->set_w(
      0.70623512037140801);
  return request;
}

intrinsic_proto::skills::MotionSegment CreateMotionSegment() {
  intrinsic_proto::skills::MotionSegment segment;
  *segment.mutable_joint_position() = ParseTextProtoOrDie(R"pb(
    joints: [ -0.2, 0.4, -1.3, -0.7, 0.7, -1.0 ]
  )pb");
  return segment;
}

intrinsic_proto::skills::AttachObjectToRobotParams
CreateAttachObjectToRobotParams() {
  intrinsic_proto::skills::AttachObjectToRobotParams params;
  params.mutable_gripper_entity()->mutable_by_name()->set_object_name(
      kRobotLabel);
  params.mutable_object_entity()->mutable_by_name()->set_object_name(
      kObjectName);
  return params;
}

intrinsic_proto::skills::DetachObjectParams CreateDetachObjectParams() {
  intrinsic_proto::skills::DetachObjectParams params;
  params.mutable_gripper_entity()->mutable_by_name()->set_object_name(
      kRobotLabel);
  params.mutable_object_entity()->mutable_by_name()->set_object_name(
      kObjectName);
  return params;
}

EquipmentPack GetEquipmentPack() {
  EquipmentPack equipment;
  CHECK_OK(equipment.Add(
      PreplanMotionSkill::kEquipmentSlot,
      icon::Icon2EquipmentHandleBuilder(kRobotLabel, kRobotLabel)
          .WithPositionControlledPart(icon::test_part_names::kArmName)
          .WithAdioPart(icon::test_part_names::kADIOName)
          .Build()));
  return equipment;
}

TEST(PreplanMotionTest, CreateSkill) {
  std::unique_ptr<SkillInterface> preplan_motion =
      PreplanMotionSkill::CreateSkill();
}

struct PreplanMotionTestParameter {
  std::string test_name;
  bool use_motion_planner_service_asset;
};

class PreplanMotionFixtureTest
    : public ::testing::TestWithParam<PreplanMotionTestParameter> {
 protected:
  PreplanMotionFixtureTest() : manifest_(GetManifestOrDie(kManifestPath)) {}

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
    basic_params_ = intrinsic_proto::skills::PreplanMotionParams();
    if (use_motion_planner_service_asset_) {
      LOG(INFO) << "Initiating ResolvedDependency on MotionPlannerService for "
                   "PreplanMotionParams ...";
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

    std::string world_gzf_filename = PathResolver::ResolveRunfilesPathForTest(
        "intrinsic/world/test_data/4_robots_cube_world.gzf");
    ASSERT_OK_AND_ASSIGN(auto gz_file, GZFile::Open(world_gzf_filename));
    ASSERT_OK_AND_ASSIGN(World world, World::FromFile(*gz_file));
    ASSERT_OK(object_world::CreateEntitiesForObject(
        &world, {.name = WorldObjectName(kObjectName)}));
    ASSERT_THAT(world_service_->AddWorld(kWorldId, std::move(world)),
                ::absl_testing::IsOk());
    world_ =
        world::ObjectWorldClient(kWorldId, world_service_->NewObjectStub());
  }

  intrinsic_proto::skills::PreplanMotionParams CreatePreplanMotionParams() {
    intrinsic_proto::skills::PreplanMotionParams params = basic_params_;
    *params.add_skills()
         ->mutable_update_world()
         ->mutable_update()
         ->mutable_create_frame() = CreateFrameRequest();

    auto* motion_segment =
        params.add_skills()->mutable_move_robot()->add_motion_segments();
    *motion_segment = CreateMotionSegment();

    *params.add_skills()->mutable_attach_object_to_robot() =
        CreateAttachObjectToRobotParams();

    *params.add_skills()->mutable_detach_object() = CreateDetachObjectParams();

    return params;
  }

  absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
  ExecutePreplanMotionTest(
      intrinsic_proto::skills::PreplanMotionParams const& params,
      eigenmath::VectorNd const& initial) {
    PreplanMotionSkill skill;
    ExecuteRequest request = skill_test_factory_.MakeExecuteRequest(params);
    auto context = skill_test_factory_.MakeExecuteContext({
        .equipment_pack = GetEquipmentPack(),
        .world_id = std::string(kWorldId),
        .motion_planner_service = motion_planner_stub_,
        .object_world_service = world_service_->NewObjectStub(),
    });

    if (use_motion_planner_service_asset_) {
      bool has_move_robot = false;
      for (const auto& skill_call : params.skills()) {
        if (skill_call.has_move_robot()) {
          has_move_robot = true;
          break;
        }
      }
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
          .Times(has_move_robot ? 1 : 0);
      mps_asset_mock_log.StartCapturingLogs();
      return skill.Execute(request, *context);
    }

    return skill.Execute(request, *context);
  }

  absl::StatusOr<std::unique_ptr<google::protobuf::Message>>
  PreviewPreplanMotionTest(
      intrinsic_proto::skills::PreplanMotionParams const& params,
      eigenmath::VectorNd const& initial) {
    PreplanMotionSkill skill;
    PreviewRequest request = skill_test_factory_.MakePreviewRequest(params);
    auto context = skill_test_factory_.MakePreviewContext({
        .equipment_pack = GetEquipmentPack(),
        .world_id = std::string(kWorldId),
        .motion_planner_service = motion_planner_stub_,
        .object_world_service = world_service_->NewObjectStub(),
    });

    return skill.Preview(request, *context);
  }

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

  bool use_motion_planner_service_asset_;
  intrinsic_proto::skills::PreplanMotionParams basic_params_;
  std::unique_ptr<MotionPlannerServiceProxy> motion_planner_service_proxy_;
};

TEST_P(PreplanMotionFixtureTest, ExecuteWorksWithValidMotionSegment) {
  intrinsic_proto::skills::PreplanMotionParams params =
      CreatePreplanMotionParams();
  eigenmath::VectorNd initial(6);
  initial << 0, -2, 2, 0, 0, 0;

  // Confirm that the frame does not exist in the world at first.
  EXPECT_THAT(world_->GetFrame(RootObjectName(), FrameName(kFrameName)),
              StatusIs(absl::StatusCode::kNotFound));
  // Check the robot and object positions before the execution.
  ASSERT_OK_AND_ASSIGN(
      auto robot, world_->GetKinematicObject(WorldObjectName(kRobotLabel)));
  EXPECT_THAT(robot.JointPositions(),
              ::testing::Pointwise(::testing::DoubleNear(1e-6), initial));
  ASSERT_OK_AND_ASSIGN(auto object,
                       world_->GetObject(WorldObjectName(kObjectName)));
  EXPECT_THAT(object.ParentTThis(), IsApprox(Pose3d::Identity()));

  ASSERT_OK_AND_ASSIGN(auto execution_result,
                       ExecutePreplanMotionTest(params, initial));
  EXPECT_EQ(execution_result, nullptr);

  // Confirm that the original world was not modified by preplanning.
  EXPECT_THAT(world_->GetFrame(RootObjectName(), FrameName(kFrameName)),
              StatusIs(absl::StatusCode::kNotFound));
  ASSERT_OK_AND_ASSIGN(
      robot, world_->GetKinematicObject(WorldObjectName(kRobotLabel)));
  EXPECT_THAT(robot.JointPositions(),
              ::testing::Pointwise(::testing::DoubleNear(1e-6), initial));
  ASSERT_OK_AND_ASSIGN(object, world_->GetObject(WorldObjectName(kObjectName)));
  EXPECT_THAT(object.ParentTThis(), IsApprox(Pose3d::Identity()));
  EXPECT_EQ(object.ParentName(), RootObjectName());
}

TEST_P(PreplanMotionFixtureTest, PreviewWorksWithValidMotionSegment) {
  intrinsic_proto::skills::PreplanMotionParams params =
      CreatePreplanMotionParams();
  eigenmath::VectorNd initial(6);
  initial << 0, -2, 2, 0, 0, 0;

  ASSERT_OK_AND_ASSIGN(auto preview_result,
                       PreviewPreplanMotionTest(params, initial));
  EXPECT_EQ(preview_result, nullptr);
}

TEST_P(PreplanMotionFixtureTest, GetFootprintDoesNotLockTheUniverse) {
  PreplanMotionSkill skill;
  GetFootprintRequest request =
      skill_test_factory_.MakeGetFootprintRequest(CreatePreplanMotionParams());
  auto context = skill_test_factory_.MakeGetFootprintContext({
      .equipment_pack = GetEquipmentPack(),
      .world_id = std::string(kWorldId),
      .object_world_service = world_service_->NewObjectStub(),
  });

  ASSERT_OK_AND_ASSIGN(auto default_footprint_result,
                       skill.GetFootprint(request, *context));
  EXPECT_FALSE(default_footprint_result.lock_the_universe());
  EXPECT_THAT(default_footprint_result.object_reservation(), IsEmpty());
  EXPECT_THAT(default_footprint_result.volume(), IsEmpty());
}

INSTANTIATE_TEST_SUITE_P(
    PreplanMotionFixtureTests, PreplanMotionFixtureTest,
    ::testing::ValuesIn<PreplanMotionTestParameter>({
        {.test_name = "WithMotionPlannerServiceAsset",
         .use_motion_planner_service_asset = true},
        {.test_name = "BackwardCompatibilityWithoutMotionPlannerServiceAsset",
         .use_motion_planner_service_asset = false},
    }),
    [](const ::testing::TestParamInfo<PreplanMotionFixtureTest::ParamType>&
           info) { return info.param.test_name; });

}  // namespace
}  // namespace skills
}  // namespace intrinsic
