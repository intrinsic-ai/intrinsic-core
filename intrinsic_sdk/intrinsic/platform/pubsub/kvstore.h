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

#ifndef INTRINSIC_PLATFORM_PUBSUB_KVSTORE_H_
#define INTRINSIC_PLATFORM_PUBSUB_KVSTORE_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_map.h"
#include "absl/flags/declare.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "intrinsic/platform/pubsub/pubsub_callbacks.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/platform/pubsub/topic_config.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"
#include "intrinsic/stats/scoped_span.h"  
#include "intrinsic/util/status/status_macros.h"

ABSL_DECLARE_FLAG(bool, use_replicated_kv_store);
ABSL_DECLARE_FLAG(std::string, admin_set_proxy_endpoint);

namespace intrinsic {

constexpr absl::Duration kDefaultGetTimeout = absl::Seconds(10);
constexpr absl::Duration kDefaultAdminCloudCopyTimeout = absl::Seconds(20);
constexpr absl::string_view kDefaultKeyPrefix = "kv_store";
constexpr absl::string_view kReplicationPrefix = "kv_store_repl";
constexpr absl::string_view kGlobalReplicationNamespace = "global";

using KeyValueCallback = std::function<void(
    absl::string_view key, std::unique_ptr<google::protobuf::Any> value)>;

// Callback invoked when the KeyValueCallback is called for all keys that match.
// Make sure to keep this callback lightweight.
using OnDoneCallback = std::function<void(absl::string_view key)>;

class KVQuery {
 public:
  explicit KVQuery(std::unique_ptr<imw_callback_functor_t> callback,
                   std::unique_ptr<imw_on_done_functor_t> on_done)
      : callback_(std::move(callback)),
        on_done_(std::move(on_done)),
        context_(
            std::make_unique<QueryContext>(callback_.get(), on_done_.get())) {}

  QueryContext* GetContext() { return context_.get(); }

 private:
  std::unique_ptr<imw_callback_functor_t> callback_;
  std::unique_ptr<imw_on_done_functor_t> on_done_;
  std::unique_ptr<QueryContext> context_;
};

class KeyValueStore {
 public:
  friend class PubSub;

  struct SetWithVerificationOptions {
    // Both verification modes avoid protobuf deserialization overhead by
    // comparing raw bytes.
    enum class VerificationMode {
      // Returns as soon as any populated value is read from the key, without
      // verifying its contents.
      //
      // Use this for performant initial writes where confirming that *some*
      // value was set is sufficient.
      //
      // WARNING:
      //   Does not verify contents. If the key already has a value, or another
      //   concurrent writer wrote to the key, this might falsely pass before
      //   the new write has actually propagated.
      //
      //   If you need to verify contents to ensure your value was set, use
      //   kHighConsistency instead.
      kFirstReply,

      // Blocks until the value is verified by reading it back and checking if
      // it matches the expected bytes.
      // Use this for updates where you need to guarantee that the key
      // converged to the exact written value.
      //
      // Also detects race conditions by checking if another process wrote a
      // different value while polling. The check is byte-exact on the
      // serialized payload, so a concurrent writer storing a semantically
      // equivalent message with a different encoding also counts as a
      // conflict.
      kHighConsistency,
    };

    VerificationMode mode = VerificationMode::kHighConsistency;

    // Budget for the verification polling loop, starting after the write has
    // been dispatched. Must be positive; SetWithVerification returns
    // InvalidArgumentError otherwise. kHighConsistency additionally performs a
    // read before the write, bounded by this timeout or 10 seconds, whichever
    // is shorter, so that read is not covered by this budget.
    absl::Duration timeout = absl::Seconds(30);
  };

  virtual ~KeyValueStore() = default;

  // Sets the value for the given key without waiting for verification
  // (fire-and-forget). A key can't include any of the following characters: /,
  // *, ?, #, [ and ].
  //
  // NOTE:
  // Returning absl::OkStatus() indicates that the write was successfully
  // dispatched/delegated to the underlying Zenoh transport, but Zenoh will
  // still need to perform the write and have it propagate. If you need to
  // guarantee and wait for the write to complete, use `SetWithVerification()`
  // instead.
  virtual absl::Status Set(absl::string_view key,
                           const google::protobuf::Any& value);

