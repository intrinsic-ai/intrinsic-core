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

#include "intrinsic/connect/cc/grpc/channel.h"

#include <climits>
#include <memory>
#include <optional>
#include <string>

#include "absl/log/die_if_null.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpc/grpc.h"
#include "grpcpp/channel.h"
#include "grpcpp/client_context.h"
#include "grpcpp/completion_queue.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/support/channel_arguments.h"
#include "grpcpp/support/status.h"
#include "grpcpp/support/time.h"
#include "intrinsic/util/grpc/limits.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/time/clock.h"
#include "intrinsic/util/time/deadline_timeout.h"
#include "src/proto/grpc/health/v1/health.grpc.pb.h"
#include "src/proto/grpc/health/v1/health.pb.h"

namespace intrinsic::connect {

namespace {

using ::grpc::health::v1::HealthCheckRequest;
using ::grpc::health::v1::HealthCheckResponse;

// Default timeout when invoking CheckChannelHealth.
constexpr absl::Duration kCheckChannelHealthTimeout = absl::Seconds(1);
// Pause duration when retrying after a failed CheckChannelHealth call.
constexpr absl::Duration kCheckChannelHealthRetryPause =
    absl::Milliseconds(200);

// Returns OK if the server responds to a noop RPC. This ensures that the
// channel can be used for other RPCs.
absl::Status CheckChannelHealth(
    std::shared_ptr<::grpc::Channel> channel, absl::Duration timeout,
    std::optional<absl::string_view> server_instance_name) {
  // Try an arbitrary RPC (we use the Health service but could use anything that
  // responds quickly without side-effects). Use the async client because the
  // sync client doesn't seem to respect the deadline for certain channels.
  auto health_stub = grpc::health::v1::Health::NewStub(channel);

  grpc::CompletionQueue cq;
  absl::Time deadline = absl::Now() + timeout;
  grpc::ClientContext ctx;
  ctx.set_deadline(deadline);
  if (server_instance_name.has_value()) {
    ctx.AddMetadata("x-resource-instance-name",
                    std::string(*server_instance_name));
  }
  HealthCheckResponse resp;
  grpc::Status status;
  std::unique_ptr<grpc::ClientAsyncResponseReader<HealthCheckResponse>> rpc(
      health_stub->AsyncCheck(&ctx, HealthCheckRequest(), &cq));
  int tag = 1;
  rpc->Finish(&resp, &status, &tag);

  // Wait for the response. If it succeeds or returns "unimplemented", then we
  // know the channel is healthy.
  void* got_tag;
  bool ok = false;
  if (cq.AsyncNext(&got_tag, &ok, deadline) !=
          grpc::CompletionQueue::GOT_EVENT ||
      *static_cast<int*>(got_tag) != tag || !ok) {
    return absl::DeadlineExceededError(
        "deadline exceeded when checking channel health");
  }

  if (!status.ok() && status.error_code() != grpc::StatusCode::UNIMPLEMENTED) {
    return ToAbslStatus(status);
  }
  return absl::OkStatus();
}

absl::string_view ConnectivityStateToString(grpc_connectivity_state state) {
  switch (state) {
    case GRPC_CHANNEL_READY:
      return "GRPC_CHANNEL_READY";
    case GRPC_CHANNEL_IDLE:
      return "GRPC_CHANNEL_IDLE";
    case GRPC_CHANNEL_CONNECTING:
      return "GRPC_CHANNEL_CONNECTING";
    case GRPC_CHANNEL_TRANSIENT_FAILURE:
      return "GRPC_CHANNEL_TRANSIENT_FAILURE";
    case GRPC_CHANNEL_SHUTDOWN:
      return "GRPC_CHANNEL_SHUTDOWN";
  }
  return "UNKNOWN";
}

}  // namespace

absl::Status WaitForChannelConnected(absl::string_view address,
                                     std::shared_ptr<::grpc::Channel> channel,
                                     absl::Time deadline) {
  if (channel->GetState(/*try_to_connect=*/true) == GRPC_CHANNEL_READY) {
    return absl::OkStatus();
  }
  channel->WaitForConnected(absl::ToChronoTime(deadline));
  grpc_connectivity_state channel_state =
      channel->GetState(/*try_to_connect=*/false);
  if (channel_state == GRPC_CHANNEL_READY) {
    return absl::OkStatus();
  }
  return absl::UnavailableError(
      absl::StrCat("gRPC channel to ", address, " is unavailable.  State is ",
                   ConnectivityStateToString(channel_state)));
}

absl::Status WaitForChannelReady(std::shared_ptr<::grpc::Channel> channel,
                                 absl::Duration timeout) {
  const absl::Time deadline = intrinsic::ToDeadline(timeout);
  while (absl::Now() < deadline) {
    if (channel->GetState(/*try_to_connect=*/true) != GRPC_CHANNEL_READY) {
      channel->WaitForConnected(absl::ToChronoTime(deadline));
    }
    grpc_connectivity_state state = channel->GetState(/*try_to_connect=*/false);
    if (state != GRPC_CHANNEL_READY) {
      LOG(WARNING) << "Channel not ready: state is "
                   << ConnectivityStateToString(state);
      continue;
    }
    absl::Status status =
        CheckChannelHealth(channel, kCheckChannelHealthTimeout,
                           /*server_instance_name=*/std::nullopt);
    if (status.ok()) {
      return absl::OkStatus();
    }
    LOG(ERROR) << "Unhealthy channel: " << status;
    // Sleep to prevent busy-looping if CheckChannelHealth returns immediately.
    absl::SleepFor(kCheckChannelHealthRetryPause);
  }

  return absl::UnavailableError(
      "gRPC channel is unavailable. Deadline exceeded waiting for gRPC channel "
      "to be ready.");
}

::grpc::ChannelArguments DefaultGrpcChannelArgs() {
  ::grpc::ChannelArguments channel_args;
  // TODO(b/33781585): There is no current way to change the minimum
  // backoff time, which defaults to 20s, except by setting this testing
  // variable and fixing the backoff.  This is obviously brittle, but currently
  // there is no other alternative unless we want to block for 20s.
  channel_args.SetInt("grpc.testing.fixed_reconnect_backoff_ms", 1000);
  channel_args.SetInt(GRPC_ARG_MAX_RECONNECT_BACKOFF_MS, 1000);

  // Disable gRPC client-side keepalive. This is a temporary fix, as
  // //third_party/blue targets depend on //net/grpc but they do not call
  // InitGoogle(). These targets should either use //third_party/grpc instead,
  // or call InitGoogle().
  channel_args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, INT_MAX);
  channel_args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 20000);
  channel_args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 0);

  // Increase metadata size, this includes, for example, the size of the
  // information gathered from an absl::Status on error.
  // Soft limit
  channel_args.SetInt(GRPC_ARG_MAX_METADATA_SIZE,
                      kGrpcRecommendedMaxMetadataSoftLimit);
  // Hard limit, some requests exceeding the soft limit but are below the hard
  // limit will be rejected. Anything exceeding the hard limit will be rejected.
  channel_args.SetInt(GRPC_ARG_ABSOLUTE_MAX_METADATA_SIZE,
                      kGrpcRecommendedMaxMetadataHardLimit);

  // Disable DNS resolution for service config. These calls can impact
  // performance negatively on some DNS servers (i.e. Vodafone LTE on-site in
  // Europe).
  channel_args.SetInt(GRPC_ARG_SERVICE_CONFIG_DISABLE_RESOLUTION, 1);
  return channel_args;
}

