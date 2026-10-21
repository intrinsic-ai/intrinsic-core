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

#ifndef INTRINSIC_EXECUTIVE_ENGINE_PUBSUB_H_
#define INTRINSIC_EXECUTIVE_ENGINE_PUBSUB_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/executive/clips_cpp/function_facade.h"
#include "intrinsic/executive/clips_cpp/protobuf.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/thread/concurrent_queue.h"
#include "intrinsic/util/thread/thread.h"
#include "third_party/imported/cpp_libraries/clock/clock.h"

namespace intrinsic::executive {

// A ClipsPublisher wraps a intrinsic::Publisher with its last_publish_time.
//
// The main point is to determine if this publisher is still considered
// actively publishing or if that can be removed.
class ClipsPublisher {
 public:
  explicit ClipsPublisher(intrinsic::Publisher&& publisher,
                          util::Clock* absl_nonnull clock)
      : publisher_(std::move(publisher)), clock_(*clock) {}

  ClipsPublisher(const ClipsPublisher&) = delete;
  ClipsPublisher& operator=(const ClipsPublisher&) = delete;
  ClipsPublisher(ClipsPublisher&&) = delete;
  ClipsPublisher& operator=(ClipsPublisher&&) = delete;

  absl::Status Publish(google::protobuf::Any&& message);
  absl::Time LastPublishTime() const { return last_publish_time_; }

 private:
  intrinsic::Publisher publisher_;
  absl::Time last_publish_time_;

  util::Clock& clock_;
};

// Manages pubsub publishers for CLIPS integrations.
//
// Besides providing a common entry point PublishProto that actually publishes a
// message this class also manages publishers. As these come from runtime
// defined user code there are not guarantees which publishers will have to run,
// so these cannot be predefined and must be created ad-hoc. Whenever new
// publishers might be created, publishers that have become too old will thus be
// removed to prevent incremental memory usage.
//
// This class is thread-safe.
class ClipsPubSub {
 public:
  static constexpr absl::Duration kDefaultMaxPublisherAge = absl::Seconds(60);

  explicit ClipsPubSub(
      clips::ProtobufManager* absl_nonnull proto_manager,
      absl::Duration max_publisher_age = kDefaultMaxPublisherAge,
      util::Clock* absl_nonnull clock = util::Clock::RealClock());

  ClipsPubSub(const ClipsPubSub&) = delete;
  ClipsPubSub& operator=(const ClipsPubSub&) = delete;
  ClipsPubSub(ClipsPubSub&&) = delete;
  ClipsPubSub& operator=(ClipsPubSub&&) = delete;

  // Must be called once after construction.
  // Do not call any other functions until Init has succeeded.
  // Creates publish thread and registers the pubsub functions with the CLIPS
  // environment.
  absl::Status Init(clips::EnvironmentFunctionFacade* absl_nonnull facade)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(facade->clips_mutex());

  // Must be called in the same parent thread as Init().
  absl::Status TearDown();

  // Publishes message under topic_name.
  // The message is stored in its serialized form and not duplicated, so that
  // neither message nor its associated pool need outlive this call.
  template <typename T>
    requires intrinsic::executive::clips::GeneratedProtoMessage<T>
  absl::Status PublishProto(absl::string_view topic_name, const T& message)
      ABSL_LOCKS_EXCLUDED(publishers_mutex_);
  absl::Status PublishProto(absl::string_view topic_name,
                            google::protobuf::Any message)
      ABSL_LOCKS_EXCLUDED(publishers_mutex_);

  size_t GetNumPublishers() const ABSL_LOCKS_EXCLUDED(publishers_mutex_);

 private:
  struct PublishRequest {
    std::string topic;
    google::protobuf::Any message;

    PublishRequest(std::string_view topic, google::protobuf::Any&& msg);
  };
  void PublishThreadReader(ConcurrentQueue<PublishRequest>& queue);

  // Updates the publishers, so that:
  // - publishers_ only containers ClipsPublishers that have recently published
  // - publishers contains a ClipsPublisher for topic_name
  absl::Status CreateOrUpdatePublishers(absl::string_view topic_name)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(publishers_mutex_);

  util::Clock& clock_;

  std::optional<intrinsic::PubSub> pubsub_;

  absl::flat_hash_map<std::string, std::unique_ptr<ClipsPublisher>> publishers_
      ABSL_GUARDED_BY(publishers_mutex_);
  mutable absl::Mutex publishers_mutex_;

  absl::Duration max_publisher_age_;

  clips::ProtobufManager* proto_manager_;  // externally owned

  Thread publish_worker_;
  std::optional<ConcurrentQueue<PublishRequest>> publish_queue_;
};

template <typename T>
  requires intrinsic::executive::clips::GeneratedProtoMessage<T>
absl::Status ClipsPubSub::PublishProto(absl::string_view topic_name,
                                       const T& message) {
  google::protobuf::Any packed_message;
  if (!packed_message.PackFrom(message)) {
    return absl::InternalError("Failed to pack proto message.");
  }
  return PublishProto(topic_name, std::move(packed_message));
}

}  // namespace intrinsic::executive

#endif  // INTRINSIC_EXECUTIVE_ENGINE_PUBSUB_H_
