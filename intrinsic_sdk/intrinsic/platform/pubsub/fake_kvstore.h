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

#ifndef INTRINSIC_PLATFORM_PUBSUB_FAKE_KVSTORE_H_
#define INTRINSIC_PLATFORM_PUBSUB_FAKE_KVSTORE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/pubsub_callbacks.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/platform/pubsub/topic_config.h"

namespace intrinsic {

// A fake implementation of KeyValueStore for use in unit tests.
//
// This class stores key-value pairs in an in-memory `absl::flat_hash_map`.
// It avoids dependencies on Zenoh or real PubSub infrastructure, making it
// fast and suitable for testing components that require a KeyValueStore.
//
// Example usage:
//   std::unique_ptr<KeyValueStore> kv_store =
//    std::make_unique<FakeKeyValueStore>();
//   kv_store->Set("my_key", my_proto_value);
//   // Pass kv_store to the class under test.
class FakeKeyValueStore : public KeyValueStore {
 public:
  FakeKeyValueStore();
  explicit FakeKeyValueStore(std::optional<std::string> prefix_override);
  ~FakeKeyValueStore() override = default;

  using KeyValueStore::CreateRawSubscription;
  using KeyValueStore::CreateSubscription;
  using KeyValueStore::GetAll;
  using KeyValueStore::GetAllRaw;
  using KeyValueStore::GetRaw;
  using KeyValueStore::Set;
  using KeyValueStore::SetRaw;
  using KeyValueStore::SetRawWithVerification;
  using KeyValueStore::SetWithVerification;

  // Stores the key-value pair in memory.
  absl::Status Set(absl::string_view key,
                   const google::protobuf::Any& value) override;

  // Stores the key-value pair in memory, satisfying verification immediately.
  absl::Status SetWithVerification(
      absl::string_view key, const google::protobuf::Any& value,
      const SetWithVerificationOptions& options) override;

  absl::Status SetRaw(absl::string_view key, ImmutableBuffer value) override;

  absl::Status SetRawWithVerification(
      absl::string_view key, ImmutableBuffer value,
      const SetWithVerificationOptions& options) override;

  absl::StatusOr<ImmutableBuffer> GetRaw(absl::string_view key,
                                         absl::Duration timeout) override;

  absl::StatusOr<KVQuery> GetAll(absl::string_view key,
                                 KeyValueCallback callback,
                                 OnDoneCallback on_done) override;

  absl::StatusOr<KVQuery> GetAllRaw(absl::string_view keyexpr,
                                    RawKeyValueCallback callback,
                                    OnDoneCallback on_done,
                                    RawQueryErrorCallback on_error) override;

  absl::Status Delete(absl::string_view key) override;

  absl::StatusOr<Subscription> CreateSubscription(
      absl::string_view key_expression, const TopicConfig& config,
      SubscriptionOkExpandedCallback<google::protobuf::Any> value_callback,
      DeletionCallback deletion_callback) const override;

  absl::StatusOr<Subscription> CreateRawSubscription(
      absl::string_view key_expression, const TopicConfig& config,
      SubscriptionOkExpandedCallback<ImmutableBuffer> value_callback,
      DeletionCallback deletion_callback) const override;

 protected:
  absl::StatusOr<google::protobuf::Any> GetAny(absl::string_view key,
                                               absl::Duration timeout) override;

  absl::StatusOr<std::vector<std::string>> ExecuteList(
      absl::string_view keyexpr, absl::Duration timeout) override;

  absl::StatusOr<KVQuery> ExecuteGetAll(
      absl::string_view keyexpr, KeyValueCallback callback,
      OnDoneCallback on_done, std::optional<absl::Duration> timeout) override;

  absl::StatusOr<KVQuery> ExecuteGetAllRaw(
      absl::string_view keyexpr, RawKeyValueCallback callback,
      OnDoneCallback on_done, RawQueryErrorCallback on_error,
      std::optional<absl::Duration> timeout) override;

 private:
  using SubscriptionValueCallback =
      std::variant<SubscriptionOkExpandedCallback<google::protobuf::Any>,
                   SubscriptionOkExpandedCallback<ImmutableBuffer>>;

  struct SubscriptionRecord {
    std::string key_expression;
    SubscriptionValueCallback callback;
    DeletionCallback deletion_callback;
  };

  struct SubscriptionRegistry {
    absl::Mutex mutex;
    uint64_t next_id ABSL_GUARDED_BY(mutex) = 1;
    absl::flat_hash_map<uint64_t, std::unique_ptr<SubscriptionRecord>>
        subscriptions ABSL_GUARDED_BY(mutex);
    absl::flat_hash_map<std::pair<uint64_t, std::thread::id>, int>
        active_callbacks ABSL_GUARDED_BY(mutex);
  };

  template <typename CallbackT, typename ExtractCallbackFn,
            typename DispatchCallbackFn>
  void DispatchMatchingSubscriptions(
      absl::string_view key, ExtractCallbackFn extract_callback,
      DispatchCallbackFn dispatch_callback) const;

  template <typename T>
  void NotifySubscribers(absl::string_view key, const T& value);

  template <typename T>
  absl::StatusOr<T> GetTyped(absl::string_view key, absl::Duration timeout,
                             absl::string_view mismatch_error);

  template <typename T, typename CallbackFn, typename EmitFn>
  absl::StatusOr<KVQuery> CollectMatches(absl::string_view keyexpr,
                                         CallbackFn callback, EmitFn emit,
                                         OnDoneCallback on_done);

  absl::StatusOr<Subscription> RegisterSubscription(
      absl::string_view key_expression,
      SubscriptionValueCallback value_callback,
      DeletionCallback deletion_callback) const;

  absl::Mutex mutex_;
  absl::flat_hash_map<std::string,
                      std::variant<google::protobuf::Any, ImmutableBuffer>>
      store_ ABSL_GUARDED_BY(mutex_);
  std::shared_ptr<SubscriptionRegistry> subscriptions_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_FAKE_KVSTORE_H_
