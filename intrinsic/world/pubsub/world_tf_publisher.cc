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

#include "intrinsic/world/pubsub/world_tf_publisher.h"

#include <future>
#include <memory>
#include <optional>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic/math/proto/tf_message.pb.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/periodic.h"
#include "intrinsic/world/pubsub/tf_echo_guard_interfaces.h"
#include "intrinsic/world/service/world_mutex.h"
#include "intrinsic/world/service/world_storage.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

absl::StatusOr<std::unique_ptr<WorldTfPublisher>> WorldTfPublisher::Create(
    std::future<WorldStorage*> world_storage,
    TfEchoGuardPublisherInterface* echo_guard, std::optional<Options> options) {
  return absl::WrapUnique(new WorldTfPublisher(
      std::move(world_storage), echo_guard, options.value_or(Options{})));
}

WorldTfPublisher::WorldTfPublisher(std::future<WorldStorage*> world_storage,
                                   TfEchoGuardPublisherInterface* echo_guard,
                                   WorldTfPublisher::Options options)
    : world_storage_future_(std::move(world_storage)),
      echo_guard_(echo_guard),
      options_(options) {}

WorldTfPublisher::~WorldTfPublisher() {
  if (tf_broadcaster_closure_) {
    CHECK_OK(tf_broadcaster_closure_->Stop());
  }
  if (tf_associations_broadcaster_closure_) {
    CHECK_OK(tf_associations_broadcaster_closure_->Stop());
  }
  if (tf_associations_computation_closure_) {
    CHECK_OK(tf_associations_computation_closure_->Stop());
  }
}

absl::Status WorldTfPublisher::Start() {
  INTR_ASSIGN_OR_RETURN(
      intrinsic::Publisher tf_pub,
      pubsub_.CreatePublisher(options_.tf_topic, TopicConfig()));
  tf_publisher_ = std::make_unique<intrinsic::Publisher>(std::move(tf_pub));
  tf_broadcaster_closure_ = std::make_unique<intrinsic::PeriodicOperation>(
      [this]() {
        const absl::Status publish_status = UpdateAndPublishTf();
        if (!publish_status.ok()) {
          LOG_EVERY_N_SEC(ERROR, 10.0)
              << "UpdateAndPublishTf() failed: " << publish_status;
        }
      },
      options_.tf_buffer_update_interval);
  INTR_RETURN_IF_ERROR(tf_broadcaster_closure_->Start());

  INTR_ASSIGN_OR_RETURN(
      intrinsic::Publisher assoc_pub,
      pubsub_.CreatePublisher(options_.tf_associations_topic, TopicConfig()));
  tf_associations_publisher_ =
      std::make_unique<intrinsic::Publisher>(std::move(assoc_pub));
  tf_associations_broadcaster_closure_ =
      std::make_unique<intrinsic::PeriodicOperation>(
          [this]() {
            const absl::Status publish_status = PublishAssociations();
            if (!publish_status.ok()) {
              LOG_EVERY_N_SEC(ERROR, 10.0)
                  << "PublishAssociations() failed: " << publish_status;
            }
          },
          options_.tf_associations_publish_interval);
  INTR_RETURN_IF_ERROR(tf_associations_broadcaster_closure_->Start());

  tf_associations_computation_closure_ =
      std::make_unique<intrinsic::PeriodicOperation>(
          [this]() {
            const absl::Status compute_status = ComputeAssociationsIfChanged();
            if (!compute_status.ok()) {
              LOG_EVERY_N_SEC(ERROR, 10.0)
                  << "ComputeAssociationsIfChanged() failed: "
                  << compute_status;
            }
          },
          options_.tf_associations_compute_interval);
  INTR_RETURN_IF_ERROR(tf_associations_computation_closure_->Start());

  return absl::OkStatus();
}

absl::Status WorldTfPublisher::PublishAssociations() {
  absl::MutexLock lock(&associations_mtx_);
  if (cached_associations_.has_value()) {
    // Publish periodically even if it hasn't changed to ensure snappy updates
    // and robustness against data loss and message drops.
    INTR_RETURN_IF_ERROR(
        tf_associations_publisher_->Publish(cached_associations_.value()));
  }

  return absl::OkStatus();
}

