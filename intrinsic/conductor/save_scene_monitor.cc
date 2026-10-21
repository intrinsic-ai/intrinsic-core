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

#include "intrinsic/conductor/save_scene_monitor.h"

#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "intrinsic/conductor/world.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/proto_time.h"
#include "intrinsic/util/status/status_conversion_grpc.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::conductor {

using ::intrinsic_proto::conductor::SaveState;
using ::intrinsic_proto::conductor::SaveStatus;
using ::intrinsic_proto::hot_shared_state::v1::GetCurrentResourceSetRequest;
using ::intrinsic_proto::hot_shared_state::v1::GetCurrentResourceSetResponse;
using ::intrinsic_proto::hot_shared_state::v1::HotSharedStateResourceSetService;
using ::intrinsic_proto::world::CompareWorldsRequest;
using ::intrinsic_proto::world::CompareWorldsResponse;
using ::intrinsic_proto::world::GetWorldRequest;
using ::intrinsic_proto::world::ObjectView;
using ::intrinsic_proto::world::World;

namespace {
constexpr char kComposedWorldId[] = "save_monitor";
constexpr absl::Duration kMinPublishPeriod = absl::Seconds(5);
constexpr absl::Duration kPollInterval = absl::Seconds(1);
constexpr absl::Duration kDefaultGrpcTimeout = absl::Seconds(30);

std::unique_ptr<grpc::ClientContext> CreateClientContext(
    absl::Duration timeout = kDefaultGrpcTimeout) {
  auto context = std::make_unique<grpc::ClientContext>();
  if (timeout != absl::InfiniteDuration()) {
    context->set_deadline(absl::ToChronoTime(absl::Now() + timeout));
  }
  return context;
}
}  // namespace

absl::StatusOr<std::unique_ptr<SaveSceneMonitor>> SaveSceneMonitor::Create(
    const std::string& world_id, const SaveSceneMonitorOptions& options) {
  if (options.asset_info_internal == nullptr) {
    return absl::InvalidArgumentError(
        "options.asset_info_internal must not be null");
  }
  if (options.world_service == nullptr) {
    return absl::InvalidArgumentError("options.world_service must not be null");
  }

  auto rss_channel = grpc::CreateChannel(options.hss_address,
                                         grpc::InsecureChannelCredentials());
  auto rss_stub = HotSharedStateResourceSetService::NewStub(rss_channel);

  PubSub pubsub;

  // Using `new` because constructor is private.
  auto monitor = absl::WrapUnique(new SaveSceneMonitor(
      world_id, *options.asset_info_internal, *options.world_service,
      std::move(rss_stub), std::move(pubsub)));

  monitor->StartPolling();
  return monitor;
}

SaveSceneMonitor::SaveSceneMonitor(
    const std::string& world_id,
    intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface&
        asset_info_internal,
    intrinsic_proto::world::ObjectWorldService::Service& world_service,
    std::unique_ptr<HotSharedStateResourceSetService::Stub> rss_stub,
    PubSub pubsub)
    : world_id_(world_id),
      asset_info_internal_(asset_info_internal),
      world_service_(world_service),
      rss_stub_(std::move(rss_stub)),
      pubsub_(std::move(pubsub)),
      last_save_time_(absl::UnixEpoch()) {}

SaveSceneMonitor::~SaveSceneMonitor() { StopPolling(); }

void SaveSceneMonitor::StartPolling() {
  polling_thread_ = std::jthread([this](std::stop_token stop_token) {
    if (auto status = PollLoop(stop_token); !status.ok()) {
      LOG(ERROR) << "Save monitor polling loop failed: " << status;
    }
  });
}

void SaveSceneMonitor::StopPolling() {
  if (polling_thread_.joinable()) {
    polling_thread_.request_stop();
    polling_thread_.join();
  }
}

absl::Status SaveSceneMonitor::PollLoop(std::stop_token stop_token) {
  LOG(INFO) << "Starting save monitor polling.";

  TopicConfig config;
  config.topic_qos = TopicConfig::HighReliability;
  INTR_ASSIGN_OR_RETURN(
      Publisher pub, pubsub_.CreatePublisher("conductor/saveStatus", config),
      _ << "Unable to open publisher on topic conductor/saveStatus");

  absl::Time last_save_time = absl::UnixEpoch();
  absl::Time last_publish_time = absl::UnixEpoch();
  SaveStatus last_save_status = SaveStatus::SAVE_STATUS_UNKNOWN;

  while (!stop_token.stop_requested()) {
    absl::Time poll_time = absl::Now();

    auto scan_result = ScanSaveStatus(world_id_);
    if (!scan_result.ok() &&
        scan_result.status().code() != absl::StatusCode::kNotFound) {
      LOG(WARNING) << "Error performing scan: " << scan_result.status();
    }
    ScanResult result = scan_result.value_or(
        ScanResult{.scan_time = poll_time,
                   .last_save_time = last_save_time,
                   .save_status = SaveStatus::SAVE_STATUS_UNKNOWN});

    bool publish = false;
    publish = publish || (result.save_status != last_save_status);
    publish = publish || (result.last_save_time > last_save_time);
    publish = publish || (last_publish_time + kMinPublishPeriod < poll_time);

    if (publish) {
      SaveState proto;
      proto.set_status(result.save_status);
      proto.set_world_id(world_id_);

      INTR_ASSIGN_OR_RETURN(auto timestamp,
                            intrinsic::FromAbslTime(result.last_save_time),
                            _ << "Failed to convert last save time to proto");
      *proto.mutable_last_save_time() = timestamp;

      absl::Status pub_status = pub.Publish(proto);
      if (!pub_status.ok()) {
        LOG(WARNING) << "While polling save status encountered error "
                        "publishing on pubsub: "
                     << pub_status;
        if (stop_token.stop_requested()) {
          break;
        }
        // If a publish fails, we wait at least 1s before trying again,
        // to prevent overwhelming the publisher.
        absl::SleepFor(absl::Seconds(1));
        continue;
      }

      last_save_status = result.save_status;
      last_save_time = result.last_save_time;
      last_publish_time = poll_time;
    }

    absl::Time deadline = absl::Now() + kPollInterval;
    while (absl::Now() < deadline && !stop_token.stop_requested()) {
      absl::SleepFor(absl::Milliseconds(100));
    }
  }
  LOG(INFO) << "Stopping save monitor polling.";
  return absl::OkStatus();
}