  // Sets the value for the given key without waiting for verification
  // (fire-and-forget). Templated overload for arbitrary Protobuf messages.
  //
  // NOTE:
  //   Returning absl::OkStatus() indicates that the write was successfully
  //   dispatched/delegated to the underlying Zenoh transport, but Zenoh will
  //   still need to perform the write and have it propagate. If you need to
  //   guarantee and wait for the write to complete, use `SetWithVerification()`
  //   instead.
  template <typename T>
  absl::Status Set(absl::string_view key, T&& value)
    requires(
        std::is_base_of_v<google::protobuf::Message, std::remove_cvref_t<T>> &&
        !std::is_same_v<google::protobuf::Any, std::remove_cvref_t<T>>)
  {
    google::protobuf::Any any;
    if (!any.PackFrom(std::forward<T>(value))) {
      return absl::InternalError(
          absl::StrCat("Failed to pack value for the key: ", key));
    }
    return Set(key, any);
  }

  // Sets the value for the given key with optional high consistency
  // verification.
  //
  // If `high_consistency` is true, this method delegates to
  // `SetWithVerification()` using `VerificationMode::kHighConsistency` and a
  // 30-second timeout. If false, it behaves as fire-and-forget `Set(key,
  // value)`.
  [[deprecated("Use SetWithVerification() instead.")]]
  virtual absl::Status Set(absl::string_view key,
                           const google::protobuf::Any& value,
                           std::optional<bool> high_consistency);

  // Sets the value for the given key with optional high consistency
  // verification. Templated overload for arbitrary Protobuf messages.
  template <typename T>
  [[deprecated("Use SetWithVerification() instead.")]]
  absl::Status Set(absl::string_view key, T&& value,
                   std::optional<bool> high_consistency)
    requires(
        std::is_base_of_v<google::protobuf::Message, std::remove_cvref_t<T>> &&
        !std::is_same_v<google::protobuf::Any, std::remove_cvref_t<T>>)
  {
    google::protobuf::Any any;
    if (!any.PackFrom(std::forward<T>(value))) {
      return absl::InternalError(
          absl::StrCat("Failed to pack value for the key: ", key));
    }
    return Set(key, any, high_consistency);
  }

  // Sets the value for the given key and blocks until verification completes
  // according to the specified `options` (see `SetWithVerificationOptions` for
  // available verification modes and their tradeoffs). A key can't include any
  // of the following characters: /, *, ?, #, [ and ].
  //
  // Returns absl::InvalidArgumentError if `options.timeout` is not positive.
  //
  // Returns absl::AbortedError if another process wrote a conflicting value
  // during verification (under kHighConsistency mode).
  virtual absl::Status SetWithVerification(
      absl::string_view key, const google::protobuf::Any& value,
      const SetWithVerificationOptions& options);

  // Sets the value for the given key and blocks until verification completes.
  // Templated overload for arbitrary Protobuf messages.
  template <typename T>
  absl::Status SetWithVerification(absl::string_view key, T&& value,
                                   const SetWithVerificationOptions& options)
    requires(
        std::is_base_of_v<google::protobuf::Message, std::remove_cvref_t<T>> &&
        !std::is_same_v<google::protobuf::Any, std::remove_cvref_t<T>>)
  {
    google::protobuf::Any any;
    if (!any.PackFrom(std::forward<T>(value))) {
      return absl::InternalError(
          absl::StrCat("Failed to pack value for the key: ", key));
    }
    return SetWithVerification(key, any, options);
  }

  template <typename T>
  absl::StatusOr<T> Get(absl::string_view key,
                        absl::Duration timeout = kDefaultGetTimeout)
    requires(std::is_base_of_v<google::protobuf::Message, T>)
  {
    INTR_ASSIGN_OR_RETURN(google::protobuf::Any any_value,
                          GetAny(key, timeout));
    if constexpr (std::is_same_v<google::protobuf::Any, T>) {
      return any_value;
    } else {
      return ExtractFromAny<T>(key, any_value);
    }
  }

  // For a given key and WildcardQueryConfig, the KeyValueCallback will be
  // invoked for each key that matches the expression. The caller is expected to
  // keep the Query object alive until the OnDoneCallback is called.
  absl::StatusOr<KVQuery> GetAll(
      absl::string_view key, KeyValueCallback callback,
      OnDoneCallback on_done = [](absl::string_view key) {});

  // Deletes the key from the KVStore.
  virtual absl::Status Delete(absl::string_view key);

  // Lists all keys in the non replicated KVStore. Returns an error if called on
  // replicated KVStore. Essentially lists keys in kv_store/**
  absl::StatusOr<std::vector<std::string>> ListAllKeys(
      absl::Duration timeout = kDefaultGetTimeout);

