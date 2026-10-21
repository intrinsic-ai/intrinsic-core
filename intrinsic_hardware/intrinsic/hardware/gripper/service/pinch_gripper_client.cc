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

// Implementation of a synchronous Pinch Gripper client.
#include "intrinsic/hardware/gripper/service/pinch_gripper_client.h"

#include <stdio.h>

#include <memory>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/client_context.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/time.h"  // needed for grpc::Context::set_deadline()
#include "intrinsic/hardware/gripper/gripper.pb.h"
#include "intrinsic/hardware/gripper/service/proto/pinch_gripper_server.pb.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::gripper {

PinchGripperClient::PinchGripperClient(
    std::shared_ptr<::intrinsic::Channel> channel)
    : stub_(::intrinsic_proto::gripper::PinchGripperServer::PinchGripperServer::
                NewStub(channel->GetChannel())),
      channel_(channel) {}

absl::StatusOr<std::string> PinchGripperClient::CreatePinchGripper(
    bool test_only) {
  auto context = channel_->GetClientContextFactory()();
  // We do not set a deadline here, because the server-side implementation of
  // the CreatePinchGripper keeps on running as long as the created pinch
  // gripper stays alive. It may run a CyclicRunner that periodically
  // sends out the last command to the gripper to maintain connection (and
  // avoids a timeout).

  intrinsic_proto::gripper::CreatePinchGripperRequest request;
  intrinsic_proto::gripper::CreatePinchGripperResponse response;
  LOG(INFO) << "Sending CreatePinchGripperRequest: " << request;
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->CreatePinchGripper(context.get(), request, &response)));

  return response.gripper_handle();
}

absl::Status PinchGripperClient::ResetPinchGripper(
    absl::string_view gripper_handle,
    absl::Duration pinch_gripper_client_timeout) {
  // 1. Allocate ClientContext object and set options (especially deadline).
  auto context = channel_->GetClientContextFactory()();
  context->set_deadline(absl::Now() + pinch_gripper_client_timeout);

  // 2. Allocate request/response and fill-in the request.
  intrinsic_proto::gripper::ResetPinchGripperRequest request;
  intrinsic_proto::gripper::ResetPinchGripperResponse response;
  request.set_gripper_handle(gripper_handle);

  // 3. Invoke service method on stub -- gRPC forwards call to server.
  return ToAbslStatus(
      stub_->ResetPinchGripper(context.get(), request, &response));
}

absl::StatusOr<intrinsic_proto::gripper::PinchGripperStatus>
PinchGripperClient::CommandPinchGripper(
    absl::string_view gripper_handle,
    intrinsic_proto::gripper::PinchGripperCommand command,
    absl::Duration pinch_gripper_client_timeout) {
  // 1. Allocate ClientContext object and set options (especially deadline).
  auto context = channel_->GetClientContextFactory()();
  context->set_deadline(absl::Now() + pinch_gripper_client_timeout);

  // 2. Allocate request/response and fill-in the request.
  intrinsic_proto::gripper::CommandPinchGripperRequest request;
  intrinsic_proto::gripper::CommandPinchGripperResponse response;
  request.set_gripper_handle(gripper_handle);
  *request.mutable_command() = command;
  request.mutable_execution_timeout()->set_seconds(
      absl::ToInt64Seconds(pinch_gripper_client_timeout));

  // 3. Invoke service method on stub -- gRPC forwards call to server.
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->CommandPinchGripper(context.get(), request, &response)));

  return response.status();
}

absl::StatusOr<intrinsic_proto::gripper::PinchGripperStatus>
PinchGripperClient::GetPinchGripperStatus(
    absl::string_view gripper_handle,
    absl::Duration pinch_gripper_client_timeout) {
  // 1. Allocate ClientContext object and set options (especially deadline).
  auto context = channel_->GetClientContextFactory()();
  context->set_deadline(absl::Now() + pinch_gripper_client_timeout);

  // 2. Allocate request/response and fill-in the request.
  intrinsic_proto::gripper::GetPinchGripperStatusRequest request;
  intrinsic_proto::gripper::GetPinchGripperStatusResponse response;
  request.set_gripper_handle(gripper_handle);

  // 3. Invoke service method on stub -- gRPC forwards call to server.
  INTR_RETURN_IF_ERROR(ToAbslStatus(
      stub_->GetPinchGripperStatus(context.get(), request, &response)));

  return response.status();
}

}  // namespace intrinsic::gripper
