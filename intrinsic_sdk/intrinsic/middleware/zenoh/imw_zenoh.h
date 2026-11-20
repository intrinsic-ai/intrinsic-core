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

#ifndef MIDDLEWARE_ZENOH_IMW_ZENOH_H_
#define MIDDLEWARE_ZENOH_IMW_ZENOH_H_

#include <list>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "absl/base/internal/sysinfo.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_liveliness_subscription.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_liveliness_token.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_publisher.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_query_context.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_queryable.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_subscription.h"
#include "nlohmann/json.hpp"
#include "zenoh.h"  // NOLINT(build/include_subdir)

namespace intrinsic {

class IMWZenoh {
  friend class IMWZenohTest;

 public:
  static absl::Mutex init_fini_mutex_;
  static constexpr absl::Duration kDefaultQueryTimeout = absl::Seconds(10);
  static constexpr absl::Duration kQueryGcGrace = absl::Seconds(30);

  IMWZenoh();

  // This class is intended to be a singleton, so the copy and move
  // constructors are removed.
  IMWZenoh(const IMWZenoh&) = delete;
  IMWZenoh& operator=(const IMWZenoh&) = delete;

  ~IMWZenoh();

  imw_ret_t create_session(const char* config);
  imw_ret_t destroy_session();

  imw_ret_t create_publisher(const char* keyexpr, const char* qos);
  imw_ret_t destroy_publisher(const char* keyexpr);
  imw_ret_t publish(const char* keyexpr, const void* bytes,
                    const size_t bytes_len);
  imw_ret_t publisher_has_matching_subscribers(const char* keyexpr,
                                               bool* has_matching);

  imw_ret_t create_subscription(const char* keyexpr,
                                imw_subscription_callback_fn* callback,
                                const char* qos, void* user_context);
  imw_ret_t create_message_subscription(
      const char* keyexpr, imw_subscription_message_callback_fn* callback,
      const char* qos, void* user_context);

  imw_ret_t destroy_subscription(const char* keyexpr,
                                 imw_subscription_callback_fn* callback,
                                 const void* user_context);
  imw_ret_t destroy_message_subscription(
      const char* keyexpr, imw_subscription_message_callback_fn* callback,
      const void* user_context);

  imw_ret_t query(const char* keyexpr, imw_query_callback_fn* callback,
                  imw_query_on_done_callback_fn* on_done,
                  const void* query_payload, const size_t query_payload_len,
                  void* user_context, imw_query_options_t* options);
  imw_ret_t query_messages(const char* keyexpr,
                           imw_query_message_callback_fn* callback,
                           imw_query_on_done_callback_fn* on_done,
                           const void* query_payload,
                           const size_t query_payload_len, void* user_context,
                           imw_query_options_t* options);

  imw_ret_t create_queryable(const char* keyexpr,
                             imw_queryable_callback_fn* callback,
                             void* user_context,
                             imw_queryable_options_t* options);

  imw_ret_t destroy_queryable(const char* keyexpr,
                              imw_queryable_callback_fn* callback,
                              void* user_context);

  imw_ret_t queryable_reply(
      const void* query_context, const char* keyexpr, const void* reply_bytes,
      const size_t reply_bytes_len,
      imw_encoding_t encoding = IMW_ENCODING_VARINT_PREFIXED_BYTES);

  imw_ret_t set(const char* keyexpr, const void* bytes, const size_t bytes_len,
                imw_encoding_t encoding = IMW_ENCODING_VARINT_PREFIXED_BYTES);
  imw_ret_t set_with_encoding_and_deleter(const char* keyexpr,
                                          const void* bytes,
                                          const size_t bytes_len,
                                          imw_encoding_t encoding,
                                          imw_payload_deleter_fn* deleter,
                                          void* deleter_context);

  imw_ret_t delete_keyexpr(const char* keyexpr);

  imw_ret_t create_liveliness_subscription(const char* keyexpr,
                                           imw_liveliness_callback_fn* callback,
                                           bool notify_about_existing_tokens,
                                           void* user_context);
  imw_ret_t destroy_liveliness_subscription(
      const char* keyexpr, imw_liveliness_callback_fn* callback,
      const void* user_context);
  imw_ret_t declare_liveliness_token(const char* keyexpr);
  imw_ret_t drop_liveliness_token(const char* keyexpr);
  imw_ret_t liveliness_get(const char* keyexpr,
                           imw_liveliness_get_callback_fn* callback,
                           imw_liveliness_get_on_done_callback_fn* on_done,
                           void* user_context);

  static void static_data_callback(z_loaned_sample_t* sample, void* arg);
  static void static_closure_drop(void* context);

  static void static_queryable_callback(z_loaned_query_t* query, void* arg);
  static void static_queryable_drop(void* context);

  static void static_query_callback(z_loaned_reply_t* reply, void* arg);
  static void static_query_drop(void* context);

