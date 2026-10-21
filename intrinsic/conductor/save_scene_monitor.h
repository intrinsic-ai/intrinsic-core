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

#ifndef INTRINSIC_CONDUCTOR_SAVE_SCENE_MONITOR_H_
#define INTRINSIC_CONDUCTOR_SAVE_SCENE_MONITOR_H_

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "intrinsic/assets/proto/v1alpha1/asset_info_internal.grpc.pb.h"
#include "intrinsic/conductor/proto/save_status.pb.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/storage/hot_shared_state/proto/v1/resource_set_service.grpc.pb.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"

namespace intrinsic::conductor {

struct SaveSceneMonitorOptions {
  intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface*
      asset_info_internal = nullptr;
  intrinsic_proto::world::ObjectWorldService::Service* world_service = nullptr;
  std::string hss_address;
};

class SaveSceneMonitor {
 public:
  struct SaveStatusResult {
    intrinsic_proto::conductor::SaveStatus status;
    absl::Time last_save_time;
  };

  static absl::StatusOr<std::unique_ptr<SaveSceneMonitor>> Create(
      const std::string& world_id, const SaveSceneMonitorOptions& options);

  ~SaveSceneMonitor();

  // GetSaveStatus returns the save status against the given world ID and the
  // last time that this monitor detected that the world had been saved.
  absl::StatusOr<SaveStatusResult> GetSaveStatus(const std::string& world_id);

  SaveSceneMonitor(const SaveSceneMonitor&) = delete;
  SaveSceneMonitor& operator=(const SaveSceneMonitor&) = delete;

 private:
  SaveSceneMonitor(
      const std::string& world_id,
      intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface&
          asset_info_internal,
      intrinsic_proto::world::ObjectWorldService::Service& world_service,
      std::unique_ptr<intrinsic_proto::hot_shared_state::v1::
                          HotSharedStateResourceSetService::Stub>
          rss_stub,
      PubSub pubsub);

  void StartPolling();
  void StopPolling();
  absl::Status PollLoop(std::stop_token stop_token);

  struct ScanResult {
    absl::Time scan_time;
    absl::Time last_save_time;
    intrinsic_proto::conductor::SaveStatus save_status;
  };

  absl::StatusOr<ScanResult> ScanSaveStatus(const std::string& world_id)
      ABSL_LOCKS_EXCLUDED(mu_);
  absl::StatusOr<ScanResult> ScanSaveStatusLocked(const std::string& world_id)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  const std::string world_id_;
  intrinsic_proto::assets::v1alpha1::AssetInfoInternal::StubInterface&
      asset_info_internal_;
  intrinsic_proto::world::ObjectWorldService::Service& world_service_;
  const std::unique_ptr<intrinsic_proto::hot_shared_state::v1::
                            HotSharedStateResourceSetService::Stub>
      rss_stub_;
  const PubSub pubsub_;

  absl::Mutex mu_;
  std::string last_application_revision_token_ ABSL_GUARDED_BY(mu_);
  absl::Time last_save_time_ ABSL_GUARDED_BY(mu_);
  bool op_world_exists_ ABSL_GUARDED_BY(mu_) = false;

  std::jthread polling_thread_;
};

}  // namespace intrinsic::conductor

#endif  // INTRINSIC_CONDUCTOR_SAVE_SCENE_MONITOR_H_
