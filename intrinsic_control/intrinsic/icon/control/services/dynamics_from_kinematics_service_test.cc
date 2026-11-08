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

#include "intrinsic/icon/control/services/dynamics_from_kinematics_service.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/string_view.h"
#include "internal/testing.h"
#include "intrinsic/eigenmath/types.h"
#include "intrinsic/icon/control/services/dynamics_service.h"
#include "intrinsic/icon/control/services/kinematics_from_world_service.h"
#include "intrinsic/icon/control/services/service.h"
#include "intrinsic/icon/control/services/world_from_scene_object_gzf_service.h"
#include "intrinsic/icon/server/config/services_config.pb.h"
#include "intrinsic/icon/utils/realtime_status_matchers.h"
#include "intrinsic/util/proto/parse_text_proto.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::Each;
using ::testing::Eq;
using ::testing::HasSubstr;

constexpr absl::string_view kSceneObjectGzfPath =
    "intrinsic_control/intrinsic/models/robot_definitions/ur/"
    "ur3e_scene_object.gzf";

TEST(DynamicsFromKinematicsServiceTest, LoadsAllPartsByDefault) {
  ASSERT_OK_AND_ASSIGN(
      auto world_from_gzf_service,
      WorldFromSceneObjectGzfService::Create(kSceneObjectGzfPath));
  ASSERT_OK_AND_ASSIGN(
      auto kinematics_from_world_service,
      KinematicsFromWorldService::Create({}, world_from_gzf_service.get()));
  // Empty proto config should succeed now by loading all parts from the
  // KinematicsService instance.
  ASSERT_OK_AND_ASSIGN(auto service,
                       DynamicsFromKinematicsService::Create(
                           {}, kinematics_from_world_service.get(), {}));
  // Verify that the loaded part "ur3e_scene_object" is available
  ASSERT_OK_AND_ASSIGN(
      auto solver, service->CreateDynamicsSolverForPart("ur3e_scene_object"));
}

TEST(DynamicsFromKinematicsServiceTest, UsesPartName) {
  ASSERT_OK_AND_ASSIGN(
      auto world_from_gzf_service,
      WorldFromSceneObjectGzfService::Create(kSceneObjectGzfPath));
  ASSERT_OK_AND_ASSIGN(
      auto kinematics_from_world_service,
      KinematicsFromWorldService::Create({}, world_from_gzf_service.get()));
  ASSERT_OK_AND_ASSIGN(
      auto service, DynamicsFromKinematicsService::Create(
                        {}, kinematics_from_world_service.get(),
                        ParseTextProtoOrDie("part_name: 'ur3e_scene_object'")));
  EXPECT_THAT(
      DynamicsFromKinematicsService::Create(
          {.resource_id = "WRONG"}, kinematics_from_world_service.get(),
          {ParseTextProtoOrDie("part_name: 'WRONG'")}),
      StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("WRONG")));
}

TEST(DynamicsFromKinematicsServiceTest, WorksFromProtoWithPartName) {
  Service::InitParameters init_parameters = {.server_name = "server_name",
                                             .control_frequency_hz = 50};
  ASSERT_OK_AND_ASSIGN(
      auto world_service_from_fragment,
      WorldFromSceneObjectGzfService::Create(kSceneObjectGzfPath));
  ASSERT_OK_AND_ASSIGN(auto kinematics_from_world_service,
                       KinematicsFromWorldService::Create(
                           init_parameters, world_service_from_fragment.get()));
  intrinsic_proto::icon::DynamicsServiceConfig dynamics_service_config;
  dynamics_service_config.set_part_name("ur3e_scene_object");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<DynamicsService> dynamics_service,
                       DynamicsFromKinematicsService::Create(
                           init_parameters, kinematics_from_world_service.get(),
                           dynamics_service_config));
  ASSERT_OK_AND_ASSIGN(
      auto solver,
      dynamics_service->CreateDynamicsSolverForPart("ur3e_scene_object"));
}