::grpc::ChannelArguments UnlimitedMessageSizeGrpcChannelArgs() {
  ::grpc::ChannelArguments channel_args = DefaultGrpcChannelArgs();
  channel_args.SetMaxReceiveMessageSize(-1);
  channel_args.SetMaxSendMessageSize(-1);
  return channel_args;
}

absl::StatusOr<std::shared_ptr<::grpc::Channel>> CreateClientChannel(
    const absl::string_view address, absl::Time deadline,
    const ::grpc::ChannelArguments& channel_args,
    bool use_default_application_credentials,
    std::optional<std::string> server_instance_name) {
  if (use_default_application_credentials) {
    return GrpcChannel(address)
        .WithDeadline(deadline)
        .WithCustomChannelArgs(channel_args)
        .Connect();
  }

  return GrpcChannel(address)
      .WithDeadline(deadline)
      .WithCustomChannelArgs(channel_args)
      .WithChannelCredentials(grpc::InsecureChannelCredentials())  // NOLINT
      .WithCheckChannelHealth(server_instance_name)
      .Connect();
}

GrpcChannel::GrpcChannel(absl::string_view address) : address_(address) {}

GrpcChannel::GrpcChannel(absl::string_view address, ClockInterface* clock)
    : address_(address), clock_(ABSL_DIE_IF_NULL(clock)) {}

