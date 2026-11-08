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

#include "intrinsic/icon/dynamics/dynamics_factory.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"

namespace intrinsic::icon {
namespace {

using ::absl_testing::StatusIs;
using ::testing::ByMove;
using ::testing::HasSubstr;
using ::testing::MockFunction;
using ::testing::Return;

TEST(DynamicsFactoryTest, Works) {
  const std::string kName = "name";
  // Fail before adding a create function.
  EXPECT_THAT(
      GetGlobalDynamicsFactory().CreateDynamicsImpl(kName,
                                                    /*skeleton=*/nullptr),
      StatusIs(absl::StatusCode::kNotFound, HasSubstr(kName)));

  MockFunction<DynamicsFactory::DynamicsFactoryFunction> mock_create_function;
  EXPECT_TRUE(GetGlobalDynamicsFactory().Register(
      kName, mock_create_function.AsStdFunction()));

  // Return a known error from the create function, rather than going through
  // the trouble of building an actual RigidBodyDynamicsInterface.
  EXPECT_CALL(mock_create_function, Call)
      .WillOnce(Return(ByMove(
          absl::InternalError("Robot zombies have invaded the factory!"))));
  // Calls create function.
  EXPECT_THAT(
      GetGlobalDynamicsFactory().CreateDynamicsImpl(kName,
                                                    /*skeleton=*/nullptr),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("Robot zombies")));
  // After clearing the factory, we should get NotFound again, with no further
  // calls to the create function.
  GetGlobalDynamicsFactory().ClearForTestingOnly();
  EXPECT_CALL(mock_create_function, Call).Times(0);
  EXPECT_THAT(GetGlobalDynamicsFactory().CreateDynamicsImpl(
                  kName, /*skeleton=*/nullptr),
              StatusIs(absl::StatusCode::kNotFound, HasSubstr(kName)));

  // Registering the same function again should work.
  EXPECT_TRUE(GetGlobalDynamicsFactory().Register(
      kName, mock_create_function.AsStdFunction()));
}

}  // namespace
}  // namespace intrinsic::icon
