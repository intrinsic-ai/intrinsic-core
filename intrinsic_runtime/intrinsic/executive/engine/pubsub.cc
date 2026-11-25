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

#include "intrinsic/executive/engine/pubsub.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/executive/clips_cpp/value.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/status/return.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/util/thread/thread.h"
#include "third_party/imported/cpp_libraries/clock/clock.h"

ABSL_FLAG(bool, pubsub_publish, false, "Enables publishing via pubsub");
ABSL_FLAG(bool, clear_capture_results_kvstore, false,
          "Enables clearing the capture results in the KV store on operation "
          "reset/delete");

namespace intrinsic::executive {

namespace {
constexpr int kAsyncPublishQueueSize = 200;

// LINT.IfChange(capture_results_kv_store)
constexpr char kCaptureResultsPrefix[] = "capture_results";
// LINT.ThenChange(//intrinsic/platform/pubsub/fds.yaml:zenoh_router_config)

// Publish a proto to a topic. The function does not take ownership of the
// passed in proto id.
constexpr char kPubSubPublish[] = "pubsub-publish";

// TODO(b/379828886, b/397354650): Clears the capture_results KV store as
// currently the executive does not handle data flow via the KV store and there
// is no garbage collection. Otherwise left over images by skills that acquired
// capture results could be left over after an operation and would never be
// deleted.
constexpr char kPubSubClearKvCaptureResults[] =
    "pubsub-clear-kv-capture-results";

}  // namespace

absl::Status ClipsPublisher::Publish(google::protobuf::Any&& message) {
  last_publish_time_ = clock_.TimeNow();
  return publisher_.Publish(std::move(message));
}

ClipsPubSub::ClipsPubSub(clips::ProtobufManager* absl_nonnull proto_manager,
                         absl::Duration max_publisher_age,
                         util::Clock* absl_nonnull clock)
    : clock_(*clock),
      max_publisher_age_(max_publisher_age),
      proto_manager_(proto_manager) {
  // Only create this if actually needed, Zenoh initialization is expensive
  // (~500ms) and thus undesirable, e.g., in most tests.
  if (absl::GetFlag(FLAGS_pubsub_publish) ||
      absl::GetFlag(FLAGS_clear_capture_results_kvstore)) {
    pubsub_.emplace();
  }
}

absl::Status ClipsPubSub::Init(
    clips::EnvironmentFunctionFacade* absl_nonnull facade) {
  publish_queue_.emplace(kAsyncPublishQueueSize);
  // Setup reader thread that performs actual publishing from queue
  publish_worker_ =
      Thread(absl::bind_front(&ClipsPubSub::PublishThreadReader, this),
             std::ref(publish_queue_.value()));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kPubSubPublish, std::function([this](const std::string& topic_name,
                                           int64_t proto_id) -> clips::Symbol {
        INTR_ASSIGN_OR_RETURN(
            google::protobuf::Any proto_msg,
            proto_manager_->CastToAny(clips::ProtoMessageId(proto_id)),
            _.LogError().With(Return(clips::Symbol::False())));

        INTR_RETURN_IF_ERROR(PublishProto(topic_name, std::move(proto_msg)))
            .LogError()
            .With(Return(clips::Symbol::False()));

        return clips::Symbol::True();
      })));

  INTR_RETURN_IF_ERROR(facade->AddFunction(
      kPubSubClearKvCaptureResults, std::function([this]() -> clips::Symbol {
        if (!absl::GetFlag(FLAGS_clear_capture_results_kvstore)) {
          return clips::Symbol::True();
        }
        if (!pubsub_.has_value()) {
          LOG(ERROR) << "PubSub is not initialized. Cannot clear KV store.";
          return clips::Symbol::False();
        }
        INTR_ASSIGN_OR_RETURN(
            KeyValueStore kvstore,
            pubsub_->KeyValueStore(kCaptureResultsPrefix),
            _.LogError().With(Return(clips::Symbol::False())));

        LOG(INFO) << "Deleting all keys in KV Store: " << kCaptureResultsPrefix;
        INTR_RETURN_IF_ERROR(kvstore.Delete("**"))
            .LogError()
            .With(Return(clips::Symbol::False()));

        return clips::Symbol::True();
      })));

  return absl::OkStatus();
}