absl::StatusOr<SaveSceneMonitor::SaveStatusResult>
SaveSceneMonitor::GetSaveStatus(const std::string& world_id) {
  INTR_ASSIGN_OR_RETURN(auto scan_result, ScanSaveStatus(world_id));
  return SaveStatusResult{.status = scan_result.save_status,
                          .last_save_time = scan_result.last_save_time};
}

absl::StatusOr<SaveSceneMonitor::ScanResult> SaveSceneMonitor::ScanSaveStatus(
    const std::string& world_id) {
  absl::MutexLock lock(&mu_);
  return ScanSaveStatusLocked(world_id);
}

absl::StatusOr<SaveSceneMonitor::ScanResult>
SaveSceneMonitor::ScanSaveStatusLocked(const std::string& world_id) {
  // Compare the worlds only after `world_id` world exists.
  // During deployment, `conductor` service invokes world rpcs to create the
  // worlds referenced in the execution context. We want these worlds to be
  // constructed as quickly as possible so as to not slow down the deployment
  // (and hit the 60s http relay timeout). If we run the save monitor right
  // away, then the create world rpc may end duplicating the work of fetching
  // the geos. This may slow down the world creation (and the deployment).
  // See: cl/816846818 for more details.
  if (!op_world_exists_) {
    grpc::ServerContext context;
    GetWorldRequest request;
    request.set_world_id(world_id);
    World response;
    grpc::Status status =
        world_service_.GetWorld(&context, &request, &response);
    if (!status.ok()) {
      LOG(INFO) << "World " << world_id
                << " does not exist yet, continuing to wait: "
                << status.error_message();
      return intrinsic::ToAbslStatus(status);
    }
    op_world_exists_ = true;
    LOG(INFO) << "World " << world_id << " exists, starting save monitor";
  }

  // First establish the last known world that is an exact replica of the
  // given resource set.
  std::string revision_token;
  {
    auto context = CreateClientContext();
    GetCurrentResourceSetRequest request;
    GetCurrentResourceSetResponse response;
    INTR_RETURN_IF_ERROR(intrinsic::ToAbslStatus(
        rss_stub_->GetCurrentResourceSet(context.get(), request, &response)));
    revision_token = response.revision_token();
  }

  // The execution context could have been reset in between triggers, so just
  // make sure that the comparison world is available before we check the
  // revision token.
  bool world_not_found = false;
  {
    grpc::ServerContext context;
    GetWorldRequest request;
    request.set_world_id(kComposedWorldId);
    World response;
    grpc::Status status =
        world_service_.GetWorld(&context, &request, &response);
    if (status.error_code() == grpc::StatusCode::NOT_FOUND) {
      world_not_found = true;
    } else if (!status.ok()) {
      return intrinsic::ToAbslStatus(status);
    }
  }

  if (world_not_found || revision_token != last_application_revision_token_) {
    INTR_RETURN_IF_ERROR(
        CreateOrUpdateWorld(kComposedWorldId, asset_info_internal_,
                            world_service_,
                            /*skip_invalid_world_updates=*/false)
            .status());
    last_save_time_ = absl::Now();
    last_application_revision_token_ = revision_token;
  }

  // Compare this world against the one requested.
  CompareWorldsResponse compare_response;
  {
    grpc::ServerContext context;
    CompareWorldsRequest request;
    request.set_base_world_id(kComposedWorldId);
    request.set_changed_world_id(world_id);
    request.set_unique_id_mode(CompareWorldsRequest::BY_NAME);
    request.set_view(ObjectView::FULL);
    INTR_RETURN_IF_ERROR(intrinsic::ToAbslStatus(
        world_service_.CompareWorlds(&context, &request, &compare_response)));
  }

  ScanResult result;
  result.scan_time = absl::Now();
  result.last_save_time = last_save_time_;
  result.save_status = SaveStatus::SAVE_STATUS_SAVED;

  if (!compare_response.added().empty() ||
      !compare_response.removed().empty() ||
      !compare_response.modified().empty() ||
      !compare_response.added_frames().empty() ||
      !compare_response.removed_frames().empty() ||
      !compare_response.modified_frames().empty()) {
    result.save_status = SaveStatus::SAVE_STATUS_MODIFIED;
  }

  return result;
}

}  // namespace intrinsic::conductor
