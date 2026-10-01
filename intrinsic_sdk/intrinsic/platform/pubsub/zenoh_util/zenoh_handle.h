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

#ifndef INTRINSIC_PLATFORM_PUBSUB_ZENOH_UTIL_ZENOH_HANDLE_H_
#define INTRINSIC_PLATFORM_PUBSUB_ZENOH_UTIL_ZENOH_HANDLE_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>

#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"

namespace intrinsic {

typedef std::function<void(const char*, const void*, const size_t)>
    imw_callback_functor_t;

typedef std::function<void(const char*, bool)>
    imw_liveliness_callback_functor_t;

typedef std::function<void(const char*)> imw_on_done_functor_t;

typedef void (*imw_on_done_callback_ptr_t)(const char*, void*);

struct QueryContext {
  imw_callback_functor_t* callback;
  imw_on_done_functor_t* on_done;
};

void zenoh_static_callback(const char* keyexpr, const void* blob,
                           size_t blob_len, void* fptr);

void zenoh_static_liveliness_callback(const char* keyexpr, bool alive,
                                      void* fptr);

void zenoh_static_liveliness_get_callback(const char* keyexpr, void* fptr);

void zenoh_static_liveliness_get_on_done_callback(const char* keyexpr,
                                                  void* fptr);

void zenoh_query_static_callback(const char* keyexpr, const void* blob,
                                 size_t blob_len, void* fptr);

void zenoh_query_static_on_done(const char* keyexpr, void* fptr);

// ZenohHandle loads the zenoh shared library and provides an interface for
// necessary PubSub calls to the shared library.
struct ZenohHandle {
 public:
  static ZenohHandle* CreateZenohHandle();

  std::add_pointer_t<imw_ret_t(const char* config)> imw_init;

  std::add_pointer_t<imw_ret_t()> imw_fini;

  std::add_pointer_t<imw_ret_t()> imw_destroy_session_when_unused;

  std::add_pointer_t<bool()> imw_is_initialized;

  std::add_pointer_t<imw_ret_t(const char* keyexpr, const char* qos)>
      imw_create_publisher;

  std::add_pointer_t<imw_ret_t(const char* keyexpr)> imw_destroy_publisher;

  std::add_pointer_t<imw_ret_t(const char* keyexpr, const void* bytes,
                               const size_t bytes_len)>
      imw_publish;

  std::add_pointer_t<imw_ret_t(const char* keyexpr, bool* has_matching)>
      imw_publisher_has_matching_subscribers;

  std::add_pointer_t<imw_ret_t(const char* keyexpr,
                               imw_subscription_callback_fn* callback,
                               const char* qos, void* user_context)>
      imw_create_subscription;

  std::add_pointer_t<imw_ret_t(const char* keyexpr,
                               imw_subscription_callback_fn* callback,
                               const void* user_context)>
      imw_destroy_subscription;

  std::add_pointer_t<int(const char* left, const char* right)>
      imw_keyexpr_intersects;

  std::add_pointer_t<int(const char* left, const char* right)>
      imw_keyexpr_includes;

  std::add_pointer_t<int(const char* keyexpr)> imw_keyexpr_is_canon;

  std::add_pointer_t<imw_ret_t(
      const char* keyexpr, imw_queryable_callback_fn* callback,
      void* user_context, imw_queryable_options_t* options)>
      imw_create_queryable;

  std::add_pointer_t<imw_ret_t(const char* keyexpr,
                               imw_queryable_callback_fn* callback,
                               void* user_context)>
      imw_destroy_queryable;

  std::add_pointer_t<imw_ret_t(const void* query_context, const char* keyexpr,
                               const void* reply_bytes,
                               const size_t reply_bytes_len)>
      imw_queryable_reply;

  std::add_pointer_t<imw_ret_t(const char* keyexpr, const void* bytes,
                               const size_t bytes_len)>
      imw_set;

  std::add_pointer_t<imw_ret_t(
      const char* keyexpr, imw_query_callback_fn* callback,
      imw_query_on_done_callback_fn* on_done, const void* query_payload,
      const size_t query_payload_len, void* user_context,
      imw_query_options_t* options)>
      imw_query;

  std::add_pointer_t<imw_ret_t(const char* keyexp)> imw_delete_keyexpr;

  std::add_pointer_t<imw_ret_t(
      const char* keyexpr, imw_liveliness_callback_fn* callback,
      bool notify_about_existing_tokens, void* user_context)>
      imw_create_liveliness_subscription;
  std::add_pointer_t<imw_ret_t(const char* keyexpr,
                               imw_liveliness_callback_fn* callback,
                               const void* user_context)>
      imw_destroy_liveliness_subscription;

  std::add_pointer_t<imw_ret_t(const char* keyexpr)>
      imw_declare_liveliness_token;
  std::add_pointer_t<imw_ret_t(const char* keyexpr)> imw_drop_liveliness_token;
  std::add_pointer_t<imw_ret_t(
      const char* keyexpr, imw_liveliness_get_callback_fn* callback,
      imw_liveliness_get_on_done_callback_fn* on_done, void* user_context)>
      imw_liveliness_get;

  std::add_pointer_t<const char* const()> imw_version;

  static absl::StatusOr<std::string> add_topic_prefix(absl::string_view topic);
  static absl::StatusOr<std::string> add_key_prefix(
      absl::string_view key, absl::string_view key_prefix);
  static absl::StatusOr<std::string> remove_topic_prefix(
      absl::string_view topic);

 private:
  void Initialize();
};

const ZenohHandle& Zenoh();

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_ZENOH_UTIL_ZENOH_HANDLE_H_
