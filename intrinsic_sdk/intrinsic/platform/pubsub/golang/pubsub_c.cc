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

#include "intrinsic/platform/pubsub/golang/pubsub_c.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "absl/base/attributes.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "intrinsic/platform/pubsub/adapters/pubsub.pb.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_config.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"

extern "C" char* GetZenohPeerConfigWrapper(const char* router_override) {
  return strdup(intrinsic::GetZenohPeerConfig(router_override).c_str());
}

ABSL_ATTRIBUTE_UNUSED void* NewZenohHandle() {
  return intrinsic::ZenohHandle::CreateZenohHandle();
  // return new intrinsic::PubSubInterfaceWrapper(std::move(pubsub_go_handle));
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwInit(void* handle, const char* config) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_init(config);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwFini(void* handle) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_fini();
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDestroySessionWhenUnused(void* handle) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_destroy_session_when_unused();
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwCreatePublisher(void* handle,
                                                        const char* keyexpr,
                                                        const char* qos) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_create_publisher(keyexpr, qos);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDestroyPublisher(void* handle,
                                                         const char* keyexpr) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_destroy_publisher(keyexpr);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwPublish(void* handle,
                                                const char* keyexpr,
                                                const void* bytes,
                                                const size_t bytes_len) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_publish(keyexpr, bytes, bytes_len);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwPublisherHasMatchingSubscribers(
    void* handle, const char* keyexpr, bool* has_matching) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_publisher_has_matching_subscribers(keyexpr,
                                                              has_matching);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwCreateSubscription(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_subscription_callback_fn callback, const char* qos,
    void* user_context) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);

  // type punning on callback?
  return zenoh_handle->imw_create_subscription(keyexpr, callback, qos,
                                               user_context);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDestroySubscription(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_subscription_callback_fn callback, void* user_context) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);

  // type punning on callback?
  return zenoh_handle->imw_destroy_subscription(keyexpr, callback,
                                                user_context);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDeclareLivelinessToken(
    void* handle, const char* keyexpr) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_declare_liveliness_token(keyexpr);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDropLivelinessToken(
    void* handle, const char* keyexpr) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_drop_liveliness_token(keyexpr);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwCreateLivelinessSubscription(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_liveliness_callback_fn callback,
    bool notify_about_existing_tokens, void* user_context) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_create_liveliness_subscription(
      keyexpr, callback, notify_about_existing_tokens, user_context);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDestroyLivelinessSubscription(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_liveliness_callback_fn callback, void* user_context) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_destroy_liveliness_subscription(keyexpr, callback,
                                                           user_context);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwLivelinessGet(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_liveliness_get_callback_fn callback,
    zenoh_handle_imw_liveliness_on_done_callback_fn on_done,
    void* user_context) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_liveliness_get(keyexpr, callback, on_done,
                                          user_context);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwCreateQueryable(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_queryable_callback_fn callback, void* user_context,
    bool is_ros_service) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);

  intrinsic::imw_queryable_options_t options{.is_ros_service = is_ros_service};
  return zenoh_handle->imw_create_queryable(keyexpr, callback, user_context,
                                            &options);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDestroyQueryable(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_queryable_callback_fn callback, void* user_context) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_destroy_queryable(keyexpr, callback, user_context);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwQueryableReply(
    void* handle, const void* query_context, const char* keyexpr,
    const void* reply_bytes, const size_t reply_bytes_len) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_queryable_reply(query_context, keyexpr, reply_bytes,
                                           reply_bytes_len);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwQuery(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_query_callback_fn callback,
    zenoh_handle_imw_query_on_done_fn on_done, const void* query_payload,
    size_t query_payload_len, void* user_context, uint64_t timeout_ms,
    bool call_ros_service) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  intrinsic::imw_query_options_t options{
      .timeout_ms = timeout_ms,
      .call_ros_service = call_ros_service,
  };
  return zenoh_handle->imw_query(keyexpr, callback, on_done, query_payload,
                                 query_payload_len, user_context, &options);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwSet(void* handle, const char* keyexpr,
                                            const void* bytes,
                                            const size_t bytes_len) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_set(keyexpr, bytes, bytes_len);
}
ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDeleteKeyExpr(void* handle,
                                                      const char* keyexpr) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_delete_keyexpr(keyexpr);
}
