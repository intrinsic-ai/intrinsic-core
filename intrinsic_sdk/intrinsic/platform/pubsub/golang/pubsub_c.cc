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

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_config.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"

namespace {

static_assert(ZENOH_HANDLE_IMW_MESSAGE_KIND_PUT ==
              static_cast<int>(intrinsic::IMW_MESSAGE_KIND_PUT));
static_assert(ZENOH_HANDLE_IMW_MESSAGE_KIND_DELETE ==
              static_cast<int>(intrinsic::IMW_MESSAGE_KIND_DELETE));
static_assert(ZENOH_HANDLE_IMW_ENCODING_VARINT_PREFIXED_BYTES ==
              static_cast<int>(intrinsic::IMW_ENCODING_VARINT_PREFIXED_BYTES));
static_assert(
    ZENOH_HANDLE_IMW_ENCODING_APPLICATION_OCTET_STREAM ==
    static_cast<int>(intrinsic::IMW_ENCODING_APPLICATION_OCTET_STREAM));
static_assert(ZENOH_HANDLE_IMW_ENCODING_APPLICATION_PROTOBUF ==
              static_cast<int>(intrinsic::IMW_ENCODING_APPLICATION_PROTOBUF));
static_assert(ZENOH_HANDLE_IMW_ENCODING_UNKNOWN ==
              static_cast<int>(intrinsic::IMW_ENCODING_UNKNOWN));

std::optional<intrinsic::imw_encoding_t> TryToImwEncoding(int encoding) {
  switch (encoding) {
    case ZENOH_HANDLE_IMW_ENCODING_VARINT_PREFIXED_BYTES:
      return intrinsic::IMW_ENCODING_VARINT_PREFIXED_BYTES;
    case ZENOH_HANDLE_IMW_ENCODING_APPLICATION_OCTET_STREAM:
      return intrinsic::IMW_ENCODING_APPLICATION_OCTET_STREAM;
    case ZENOH_HANDLE_IMW_ENCODING_APPLICATION_PROTOBUF:
      return intrinsic::IMW_ENCODING_APPLICATION_PROTOBUF;
    case ZENOH_HANDLE_IMW_ENCODING_UNKNOWN:
    default:
      return std::nullopt;
  }
}

struct MessageSubscriptionContext {
  zenoh_handle_imw_subscription_message_callback_fn callback = nullptr;
  uintptr_t user_context = 0;
};

struct UnpackedMessageMetadata {
  int message_kind;
  int encoding;
};

UnpackedMessageMetadata UnpackMessageMetadata(
    const intrinsic::imw_message_metadata_t* metadata) {
  if (metadata == nullptr) {
    return UnpackedMessageMetadata{
        .message_kind = ZENOH_HANDLE_IMW_MESSAGE_KIND_PUT,
        .encoding = ZENOH_HANDLE_IMW_ENCODING_VARINT_PREFIXED_BYTES,
    };
  }
  return UnpackedMessageMetadata{
      .message_kind = static_cast<int>(metadata->kind),
      .encoding = static_cast<int>(metadata->encoding),
  };
}

void MessageSubscriptionTrampoline(
    const char* keyexpr, const void* bytes, const size_t bytes_len,
    const intrinsic::imw_message_metadata_t* metadata, void* user_context) {
  auto* ctx = static_cast<MessageSubscriptionContext*>(user_context);
  if (ctx == nullptr || ctx->callback == nullptr) {
    return;
  }
  const UnpackedMessageMetadata unpacked = UnpackMessageMetadata(metadata);
  ctx->callback(keyexpr, bytes, bytes_len, unpacked.message_kind,
                unpacked.encoding, ctx->user_context);
}

// Bridges ZenohHandle::imw_query_messages (`void*` + `const
// imw_message_metadata_t*`) into CGo `uintptr_t` handles and unpacked
// `(message_kind, encoding)` callbacks.
//
// Zenoh may invoke the reply closure destructor
// (`MessageQueryOnDoneTrampoline`) synchronously inside `imw_query_messages` on
// either success or `z_get` failure. `in_flight` and `closure_dropped` (guarded
// by `mu`) defer `on_done` until `imw_query_messages` returns `IMW_OK`
// (suppressing `on_done` if `z_get` failed), while `pending_owners` (`2`)
// splits lifetime between `ZenohHandleImwQueryMessages` and
// `MessageQueryOnDoneTrampoline` so whichever finishes second deletes `ctx` (or
// `ZenohHandleImwQueryMessages` deletes `ctx` directly if registration failed
// without dropping the closure).
struct MessageQueryContext {
  zenoh_handle_imw_query_message_callback_fn callback = nullptr;
  zenoh_handle_imw_message_query_on_done_fn on_done = nullptr;
  uintptr_t user_context = 0;
  absl::Mutex mu;
  bool in_flight ABSL_GUARDED_BY(mu) = true;
  bool closure_dropped ABSL_GUARDED_BY(mu) = false;
  std::atomic<int> pending_owners{2};
};

void MessageQueryTrampoline(const char* keyexpr, const void* response_bytes,
                            const size_t response_bytes_len,
                            const intrinsic::imw_message_metadata_t* metadata,
                            void* user_context) {
  auto* ctx = static_cast<MessageQueryContext*>(user_context);
  if (ctx == nullptr || ctx->callback == nullptr) {
    return;
  }
  const UnpackedMessageMetadata unpacked = UnpackMessageMetadata(metadata);
  ctx->callback(keyexpr, response_bytes, response_bytes_len,
                unpacked.message_kind, unpacked.encoding, ctx->user_context);
}

void MessageQueryOnDoneTrampoline(const char* keyexpr, void* user_context) {
  auto* ctx = static_cast<MessageQueryContext*>(user_context);
  if (ctx == nullptr) {
    return;
  }
  bool invoke_now = false;
  {
    absl::MutexLock lock(&ctx->mu);
    ctx->closure_dropped = true;
    invoke_now = !ctx->in_flight;
  }
  if (invoke_now && ctx->on_done != nullptr) {
    ctx->on_done(keyexpr, ctx->user_context);
  }
  if (ctx->pending_owners.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete ctx;
  }
}

}  // namespace

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

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwCreateMessageSubscription(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_subscription_message_callback_fn callback, const char* qos,
    uintptr_t user_context, void** out_subscription_context) {
  if (handle == nullptr || callback == nullptr ||
      out_subscription_context == nullptr) {
    return intrinsic::IMW_ERROR;
  }
  *out_subscription_context = nullptr;
  auto* zenoh_handle = static_cast<intrinsic::ZenohHandle*>(handle);
  auto ctx =
      std::make_unique<MessageSubscriptionContext>(MessageSubscriptionContext{
          .callback = callback, .user_context = user_context});
  const int ret = zenoh_handle->imw_create_message_subscription(
      keyexpr, MessageSubscriptionTrampoline, qos, ctx.get());
  if (ret != intrinsic::IMW_OK) {
    return ret;
  }
  *out_subscription_context = ctx.release();
  return ret;
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

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDestroyMessageSubscription(
    void* handle, const char* keyexpr, void* subscription_context) {
  if (handle == nullptr || subscription_context == nullptr) {
    return intrinsic::IMW_ERROR;
  }
  auto* zenoh_handle = static_cast<intrinsic::ZenohHandle*>(handle);
  auto* ctx = static_cast<MessageSubscriptionContext*>(subscription_context);
  const int ret = zenoh_handle->imw_destroy_message_subscription(
      keyexpr, MessageSubscriptionTrampoline, ctx);
  if (ret == intrinsic::IMW_OK) {
    delete ctx;
  }
  return ret;
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

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwSetWithEncoding(void* handle,
                                                        const char* keyexpr,
                                                        const void* bytes,
                                                        const size_t bytes_len,
                                                        int encoding) {
  if (handle == nullptr) {
    return intrinsic::IMW_ERROR;
  }
  const std::optional<intrinsic::imw_encoding_t> imw_encoding =
      TryToImwEncoding(encoding);
  if (!imw_encoding.has_value()) {
    return intrinsic::IMW_ERROR;
  }
  auto* zenoh_handle = static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_set_with_encoding(keyexpr, bytes, bytes_len,
                                             *imw_encoding);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwDeleteKeyExpr(void* handle,
                                                      const char* keyexpr) {
  intrinsic::ZenohHandle* zenoh_handle =
      static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_delete_keyexpr(keyexpr);
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

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwQueryableReplyWithEncoding(
    void* handle, const void* query_context, const char* keyexpr,
    const void* reply_bytes, const size_t reply_bytes_len, int encoding) {
  if (handle == nullptr) {
    return intrinsic::IMW_ERROR;
  }
  const std::optional<intrinsic::imw_encoding_t> imw_encoding =
      TryToImwEncoding(encoding);
  if (!imw_encoding.has_value()) {
    return intrinsic::IMW_ERROR;
  }
  auto* zenoh_handle = static_cast<intrinsic::ZenohHandle*>(handle);
  return zenoh_handle->imw_queryable_reply_with_encoding(
      query_context, keyexpr, reply_bytes, reply_bytes_len, *imw_encoding);
}

ABSL_ATTRIBUTE_UNUSED int ZenohHandleImwQueryMessages(
    void* handle, const char* keyexpr,
    zenoh_handle_imw_query_message_callback_fn callback,
    zenoh_handle_imw_message_query_on_done_fn on_done, uintptr_t user_context,
    uint64_t timeout_ms) {
  if (handle == nullptr || keyexpr == nullptr || callback == nullptr) {
    return intrinsic::IMW_ERROR;
  }
  auto* zenoh_handle = static_cast<intrinsic::ZenohHandle*>(handle);
  intrinsic::imw_query_options_t options{
      .timeout_ms = timeout_ms,
      .call_ros_service = false,
  };
  auto* ctx = new MessageQueryContext;
  ctx->callback = callback;
  ctx->on_done = on_done;
  ctx->user_context = user_context;
  const int ret = zenoh_handle->imw_query_messages(
      keyexpr, MessageQueryTrampoline, MessageQueryOnDoneTrampoline,
      /*query_payload=*/nullptr, /*query_payload_len=*/0, ctx, &options);
  bool closure_dropped = false;
  {
    absl::MutexLock lock(&ctx->mu);
    ctx->in_flight = false;
    closure_dropped = ctx->closure_dropped;
  }
  if (ret == intrinsic::IMW_OK && closure_dropped && ctx->on_done != nullptr) {
    ctx->on_done(keyexpr, ctx->user_context);
  }
  // When imw_query_messages fails before registering reply_closure,
  // MessageQueryOnDoneTrampoline will never run, so delete ctx directly.
  const bool closure_owns_ctx = (ret == intrinsic::IMW_OK) || closure_dropped;
  if (!closure_owns_ctx ||
      ctx->pending_owners.fetch_sub(1, std::memory_order_acq_rel) == 1) {
    delete ctx;
  }
  return ret;
}
