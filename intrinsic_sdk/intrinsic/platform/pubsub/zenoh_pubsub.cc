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

#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"
#include "intrinsic/platform/pubsub/adapters/pubsub.pb.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/liveliness_query.h"
#include "intrinsic/platform/pubsub/liveliness_subscription.h"
#include "intrinsic/platform/pubsub/liveliness_subscription_data.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/pubsub_callbacks.h"
#include "intrinsic/platform/pubsub/queryable.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/platform/pubsub/topic_config.h"
#include "intrinsic/platform/pubsub/zenoh_publisher_data.h"
#include "intrinsic/platform/pubsub/zenoh_pubsub_data.h"
#include "intrinsic/platform/pubsub/zenoh_subscription_data.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {

constexpr char kIntrospectionTopicPrefix[] = "in/_introspection/";

PubSub::PubSub() : data_(std::make_shared<PubSubData>()) {}

PubSub::PubSub(absl::string_view participant_name)
    : data_(std::make_shared<PubSubData>()) {}

PubSub::PubSub(absl::string_view participant_name, absl::string_view config)
    : data_(std::make_shared<PubSubData>(config)) {}

PubSub::~PubSub() = default;

void PubSub::DestroySessionWhenUnused() {
  Zenoh().imw_destroy_session_when_unused();
}

absl::StatusOr<Publisher> PubSub::CreatePublisher(
    absl::string_view topic_name, const TopicConfig& config) const {
  auto prefixed_name = ZenohHandle::add_topic_prefix(topic_name);
  if (!prefixed_name.ok()) {
    return prefixed_name.status();
  }

  imw_ret_t ret = Zenoh().imw_create_publisher(
      prefixed_name->c_str(),
      intrinsic::PubSubQoSToZenohQos(config.topic_qos).c_str());

  if (ret == IMW_ERROR) {
    return absl::InternalError("Error creating a publisher");
  }
  auto publisher_data = std::make_unique<PublisherData>();
  publisher_data->prefixed_name = *prefixed_name;
  return Publisher(topic_name, std::move(publisher_data));
}

absl::StatusOr<Subscription> PubSub::CreateSubscription(
    absl::string_view topic_name, const TopicConfig& config,
    SubscriptionOkCallback<intrinsic_proto::pubsub::PubSubPacket> msg_callback)
    const {
  auto prefixed_name = ZenohHandle::add_topic_prefix(topic_name);
  if (!prefixed_name.ok()) {
    return prefixed_name.status();
  }

  auto subscription_data = std::make_unique<SubscriptionData>();
  subscription_data->prefixed_name = *prefixed_name;
  auto callback = std::make_unique<imw_callback_functor_t>(
      [msg_callback](const char* keyexpr, const void* blob,
                     const size_t blob_len) {
        // Don't attempt to deserialize the introspection data
        // since it's JSON and doesn't need to be logged anyway;
        // it will be parsed and captured by Prometheus separately
        if (absl::StartsWith(keyexpr, kIntrospectionTopicPrefix)) return;

        intrinsic_proto::pubsub::PubSubPacket msg;
        bool success = msg.ParseFromArray(blob, blob_len);
        if (!success) {
          LOG_EVERY_N(ERROR, 1)
              << absl::StrFormat("Deserializing message failed. Topic: ")
              << keyexpr;
          return;
        }
        msg_callback(msg);
      });
  subscription_data->callback_functor = std::move(callback);

  imw_ret_t ret = Zenoh().imw_create_subscription(
      prefixed_name->c_str(), zenoh_static_callback,
      intrinsic::PubSubQoSToZenohQos(config.topic_qos).c_str(),
      subscription_data->callback_functor.get());
  if (ret != IMW_OK) {
    return absl::InternalError("Error creating a subscription");
  }
  return Subscription(topic_name, std::move(subscription_data));
}