  // Lists all keys in the global cloud KVStore key space.
  // Essentially lists keys in kv_store_repl/global/**
  absl::StatusOr<std::vector<std::string>> ListAllGlobalKeys(
      absl::Duration timeout = kDefaultGetTimeout);

  // Lists all keys in the onprem replicated KVStore. Essentially lists keys in
  // kv_store_repl/<workcell_name>/**
  absl::StatusOr<std::vector<std::string>> ListAllOnpremKeys(
      absl::string_view workcell_name,
      absl::Duration timeout = kDefaultGetTimeout);

  // Use this method to copy local key-value pairs to the cloud key value
  // store. For eg, you can use this method to copy kv_store/<current_ipc>/key
  // to kv_store_repl/<destination_ipc>/key. To use this method, the you must be
  // running on a cluster with credentials that allow cloud ingress access. The
  // timeout is not enforced for the entire duration of the copy, but rather for
  // the call to the cloud server.
  absl::Status AdminCloudCopy(absl::string_view source_key,
                              absl::string_view target_key,
                              absl::Duration timeout);

  // Same as GetAll, but does not need a callback. The tradeoff is less control.
  absl::StatusOr<absl::flat_hash_map<std::string, google::protobuf::Any>>
  GetAllSynchronous(absl::string_view keyexpr, absl::Duration timeout);

  // Creates a key from an arbitrary number of strings, removing leading and
  // trailing slashes, and joining them with a slash delimiter.
  template <typename... Args>
  static std::string MakeKey(const Args&... args) {
    return MakeKeyImpl({absl::string_view(args)...});
  }

  // Creates a key from a vector of string views, removing leading and
  // trailing slashes, and joining them with a slash delimiter.
  //
  // Unlike MakeKey, this method is compatible with PyBind.
  static std::string MakeKeyFromVector(const std::vector<std::string>& parts);

  // Creates a subscription to changes in value of the specified key expression.
  //
  // Doesn't make any assumptions about the type of values stored in the KV
  // store. The calling code is responsible for checking their type.
  //
  // Parameters:
  // - kvstore - KV store to subscribe to.
  // - key_expression - key expression to subscribe to. It must not include
  //   the KV store's key prefix.
  // - config - subscription configuration.
  // - value_callback - callback that will be invoked when a key-value pair
  //   matching `key_expression` is updated. The updated value is wrapped into
  //   `google::protobuf::Any`. The callback code is responsible for extracting
  //   that value and checking its type.
  // - deletion_callback - callback that will be
  //   invoked when a key matching `key_expression` is deleted.
  absl::StatusOr<Subscription> CreateSubscription(
      absl::string_view key_expression, const TopicConfig& config,
      SubscriptionOkExpandedCallback<google::protobuf::Any> value_callback,
      DeletionCallback deletion_callback) const;

  // Creates a subscription to changes in value of the specified key expression.
  //
  // Assumes that all values matching the subscription's key expression have
  // the same type.
  //
  // Parameters:
  // - kvstore - KV store to subscribe to.
  // - key_expression - key expression to subscribe to. It must not include
  //   the KV store's key prefix.
  // - config - subscription configuration.
  // - exemplar - an empty proto of the same type as the values that match the
  //   `key_expression`.
  // - value_callback - callback that will be invoked when a key-value pair
  //   matching `key_expression` is updated.
  // - deletion_callback - callback that will be invoked when a key matching
  //   `key_expression` is deleted.
  // - error_callback - callback that will be invoked when type of the value
  //   that matches `key_expression` doesn't match `examplar`'s type.
  template <typename T>
  absl::StatusOr<Subscription> CreateSubscription(
      absl::string_view key_expression, const TopicConfig& config,
      const T& exemplar, SubscriptionOkExpandedCallback<T> value_callback,
      DeletionCallback deletion_callback,
      SubscriptionErrorExpandedCallback error_callback = {}) const {
    static_assert(std::is_base_of_v<google::protobuf::Message, T>,
                  "Protocol buffers are the only supported serialization "
                  "format for PubSub.");

    // This payload is shared between callbacks and may be read from multiple
    // threads. We need a shared_ptr here because a std::function must be
    // copyable.
    std::shared_ptr<T> shared_payload(exemplar.New());

    // The message callback is never copied. It is merely moved to this helper
    // lambda which is itself moved to the subscription class.
    auto unwrap_payload = [callback = std::move(value_callback),
                           error_callback = std::move(error_callback),
                           shared_payload = std::move(shared_payload)](
                              absl::string_view keyexpr,
                              const google::protobuf::Any& wrapped_payload) {
      // Create a local copy of the shared payload which we can safely
      // modify in different threads.
      std::unique_ptr<T> payload(shared_payload->New());
      if (!wrapped_payload.UnpackTo(payload.get())) {
        error_callback(keyexpr, absl::StrCat(wrapped_payload),
                       absl::InvalidArgumentError(absl::StrCat(
                           "Expected payload of type ", payload->GetTypeName(),
                           ", but got ", wrapped_payload.type_url())));
        return;
      }
      callback(keyexpr, *payload);
    };
    return CreateSubscription(key_expression, config, std::move(unwrap_payload),
                              std::move(deletion_callback));
  }

