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

#include "intrinsic/icon/cc_client/client_utils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/wrappers.pb.h"
#include "grpcpp/support/status.h"
#include "internal/testing.h"
#include "intrinsic/icon/cc_client/client.h"
#include "intrinsic/icon/cc_client/condition.h"
#include "intrinsic/icon/common/builtins.h"
#include "intrinsic/icon/common/part_properties.h"
#include "intrinsic/icon/proto/v1/condition_types.pb.h"
#include "intrinsic/icon/proto/v1/service.pb.h"
#include "intrinsic/icon/proto/v1/service_mock.grpc.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "protobuf-matchers/protocol-buffer-matchers.h"

namespace intrinsic {
namespace icon {
namespace {

using ::absl_testing::StatusIs;
using ::protobuf_matchers::EqualsProto;
using ::testing::_;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Key;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

struct Mock {
  intrinsic_proto::icon::v1::MockIconApiStub* stub;
  std::unique_ptr<Client> client;
};

Mock CreateMockClient() {
  Mock mock;
  auto stub = std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  mock.stub = stub.get();
  mock.client = std::make_unique<Client>(std::move(stub));
  return mock;
}

TEST(ClientUtils, IsDone) {
  EXPECT_EQ(IsDone().state_variable_name(), kIsDone);
  EXPECT_EQ(IsDone().operation(), intrinsic_proto::icon::v1::Comparison::EQUAL);
  EXPECT_EQ(std::get<bool>(IsDone().value()), true);
}

TEST(GetPartConfig, GetPartConfigFailsPartNotFound) {
  auto stub = std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  intrinsic_proto::icon::v1::MockIconApiStub* mock_stub = stub.get();
  auto mock_client = std::make_unique<Client>(std::move(stub));

  // Prepare mock method.
  intrinsic_proto::icon::v1::GetConfigRequest request;
  intrinsic_proto::icon::v1::GetConfigResponse response;
  EXPECT_CALL(*mock_stub, GetConfig(_, EqualsProto(request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client and check results.
  EXPECT_THAT(
      GetPartConfig<google::protobuf::FloatValue>(*mock_client, "robot_arm"),
      StatusIs(absl::StatusCode::kNotFound));
}

TEST(GetPartConfig, GetPartConfigFailsWrongType) {
  auto stub = std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  intrinsic_proto::icon::v1::MockIconApiStub* mock_stub = stub.get();
  auto mock_client = std::make_unique<Client>(std::move(stub));

  // Prepare mock method.
  intrinsic_proto::icon::v1::GetConfigRequest request;
  intrinsic_proto::icon::v1::GetConfigResponse response;
  intrinsic_proto::icon::v1::PartConfig* part_config =
      response.add_part_configs();
  part_config->set_name("robot_arm");
  google::protobuf::Int32Value value;
  value.set_value(123);
  ASSERT_TRUE(part_config->mutable_config()->PackFrom(value));
  EXPECT_CALL(*mock_stub, GetConfig(_, EqualsProto(request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client and check results.
  EXPECT_THAT(
      GetPartConfig<google::protobuf::FloatValue>(*mock_client, "robot_arm"),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(GetPartConfig, GetPartConfigSucceeds) {
  auto stub = std::make_unique<intrinsic_proto::icon::v1::MockIconApiStub>();
  intrinsic_proto::icon::v1::MockIconApiStub* mock_stub = stub.get();
  auto mock_client = std::make_unique<Client>(std::move(stub));

  // Prepare mock method.
  intrinsic_proto::icon::v1::GetConfigRequest request;
  intrinsic_proto::icon::v1::GetConfigResponse response;
  intrinsic_proto::icon::v1::PartConfig* part_config =
      response.add_part_configs();
  part_config->set_name("robot_arm");
  google::protobuf::Int32Value value;
  value.set_value(123);
  ASSERT_TRUE(part_config->mutable_config()->PackFrom(value));
  EXPECT_CALL(*mock_stub, GetConfig(_, EqualsProto(request), _))
      .WillOnce(DoAll(SetArgPointee<2>(response), Return(::grpc::Status::OK)));

  // Invoke it through client and check results.
  ASSERT_OK_AND_ASSIGN(
      google::protobuf::Int32Value result_proto,
      GetPartConfig<google::protobuf::Int32Value>(*mock_client, "robot_arm"));
  EXPECT_THAT(value, EqualsProto(result_proto));
}

TEST(Client, SetPartPropertyBoolSucceeds) {
  Mock mock = CreateMockClient();

  intrinsic_proto::icon::v1::SetPartPropertiesRequest req;
  EXPECT_CALL(*mock.stub, SetPartProperties(_, _, _))
      .WillOnce(
          DoAll(SaveArg<1>(&req),
                SetArgPointee<2>(
                    intrinsic_proto::icon::v1::SetPartPropertiesResponse()),
                Return(::grpc::Status::OK)));

  EXPECT_OK(SetPartProperty(*mock.client, "arm", "bool_prop", true));

  ASSERT_THAT(req.part_properties_by_part_name(), ElementsAre(Key("arm")));
  EXPECT_THAT(
      req.part_properties_by_part_name().at("arm").property_values_by_name(),
      ElementsAre(
          Pair("bool_prop", EqualsProto(ToProto(PartPropertyValue(true))))));
}

TEST(Client, SetPartPropertyDoubleSucceeds) {
  Mock mock = CreateMockClient();

  intrinsic_proto::icon::v1::SetPartPropertiesRequest req;
  EXPECT_CALL(*mock.stub, SetPartProperties(_, _, _))
      .WillOnce(
          DoAll(SaveArg<1>(&req),
                SetArgPointee<2>(
                    intrinsic_proto::icon::v1::SetPartPropertiesResponse()),
                Return(::grpc::Status::OK)));

  EXPECT_OK(SetPartProperty(*mock.client, "arm", "double_prop", 1.234));

  ASSERT_THAT(req.part_properties_by_part_name(), ElementsAre(Key("arm")));
  EXPECT_THAT(
      req.part_properties_by_part_name().at("arm").property_values_by_name(),
      ElementsAre(
          Pair("double_prop", EqualsProto(ToProto(PartPropertyValue(1.234))))));
}

}  // namespace
}  // namespace icon
}  // namespace intrinsic