size_t ClipsPubSub::GetNumPublishers() const {
  absl::MutexLock lock(publishers_mutex_);
  return publishers_.size();
}

absl::Status ClipsPubSub::TearDown() {
  // Closing the queue causes the worker thread to stop.
  if (publish_queue_.has_value()) {
    publish_queue_->Close();
  }
  if (publish_worker_.joinable()) {
    publish_worker_.join();
  }
  return absl::OkStatus();
}

absl::Status ClipsPubSub::PublishProto(absl::string_view topic_name,
                                       google::protobuf::Any message) {
  if (!absl::GetFlag(FLAGS_pubsub_publish)) {
    return absl::OkStatus();
  }
  if (!publish_queue_.has_value()) {
    return absl::FailedPreconditionError(
        "Channel for publishing is not available");
  }

  return publish_queue_->Enqueue(PublishRequest(topic_name, std::move(message)),
                                 absl::ZeroDuration());
}

void ClipsPubSub::PublishThreadReader(ConcurrentQueue<PublishRequest>& queue) {
  while (true) {
    absl::StatusOr<PublishRequest> request =
        queue.Dequeue(absl::Milliseconds(500));
    if (absl::IsUnavailable(request.status())) {
      // Queue is empty and closed, stop this thread.
      return;
    }
    if (!request.ok()) {
      // Queue is empty, wait and try again.
      continue;
    }

    absl::MutexLock lock(publishers_mutex_);
    absl::Status publishers_status = CreateOrUpdatePublishers(request->topic);
    if (!publishers_status.ok()) {
      LOG_EVERY_N_SEC(ERROR, 0.5)
          << "Failed to update publishers when trying to publish to "
          << request->topic << ": " << publishers_status;
      continue;
    }

    absl::flat_hash_map<std::string, std::unique_ptr<ClipsPublisher>>::iterator
        it = publishers_.find(request->topic);
    if (it == publishers_.end()) {
      LOG_EVERY_N_SEC(ERROR, 0.5) << "Failed to find or create publisher for "
                                  << request->topic << ". Cannot publish.";
      continue;
    }
    LOG_EVERY_N_SEC(INFO, 60.0)
        << "Publishing (" << COUNTER << ") to " << request->topic << ": "
        << request->message.type_url();
    absl::Status publish_status =
        it->second->Publish(std::move(request->message));
    if (!publish_status.ok()) {
      LOG_EVERY_N_SEC(ERROR, 0.5) << "Failed to publish to " << request->topic
                                  << ": " << publish_status;
    }
  }
}

ClipsPubSub::PublishRequest::PublishRequest(std::string_view topic,
                                            google::protobuf::Any&& msg)
    : topic(topic), message(std::move(msg)) {}

absl::Status ClipsPubSub::CreateOrUpdatePublishers(absl::string_view topic_name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(publishers_mutex_) {
  std::vector<std::string> old_publishers;
  absl::Time lookup_time = clock_.TimeNow();
  bool topic_found = false;

  absl::erase_if(publishers_, [&topic_name, &lookup_time, &topic_found,
                               &old_publishers, this](const auto& item) {
    // Do not erase the publisher for the requested topic_name
    if (item.first == topic_name) {
      topic_found = true;
      return false;
    }
    const std::unique_ptr<ClipsPublisher>& publisher = item.second;
    if (lookup_time - publisher->LastPublishTime() > max_publisher_age_) {
      old_publishers.push_back(item.first);
      return true;
    }
    return false;
  });

  if (!old_publishers.empty()) {
    LOG(INFO) << "Removing old publishers: "
              << absl::StrJoin(old_publishers, ", ");
  }

  if (!topic_found && !topic_name.empty()) {
    if (!pubsub_.has_value()) {
      return absl::FailedPreconditionError(
          "PubSub is not initialized. Cannot create publisher.");
    }
    INTR_ASSIGN_OR_RETURN(
        intrinsic::Publisher publisher,
        pubsub_->CreatePublisher(topic_name, intrinsic::TopicConfig()));
    publishers_[topic_name] =
        std::make_unique<ClipsPublisher>(std::move(publisher), &clock_);
  }
  return absl::OkStatus();
}

}  // namespace intrinsic::executive
