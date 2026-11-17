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

#ifndef INTRINSIC_WORLD_PUBSUB_WORLD_TF_PUBLISHER_H_
#define INTRINSIC_WORLD_PUBSUB_WORLD_TF_PUBLISHER_H_

#include <future>
#include <memory>
#include <optional>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/thread/periodic.h"
#include "intrinsic/world/proto/tf_associations.pb.h"
#include "intrinsic/world/pubsub/tf_echo_guard_interfaces.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/util/tf_frame_util.h"

namespace intrinsic {

// The default ID of the world to publish.
inline constexpr absl::string_view kBeliefWorldId = "world";

// The default PubSub topic used to publish TF messages representing world
// transforms.
inline constexpr absl::string_view kBeliefWorldTfTopic = "tf";

// The default PubSub topic used to publish TFAssociations messages for mapping
// of TF frames to associated resources like geometries.
inline constexpr absl::string_view kBeliefWorldTfAssociationTopic =
    "tf_associations";

// Publishes world transforms to a PubSub topic.
class WorldTfPublisher {
 public:
  struct Options {
    // The interval at which the internal transform buffer updates and
    // publishes TF frames from the belief world.
    //
    // The default interval is selected to match the default publishing rate
    // of the ROS robot_state_publisher standard, providing smooth tracking.
    absl::Duration tf_buffer_update_interval{absl::Milliseconds(20)};

    // The interval at which the TF associations (mapping frames to resources
    // like geometries) are periodically published over PubSub, ensuring
    // late joiners reliably receive the state.
    //
    // The default is selected reflecting that these structural associations
    // change far less frequently than pure transforms.
    absl::Duration tf_associations_publish_interval{absl::Milliseconds(2000)};

    // The interval at which the system checks the world structure hash to
    // detect tree changes, computing new TF associations if needed.
    //
    // The default is a fast check rate matching the TF buffer update rate
    // to ensure structural modifications trigger an immediate non-blocking
    // publish without waiting for the slower publish cycle.
    absl::Duration tf_associations_compute_interval{absl::Milliseconds(20)};

    // The ID of the world to publish.
    std::string world_id{kBeliefWorldId};

    // The PubSub topic used to publish TF messages representing world
    // transforms.
    std::string tf_topic{kBeliefWorldTfTopic};

    // The PubSub topic used to publish TFAssociations messages.
    std::string tf_associations_topic{kBeliefWorldTfAssociationTopic};
  };

  static absl::StatusOr<std::unique_ptr<WorldTfPublisher>> Create(
      std::future<WorldStorage*> world_storage,
      TfEchoGuardPublisherInterface* echo_guard,
      std::optional<Options> options = std::nullopt);

  // Spins up a thread to periodically publish TF frames from the belief world.
  absl::Status Start();

  ~WorldTfPublisher();

 private:
  WorldTfPublisher(std::future<WorldStorage*> world_storage,
                   TfEchoGuardPublisherInterface* echo_guard, Options options);

  // Build and publish a TF message of all frames in the belief world. Also
  // use this TF message to update the internal TF buffer for future
  // queries.
  absl::Status UpdateAndPublishTf();

  // Publish the most recently cached TFAssociations message.
  absl::Status PublishAssociations();

  // Periodically check if the world structure changed to compute new
  // associations.
  absl::Status ComputeAssociationsIfChanged();

  intrinsic::PubSub pubsub_;
  std::unique_ptr<intrinsic::Publisher> tf_publisher_;
  std::unique_ptr<intrinsic::PeriodicOperation> tf_broadcaster_closure_;

  std::unique_ptr<intrinsic::Publisher> tf_associations_publisher_;
  std::unique_ptr<intrinsic::PeriodicOperation>
      tf_associations_broadcaster_closure_;

  std::unique_ptr<intrinsic::PeriodicOperation>
      tf_associations_computation_closure_;

  absl::Mutex world_storage_mtx_;
  std::future<WorldStorage*> world_storage_future_
      ABSL_GUARDED_BY(world_storage_mtx_);
  WorldStorage* world_storage_ ABSL_GUARDED_BY(world_storage_mtx_) = nullptr;
  WorldStorage* GetWorldStorage();

  TfEchoGuardPublisherInterface* echo_guard_;
  absl::Mutex associations_mtx_;
  std::string world_structure_hash_ ABSL_GUARDED_BY(associations_mtx_);
  std::optional<intrinsic_proto::world::TFAssociations> cached_associations_
      ABSL_GUARDED_BY(associations_mtx_);

  const Options options_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_PUBSUB_WORLD_TF_PUBLISHER_H_
