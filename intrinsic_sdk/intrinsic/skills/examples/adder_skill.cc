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

#include "intrinsic/skills/examples/adder_skill.h"

#include <cstdint>
#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "google/protobuf/message.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "intrinsic/assets/services/examples/calcserver/calc_server.grpc.pb.h"
#include "intrinsic/assets/services/examples/calcserver/calc_server.pb.h"
#include "intrinsic/resources/proto/resource_handle.pb.h"
#include "intrinsic/skills/cc/equipment_pack.h"
#include "intrinsic/skills/cc/skill_interface.h"
#include "intrinsic/skills/examples/adder_skill.pb.h"
#include "intrinsic/skills/proto/equipment.pb.h"
#include "intrinsic/skills/proto/skill_service.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace skills {

absl::StatusOr<std::unique_ptr<google::protobuf::Message>> AdderSkill::Execute(
    const ExecuteRequest& request, ExecuteContext& context) {
  // Get the skill's parameters.
  INTR_ASSIGN_OR_RETURN(auto params,
                        request.params<intrinsic_proto::skills::AdderParams>());

  LOG(INFO) << "Adding " << params.x() << " and " << params.y();

  // Get the equipment.
  INTR_ASSIGN_OR_RETURN(intrinsic_proto::resources::ResourceHandle calc_handle,
                        context.equipment().GetHandle(kCalculatorSlot));
  const std::string& address = calc_handle.connection_info().grpc().address();
  const std::string& instance =
      calc_handle.connection_info().grpc().server_instance();

  // Connect to the Calculator service.
  std::shared_ptr<grpc::Channel> channel = ::grpc::CreateChannel(
      address, grpc::InsecureChannelCredentials());  // NOLINT(insecure)
  auto stub = intrinsic_proto::services::Calculator::NewStub(channel);

  // Create the request.
  intrinsic_proto::services::CalculatorRequest calculator_request;
  calculator_request.set_operation(
      intrinsic_proto::services::CalculatorOperation::CALCULATOR_OPERATION_ADD);
  calculator_request.set_x(params.x());
  calculator_request.set_y(params.y());

  LOG(INFO) << "Calling the Calculator service";

  // We need to add the server_instance name to the header so that the ingress
  // can properly route the request.
  ::grpc::ClientContext ctx;
  ctx.AddMetadata("x-resource-instance-name", instance);
  intrinsic_proto::services::CalculatorResponse response;
  INTR_RETURN_IF_ERROR(
      ToAbslStatus(stub->Calculate(&ctx, calculator_request, &response)));

  const int64_t sum = response.result();
  LOG(INFO) << "and the answer is " << sum;

  auto return_value = std::make_unique<intrinsic_proto::skills::AdderResult>();
  return_value->set_sum(sum);
  return return_value;
}

}  // namespace skills
}  // namespace intrinsic