  static int keyexpr_includes(const char* left, const char* right);
  static int keyexpr_intersects(const char* left, const char* right);
  static int keyexpr_is_canon(const char* keyexpr);

  static void static_liveliness_callback(z_loaned_sample_t* sample, void* arg);
  static void static_liveliness_closure_drop(void* context);
  static void static_liveliness_get_callback(z_loaned_reply_t* reply,
                                             void* untyped_context);
  static void static_liveliness_get_drop(void* untyped_context);

  static const char* const version();

 private:
  struct LivelinessCallbackContext {
    std::weak_ptr<IMWZenohLivelinessSubscription> subscription;
  };
  imw_ret_t create_subscription_internal(
      const char* keyexpr, imw_subscription_callback_fn* legacy_callback,
      imw_subscription_message_callback_fn* message_callback, const char* qos,
      void* user_context);
  imw_ret_t destroy_subscription_internal(
      const char* keyexpr, imw_subscription_callback_fn* legacy_callback,
      imw_subscription_message_callback_fn* message_callback,
      const void* user_context);
  imw_ret_t query_internal(const IMWZenohQueryParams& params,
                           const void* query_payload,
                           const size_t query_payload_len);

  static void data_callback(IMWZenohSubscription& sub,
                            const z_loaned_sample_t* sample);
  void queryable_callback(const std::string& queryable_keyexpr,
                          const z_loaned_query_t* sample);
  static void query_callback(const char* keyexpr,
                             imw_query_callback_fn* user_callback,
                             imw_query_message_callback_fn* message_callback,
                             z_loaned_reply_t* reply, void* user_context,
                             const imw_query_options_t* options);
  void liveliness_get_callback(z_loaned_reply_t* reply,
                               imw_liveliness_get_callback_fn* callback,
                               void* untyped_context);

  // Creates any pending publishers and destroys publishers marked for deletion.
  // Returns the publisher matching the given keyexpr if one exists, otherwise
  // nullptr.
  std::shared_ptr<IMWZenohPublisher>
  resolve_pending_publishers_and_get_matching(const std::string& keyexpr)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(publishers_mutex_)
          ABSL_LOCKS_EXCLUDED(new_publishers_mutex_);

  void destroy_publishers_marked_for_deletion()
      ABSL_SHARED_LOCKS_REQUIRED(publishers_mutex_);

  void destroy_empty_subscriptions() ABSL_LOCKS_EXCLUDED(subscriptions_mutex_) {
    std::vector<std::shared_ptr<IMWZenohSubscription>> subs_to_undeclare;
    {
      absl::MutexLock lock(&subscriptions_mutex_);
      for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        if ((*it)->is_empty()) {
          subs_to_undeclare.push_back(*it);
          it = subscriptions_.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (auto& sub : subs_to_undeclare) {
      sub->wait_for_all_callbacks();
      z_undeclare_subscriber(z_move(sub->get_zenoh_sub()));
    }
  }

  z_owned_session_t session_;
  z_id_t zenoh_id_;
  std::string zenoh_id_str_;
  std::vector<std::string> cmdline_;
  std::string hostname_;
  int pid_ = 0;

  // indirection because absl::Mutex in IMWZenohPublisher cannot be copied
  std::list<std::shared_ptr<IMWZenohPublisher>> publishers_;
  std::list<std::shared_ptr<IMWZenohPublisher>> new_publishers_;
  absl::Mutex publishers_mutex_, new_publishers_mutex_;

  // indirection because absl::Mutex in IMWZenohSubscription cannot be copied
  std::list<std::shared_ptr<IMWZenohSubscription>> subscriptions_;
  absl::Mutex subscriptions_mutex_;

  std::list<std::shared_ptr<IMWZenohLivelinessSubscription>>
      liveliness_subscriptions_;
  absl::Mutex liveliness_subscriptions_mutex_;

  std::list<std::unique_ptr<IMWZenohQueryable>> queryables_;
  absl::Mutex queryables_mutex_;

  std::unordered_map<std::string, std::unique_ptr<IMWZenohLivelinessToken>>
      liveliness_tokens_;
  absl::Mutex liveliness_tokens_mutex_;

  std::shared_ptr<IMWZenohQueryTable> active_queries_;
  absl::Duration default_query_timeout_ = kDefaultQueryTimeout;
  absl::Duration query_gc_grace_ = kQueryGcGrace;

  std::string introspection_keyexpr_;
  std::thread introspection_thread_;

  // Used to signal introspection_thread_ to exit.
  absl::Notification introspection_thread_exit_requested_;

  absl::Duration introspection_publish_interval_ = absl::Seconds(1);
  bool introspection_enable_ = false;
  bool introspection_transmit_process_args_ = false;
  bool introspection_init();
  void introspection_thread_func();
  void introspection_collect_and_publish();
  void configure_from_json(const nlohmann::json& j);
};

}  // namespace intrinsic

#endif  // MIDDLEWARE_ZENOH_IMW_ZENOH_H_