TEST(DynamicsFromKinematicsServiceTest, WorksFromProtoWithResourceId) {
  Service::InitParameters init_parameters = {
      .server_name = "server_name",
      .control_frequency_hz = 50,
      .resource_id = "ur3e_scene_object",
  };
  ASSERT_OK_AND_ASSIGN(
      auto world_service_from_fragment,
      WorldFromSceneObjectGzfService::Create(kSceneObjectGzfPath));
  ASSERT_OK_AND_ASSIGN(auto kinematics_from_world_service,
                       KinematicsFromWorldService::Create(
                           init_parameters, world_service_from_fragment.get()));
  intrinsic_proto::icon::DynamicsServiceConfig dynamics_service_config;
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<DynamicsService> dynamics_service,
                       DynamicsFromKinematicsService::Create(
                           init_parameters, kinematics_from_world_service.get(),
                           dynamics_service_config));
  ASSERT_OK_AND_ASSIGN(
      auto solver,
      dynamics_service->CreateDynamicsSolverForPart("ur3e_scene_object"));
}

TEST(DynamicsFromKinematicsServiceTest, ZeroGravityWorks) {
  Service::InitParameters init_parameters = {
      .server_name = "server_name",
      .control_frequency_hz = 50,
      .resource_id = "ur3e_scene_object",
  };
  ASSERT_OK_AND_ASSIGN(
      auto world_service_from_fragment,
      WorldFromSceneObjectGzfService::Create(kSceneObjectGzfPath));
  ASSERT_OK_AND_ASSIGN(auto kinematics_from_world_service,
                       KinematicsFromWorldService::Create(
                           init_parameters, world_service_from_fragment.get()));
  intrinsic_proto::icon::DynamicsServiceConfig dynamics_service_config;
  dynamics_service_config.set_use_zero_gravity(true);
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<DynamicsService> dynamics_service,
                       DynamicsFromKinematicsService::Create(
                           init_parameters, kinematics_from_world_service.get(),
                           dynamics_service_config));
  ASSERT_OK_AND_ASSIGN(
      auto solver,
      dynamics_service->CreateDynamicsSolverForPart("ur3e_scene_object"));
  INTRINSIC_RT_ASSERT_OK_AND_ASSIGN(
      eigenmath::VectorNd gravity_vector,
      solver->ComputeGeneralizedGravityVector(eigenmath::Vector6d::Zero()));
  EXPECT_THAT(gravity_vector, Each(Eq(0.0)));
}

TEST(DynamicsFromKinematicsServiceTest,
     FromProtoFallsBackToNameIfResourceIdFails) {
  Service::InitParameters init_parameters = {
      .server_name = "server_name",
      .control_frequency_hz = 50,
      .resource_id = "WRONG",
  };
  ASSERT_OK_AND_ASSIGN(
      auto world_service_from_fragment,
      WorldFromSceneObjectGzfService::Create(kSceneObjectGzfPath));
  ASSERT_OK_AND_ASSIGN(auto kinematics_from_world_service,
                       KinematicsFromWorldService::Create(
                           init_parameters, world_service_from_fragment.get()));
  intrinsic_proto::icon::DynamicsServiceConfig dynamics_service_config;
  dynamics_service_config.set_part_name("ur3e_scene_object");
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<DynamicsService> dynamics_service,
                       DynamicsFromKinematicsService::Create(
                           init_parameters, kinematics_from_world_service.get(),
                           dynamics_service_config));
  ASSERT_OK_AND_ASSIGN(
      auto solver,
      dynamics_service->CreateDynamicsSolverForPart("ur3e_scene_object"));
}

TEST(DynamicsFromKinematicsServiceTest,
     FromProtoFailsWithBadNameAndResourceId) {
  Service::InitParameters init_parameters = {
      .server_name = "server_name",
      .control_frequency_hz = 50,
      .resource_id = "zorb",
  };
  ASSERT_OK_AND_ASSIGN(
      auto world_service_from_fragment,
      WorldFromSceneObjectGzfService::Create(kSceneObjectGzfPath));
  ASSERT_OK_AND_ASSIGN(auto kinematics_from_world_service,
                       KinematicsFromWorldService::Create(
                           init_parameters, world_service_from_fragment.get()));
  intrinsic_proto::icon::DynamicsServiceConfig dynamics_service_config;
  dynamics_service_config.set_part_name("zorb");
  EXPECT_THAT(DynamicsFromKinematicsService::Create(
                  init_parameters, kinematics_from_world_service.get(),
                  dynamics_service_config),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace intrinsic::icon