// TODO(vivekbagade@) Merge SubscriptionOkCallback with
// SubscriptionOkExpandedCallback.
absl::StatusOr<Subscription> PubSub::CreateSubscription(
    absl::string_view topic_name, const TopicConfig& config,
    SubscriptionOkExpandedCallback<intrinsic_proto::pubsub::PubSubPacket>
        msg_callback) const {
  auto prefixed_name = ZenohHandle::add_topic_prefix(topic_name);
  if (!prefixed_name.ok()) {
    return prefixed_name.status();
  }

  auto subscription_data = std::make_unique<SubscriptionData>();
  subscription_data->prefixed_name = *prefixed_name;
  auto callback = std::make_unique<imw_callback_functor_t>(
      [msg_callback](const char* keyexpr, const void* blob,
                     const size_t blob_len) {
        absl::string_view topic_str(keyexpr);
        if (absl::StartsWith(topic_str, kIntrospectionTopicPrefix)) return;

        intrinsic_proto::pubsub::PubSubPacket msg;
        bool success = msg.ParseFromArray(blob, blob_len);
        if (!success) {
          LOG_EVERY_N(ERROR, 1) << absl::StrFormat(
              "Deserializing message failed. Topic: %s", keyexpr);
          return;
        }
        auto topic_name = ZenohHandle::remove_topic_prefix(keyexpr);
        if (!topic_name.ok()) {
          LOG_EVERY_N(ERROR, 1) << "Topic name error: " << topic_name.status();
          return;
        }
        msg_callback(*topic_name, msg);
      });
  subscription_data->callback_functor = std::move(callback);

  imw_ret_t ret = Zenoh().imw_create_subscription(
      prefixed_name->c_str(), zenoh_static_callback,
      intrinsic::PubSubQoSToZenohQos(config.topic_qos).c_str(),
      subscription_data->callback_functor.get());
  if (ret != IMW_OK) {
    return absl::InternalError("Error creating a subscription");
  }
  return Subscription(topic_name, std::move(subscription_data));
}

absl::StatusOr<LivelinessSubscription> PubSub::CreateLivelinessSubscription(
    absl::string_view keyexpr, bool notify_about_existing_tokens,
    LivelinessCallback liveliness_callback) const {
  std::string keyexpr_str = std::string(keyexpr);

  auto subscription_data = std::make_unique<LivelinessSubscriptionData>();
  subscription_data->key_expression = keyexpr_str;
  auto callback = std::make_unique<imw_liveliness_callback_functor_t>(
      [liveliness_callback](const char* keyexpr, bool alive) {
        liveliness_callback(keyexpr, alive);
      });
  subscription_data->callback_functor = std::move(callback);

  imw_ret_t ret = Zenoh().imw_create_liveliness_subscription(
      keyexpr_str.c_str(), zenoh_static_liveliness_callback,
      notify_about_existing_tokens, subscription_data->callback_functor.get());
  if (ret != IMW_OK) {
    return absl::InternalError("Error creating a subscription");
  }
  return LivelinessSubscription(keyexpr, std::move(subscription_data));
}

absl::Status PubSub::DeclareLivelinessToken(absl::string_view keyexpr) {
  imw_ret_t ret =
      Zenoh().imw_declare_liveliness_token(std::string(keyexpr).c_str());
  if (ret != IMW_OK) {
    return absl::InternalError("Error declaring a liveliness token");
  }
  return absl::OkStatus();
}

absl::Status PubSub::DropLivelinessToken(absl::string_view keyexpr) {
  imw_ret_t ret =
      Zenoh().imw_drop_liveliness_token(std::string(keyexpr).c_str());
  if (ret != IMW_OK) {
    return absl::InternalError("Error dropping a liveliness token");
  }
  return absl::OkStatus();
}

absl::StatusOr<LivelinessQuery> PubSub::LivelinessGet(
    absl::string_view keyexpr, LivelinessGetCallback callback,
    LivelinessGetOnDoneCallback on_done) {
  LivelinessQuery query(std::move(callback), std::move(on_done));
  std::string keyexpr_str(keyexpr);
  LOG(INFO) << "[PubSub::LivelinessGet] Calling imw_liveliness_get";
  imw_ret_t ret = Zenoh().imw_liveliness_get(
      keyexpr_str.c_str(), zenoh_static_liveliness_get_callback,
      zenoh_static_liveliness_get_on_done_callback, query.GetContext());
  LOG(INFO) << "[PubSub::LivelinessGet] imw_liveliness_get returned " << ret;

  if (ret != IMW_OK) {
    return absl::InternalError("Internal error in LivelinessGet");
  }
  return query;
}