  // Returns a string that corresponds to the namespace needed for the
  // workcell's replicated namespace. Using this namespace prefix for keyexprs
  // will make the KVStore use replicated storage.
  //
  // This function will block until the workcell info is received or the timeout
  // expires. The calling code should check the returned value, and retry if
  // the returned status code is absl::StatusCode::kDeadlineExceeded.
  absl::StatusOr<std::string> GetWorkcellReplicationNamespace(
      absl::Duration timeout = kDefaultGetTimeout);

  // Returns a string that corresponds to the namespace needed for the
  // global replicated namespace. Using this namespace prefix for keyexprs
  // will make the KVStore use replicated storage available to all cloud
  // connected workcells within an organization.
  std::string GetGlobalReplicationNamespace() {
    return std::string(kGlobalReplicationNamespace);
  }

  // Returns the storage key prefix configured for this KVStore instance.
  absl::string_view key_prefix() const ABSL_ATTRIBUTE_LIFETIME_BOUND {
    return key_prefix_;
  }

 private:
  static std::string MakeKeyImpl(
      std::initializer_list<absl::string_view> parts);

  absl::StatusOr<std::vector<std::string>> ExecuteList(
      absl::string_view keyexpr, absl::Duration timeout);

 protected:
  explicit KeyValueStore(std::optional<std::string> prefix_override);

  // Returns the value for the given key, wrapped into a google::protobuf::Any.
  // The KV store's key prefix is added to the key before fetching the value.
  virtual absl::StatusOr<google::protobuf::Any> GetAny(absl::string_view key,
                                                       absl::Duration timeout);

 private:
  // Returns the raw serialized bytes for the given key.
  // The key is processed as is, i.e. no prefixes are added to it.
  //
  // This method is private because the raw keys contain prefixes not known
  // to the client code, such as `kv_store` or `kv_store_repl`.
  absl::StatusOr<std::string> GetRawWithRawKey(const std::string& raw_key,
                                               absl::Duration timeout);

  // Returns the value for the given key, wrapped into a google::protobuf::Any.
  // The key is processed as is, i.e. no prefixes are added to it.
  //
  // This method is private because the raw keys contain prefixes not known
  // to the client code, such as `kv_store` or `kv_store_repl`.
  absl::StatusOr<google::protobuf::Any> GetAnyWithRawKey(
      const std::string& raw_key, absl::Duration timeout);

  // Polls the KVStore until the first non-empty query reply is received for
  // `key`, skipping content verification. Wait loops use exponential backoff,
  // failing if the operation takes longer than `timeout`.
  absl::Status VerifyFirstReply(const std::string& prefixed_name,
                                absl::string_view key, absl::Duration timeout);

  // Polls the KVStore until the specified `key` converges to the given
  // `serialized_value` using size and raw-byte comparison.
  // Wait loops use exponential backoff, failing if the operation takes longer
  // than `timeout`.
  //
  // Aborts if the value changes to a value other than the newly-written
  // `serialized_value`, detecting race conditions with other writers (using
  // `initial_state` to know what value the key started with).
  absl::Status VerifyHighConsistency(
      const std::string& prefixed_name, absl::string_view key,
      absl::string_view serialized_value,
      std::optional<absl::string_view> initial_state, absl::Duration timeout);

  template <typename T>
  absl::StatusOr<T> ExtractFromAny(absl::string_view key,
                                   const google::protobuf::Any& any_value)
    requires(std::is_base_of_v<google::protobuf::Message, T>)
  {

    const stats::ScopedSpan wait_span("KVStoreClient::ExtractFromAny");

    T value;
    if (!any_value.UnpackTo(&value)) {
      return absl::InternalError(
          absl::StrCat("Failed to unpack value for the key: ", key));
    }
    return value;
  }

  static void StripSlashesAndAppend(std::string& result,
                                    absl::string_view part);

  std::string key_prefix_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_KVSTORE_H_