GrpcChannel& GrpcChannel::WithTimeout(absl::Duration timeout) {
  deadline_ = clock_->Now() + timeout;
  return *this;
}

GrpcChannel& GrpcChannel::WithDeadline(absl::Time deadline) {
  deadline_ = deadline;
  return *this;
}

GrpcChannel& GrpcChannel::WithChannelCredentials(
    std::shared_ptr<grpc::ChannelCredentials> credentials) {
  credentials_ = credentials;
  return *this;
}

GrpcChannel& GrpcChannel::WithCheckChannelHealth(
    std::optional<absl::string_view> server_instance_name) {
  check_channel_health_ = CheckChannelHealthOptions{
      .server_instance_name = server_instance_name,
  };
  return *this;
}

GrpcChannel& GrpcChannel::WithUnlimitedMessageSizeChannelArgs() {
  channel_args_ = UnlimitedMessageSizeGrpcChannelArgs();
  return *this;
}

GrpcChannel& GrpcChannel::WithCustomChannelArgs(
    const grpc::ChannelArguments& channel_args) {
  channel_args_ = channel_args;
  return *this;
}

absl::StatusOr<std::shared_ptr<grpc::Channel>> GrpcChannel::Connect() {
  if (consumed_) {
    return absl::FailedPreconditionError(
        "GrpcChannel::Connect() can only be called once.");
  }
  consumed_ = true;

  LOG(INFO) << "Connecting to " << address_
            << " (timeout: " << deadline_ - clock_->Now()
            << (check_channel_health_.has_value() &&
                        check_channel_health_->server_instance_name.has_value()
                    ? absl::StrCat(", instance: ",
                                   *check_channel_health_->server_instance_name)
                    : "")
            << ")";

  absl::Status status;
  std::shared_ptr<::grpc::Channel> channel = ::grpc::CreateCustomChannel(
      address_,
      credentials_ == nullptr ? grpc::GoogleDefaultCredentials() : credentials_,
      channel_args_);
  while (clock_->Now() < deadline_) {
    status = WaitForChannelConnected(address_, channel, deadline_);
    if (!status.ok()) {
      LOG(WARNING) << "Channel not ready: " << status;
      continue;
    }

    if (!check_channel_health_.has_value()) {
      LOG(INFO) << "Skipping channel health check for " << address_;
      LOG(INFO) << "Successfully connected to " << address_;
      return channel;
    }

    // For some reason, WaitForChannelConnected can return "ok" even when the
    // server is not yet running. When checking for this case, use a short
    // timeout to allow time to retry after.
    status = CheckChannelHealth(channel, kCheckChannelHealthTimeout,
                                check_channel_health_->server_instance_name);
    if (!status.ok()) {
      LOG(ERROR) << "Unhealthy channel for " << address_ << ": " << status;
      // Sleep to prevent busy-looping if CheckChannelHealth returns
      // immediately.
      absl::SleepFor(kCheckChannelHealthRetryPause);
      continue;
    }

    LOG(INFO) << "Successfully connected to " << address_;
    return channel;
  }

  INTR_RETURN_IF_ERROR(status) << "failed to connect to channel by specified "
                                  "deadline; returning last channel status";

  return absl::DeadlineExceededError(
      "deadline exceeded when connecting to channel");
}

}  // namespace intrinsic::connect