absl::StatusOr<std::vector<std::string>> PubSub::LivelinessGetAllSynchronous(
    absl::string_view keyexpr) {
  std::vector<std::string> result;
  absl::CondVar cv;
  absl::Mutex mutex;
  bool done_called = false;

  LivelinessGetCallback callback = [&result, &mutex](absl::string_view key) {
    absl::MutexLock lock(&mutex);
    result.push_back(std::string(key));
  };

  LivelinessGetOnDoneCallback on_done = [&done_called, &cv,
                                         &mutex](absl::string_view key) {
    absl::MutexLock lock(&mutex);
    done_called = true;
    cv.Signal();
  };

  absl::StatusOr<LivelinessQuery> query =
      LivelinessGet(keyexpr, callback, on_done);
  if (!query.ok()) {
    LOG(ERROR) << "LivelinessGet returned an error: "
               << query.status().message();
    return query.status();
  }

  LOG(INFO) << "[LivelinessGetAllSynchronous] Waiting for on_done to be called";
  WaitForLivelinessTokens([&mutex, &cv, &done_called]() {
    // Timeout is enforced by Zenoh. So, we are using a condition variable here
    // instead of an absl::Notification and its WaitForNotificationWithTimeout
    // method. Use of WaitForNotificationWithTimeout may lead to race
    // conditions (the notification's timeout may expire before Zenoh calls
    // on_done_functor).
    absl::MutexLock lock(&mutex);
    while (!done_called) {
      cv.Wait(&mutex);
    }
  });

  LOG(INFO) << "[LivelinessGetAllSynchronous] Done waiting";
  return result;
}

bool PubSub::KeyexprIsCanon(absl::string_view keyexpr) const {
  const auto prefixed_keyexpr = ZenohHandle::add_topic_prefix(keyexpr);
  if (!prefixed_keyexpr.ok()) return false;
  return Zenoh().imw_keyexpr_is_canon(prefixed_keyexpr->c_str()) == 0;
}

absl::StatusOr<bool> PubSub::KeyexprIntersects(absl::string_view left,
                                               absl::string_view right) const {
  INTR_ASSIGN_OR_RETURN(const std::string prefixed_left,
                        ZenohHandle::add_topic_prefix(left));
  INTR_ASSIGN_OR_RETURN(const std::string prefixed_right,
                        ZenohHandle::add_topic_prefix(right));
  const int result = Zenoh().imw_keyexpr_intersects(prefixed_left.c_str(),
                                                    prefixed_right.c_str());
  switch (result) {
    case 0:
      return true;
    case 1:
      return false;
    default:
      return absl::InvalidArgumentError("A key expression is invalid");
  }
}

absl::StatusOr<bool> PubSub::KeyexprIncludes(absl::string_view left,
                                             absl::string_view right) const {
  INTR_ASSIGN_OR_RETURN(const std::string prefixed_left,
                        ZenohHandle::add_topic_prefix(left));
  INTR_ASSIGN_OR_RETURN(const std::string prefixed_right,
                        ZenohHandle::add_topic_prefix(right));
  const int result = Zenoh().imw_keyexpr_includes(prefixed_left.c_str(),
                                                  prefixed_right.c_str());
  switch (result) {
    case 0:
      return true;
    case 1:
      return false;
    default:
      return absl::InvalidArgumentError("A key expression is invalid");
  }
}

absl::StatusOr<intrinsic::KeyValueStore> PubSub::KeyValueStore(
    std::optional<std::string> prefix_override) const {
  return intrinsic::KeyValueStore(prefix_override);
}