absl::Status WorldTfPublisher::ComputeAssociationsIfChanged() {
  WorldStorage* world_storage = GetWorldStorage();
  if (world_storage == nullptr) {
    return absl::OkStatus();
  }
  absl::StatusOr<std::shared_ptr<WorldAndMutex>> world_and_mutex =
      world_storage->GetWorld(options_.world_id);
  if (absl::IsNotFound(world_and_mutex.status())) {
    return absl::OkStatus();
  }
  INTR_RETURN_IF_ERROR(world_and_mutex.status());

  bool associations_changed = false;
  {
    std::shared_ptr<WorldAndMutex> world_ptr = world_and_mutex.value();

    absl::ReaderMutexLock lock(*world_ptr->mtx);
    absl::MutexLock assoc_lock(&associations_mtx_);

    // As an optimization, we only construct the TF associations message when
    // the world structure has changed (indicated by the world_structure_hash).
    // Constructing the associations message is more expensive than standard TF
    // publishing as it iterates over the entire tree.
    if (world_ptr->world_structure_hash != world_structure_hash_ ||
        !cached_associations_.has_value()) {
      world_structure_hash_ = world_ptr->world_structure_hash;
      const World& world = world_ptr->world;
      INTR_ASSIGN_OR_RETURN(cached_associations_,
                            world.ConstructTfAssociations());
      associations_changed = true;
    }
  }

  // Schedule publishes as soon as we have new TF associations to ensure updates
  // are reflected as soon as possible.
  if (associations_changed) {
    if (tf_associations_broadcaster_closure_) {
      tf_associations_broadcaster_closure_->RunNowNonBlocking();
    }
    if (tf_broadcaster_closure_) {
      tf_broadcaster_closure_->RunNowNonBlocking();
    }
  }
  return absl::OkStatus();
}

absl::Status WorldTfPublisher::UpdateAndPublishTf() {
  // Because WorldStorage is in intrinsic-base, when an application is not
  // running, there will not be a World instance. We need to
  // detect this case and return quickly, to avoid spamming the logs with
  // useless "could not find world" errors. We should release the worlds mutex
  // as quickly as possible, to avoid blocking other threads while we are
  // creating the TF message from the world.
  WorldStorage* world_storage = GetWorldStorage();
  if (world_storage == nullptr) {
    LOG_EVERY_N_SEC(WARNING, 10.0)
        << "World Storage is not ready yet, skipping TF publish";
    return absl::OkStatus();
  }
  absl::StatusOr<std::shared_ptr<WorldAndMutex>> world_and_mutex =
      world_storage->GetWorld(options_.world_id);
  if (absl::IsNotFound(world_and_mutex.status())) {
    LOG_EVERY_N_SEC(WARNING, 10.0) << "World '" << options_.world_id
                                   << "' is not ready yet, skipping TF publish";
    return absl::OkStatus();
  }
  INTR_RETURN_IF_ERROR(world_and_mutex.status());

  intrinsic_proto::TFMessage tf_message;
  {
    // To avoid replicating ECS queries for both updating TF and preparing
    // a TFMessage to publish, the generated TFMessage will be used to update
    // the TF buffer, because required work is identical in both steps.
    std::shared_ptr<WorldAndMutex> world_ptr = world_and_mutex.value();
    absl::ReaderMutexLock lock(*world_ptr->mtx);
    const World& world = world_ptr->world;
    // In order to release the lock as quickly as possible, the tf_message
    // returned by World::PopulateTfMessage() is copied into the outer scope,
    // to allow the Publish() call to happen after the lock is released.
    INTR_ASSIGN_OR_RETURN(tf_message, world.ConstructTfMessage());
    INTR_RETURN_IF_ERROR(world.UpdateTransformBuffer(tf_message));
  }
  if (echo_guard_ != nullptr) {
    INTR_RETURN_IF_ERROR(
        echo_guard_->FilterTfMessagePendingPublish(tf_message));
    INTR_RETURN_IF_ERROR(echo_guard_->RecordPublishedMessage(tf_message));
  }
  INTR_RETURN_IF_ERROR(tf_publisher_->Publish(tf_message));
  return absl::OkStatus();
}

WorldStorage* WorldTfPublisher::GetWorldStorage() {
  absl::MutexLock lock(world_storage_mtx_);
  if (world_storage_ == nullptr) {
    if (!world_storage_future_.valid()) {
      return nullptr;
    }
    world_storage_ = world_storage_future_.get();
  }
  return world_storage_;
}

}  // namespace intrinsic