namespace {
struct GetData {
  absl::Notification notification;
  absl::Mutex responses_mutex;
  struct Response {
    std::string key;
    intrinsic_proto::pubsub::PubSubQueryResponse proto;
  };
  absl::StatusOr<std::vector<Response>> responses
      ABSL_GUARDED_BY(responses_mutex) = std::vector<Response>{};
};

void GetCallbackFn(const char* key, const void* response_bytes,
                   const size_t response_bytes_len, void* user_context) {
  GetData* query_data = static_cast<GetData*>(user_context);
  absl::MutexLock lock(&query_data->responses_mutex);
  if (!query_data->responses.ok()) {
    // There was already an error, return immediately
    return;
  }
  std::string_view response_str(static_cast<const char*>(response_bytes),
                                response_bytes_len);
  intrinsic_proto::pubsub::PubSubQueryResponse response_packet;
  if (!response_packet.ParseFromString(response_str)) {
    query_data->responses =
        absl::InvalidArgumentError("Failed to parse response packet");
  }
  query_data->responses->push_back(
      {.key = key, .proto = std::move(response_packet)});
}

void GetOnDoneCallbackFn(const char* key, void* user_context) {
  GetData* query_data = static_cast<GetData*>(user_context);
  query_data->notification.Notify();
}

}  // namespace

bool PubSub::SupportsQueryables() const { return true; }

absl::StatusOr<Queryable> PubSub::CreateQueryableImpl(
    absl::string_view key, internal::GeneralQueryableCallback callback) {
  return Queryable::Create(key, callback);
}

absl::StatusOr<intrinsic_proto::pubsub::PubSubQueryResponse> PubSub::GetOneImpl(
    absl::string_view key,
    const intrinsic_proto::pubsub::PubSubQueryRequest& request,
    const QueryOptions& options) {
  std::string serialized_request = request.SerializeAsString();
  GetData query_data;
  imw_query_options_t query_options;
  if (options.timeout.has_value()) {
    query_options.timeout_ms = *options.timeout / absl::Milliseconds(1);
  }
  if (Zenoh().imw_query(key.data(), &GetCallbackFn, &GetOnDoneCallbackFn,
                        serialized_request.c_str(), serialized_request.size(),
                        &query_data, &query_options) != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Executing query for key '%s' failed", key));
  }

  query_data.notification.WaitForNotification();

  absl::MutexLock lock(&query_data.responses_mutex);
  if (!query_data.responses.ok()) {
    return query_data.responses.status();
  }
  if (query_data.responses->empty()) {
    return absl::DeadlineExceededError("Get operation timed out");
  }

  if (query_data.responses->empty()) {
    return absl::NotFoundError(
        absl::StrFormat("When calling GetOne for queryable '%s' received %d "
                        "no results",
                        key, query_data.responses->size()));
  }

  if (query_data.responses->size() > 1) {
    return absl::FailedPreconditionError(
        absl::StrFormat("When calling GetOne for queryable '%s' received %d "
                        "results, expected exactly one",
                        key, query_data.responses->size()));
  }
  return std::move(query_data.responses->at(0).proto);
}

absl::StatusOr<std::vector<intrinsic_proto::pubsub::PubSubQueryResponse>>
PubSub::GetImpl(absl::string_view key,
                const intrinsic_proto::pubsub::PubSubQueryRequest& request,
                const QueryOptions& options) {
  std::string serialized_request = request.SerializeAsString();
  GetData query_data;
  imw_query_options_t query_options;
  if (options.timeout.has_value()) {
    query_options.timeout_ms = *options.timeout / absl::Milliseconds(1);
  }
  if (Zenoh().imw_query(key.data(), &GetCallbackFn, &GetOnDoneCallbackFn,
                        serialized_request.c_str(), serialized_request.size(),
                        &query_data, &query_options) != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Executing query for key '%s' failed", key));
  }

  query_data.notification.WaitForNotification();

  absl::MutexLock lock(&query_data.responses_mutex);
  if (!query_data.responses.ok()) {
    return query_data.responses.status();
  }

  std::vector<intrinsic_proto::pubsub::PubSubQueryResponse> results;
  for (auto response : *query_data.responses) {
    results.push_back(std::move(response.proto));
  }
  return results;
}

}  // namespace intrinsic
