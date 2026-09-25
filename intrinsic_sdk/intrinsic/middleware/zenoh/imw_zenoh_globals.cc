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

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/const_init.h"
#include "absl/base/no_destructor.h"
#include "absl/base/thread_annotations.h"
#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_data_callback_context.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_liveliness_get_context.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_query_context.h"
#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_queryable_context.h"

namespace intrinsic {

ABSL_CONST_INIT absl::Mutex IMWZenoh::init_fini_mutex_(absl::kConstInit);

namespace {

absl::NoDestructor<std::shared_ptr<IMWZenoh>> g_imw_zenoh_singleton
    ABSL_GUARDED_BY(IMWZenoh::init_fini_mutex_);
int g_imw_init_refcount ABSL_GUARDED_BY(IMWZenoh::init_fini_mutex_) = 0;
int g_imw_entity_refcount ABSL_GUARDED_BY(IMWZenoh::init_fini_mutex_) = 0;
bool g_imw_destroy_when_unused ABSL_GUARDED_BY(IMWZenoh::init_fini_mutex_) =
    false;
absl::NoDestructor<std::string> g_imw_active_config
    ABSL_GUARDED_BY(IMWZenoh::init_fini_mutex_);

bool IsSessionUnusedLocked()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(IMWZenoh::init_fini_mutex_) {
  return g_imw_init_refcount == 0 && g_imw_entity_refcount == 0;
}

std::shared_ptr<IMWZenoh> GetSingleton()
    ABSL_LOCKS_EXCLUDED(IMWZenoh::init_fini_mutex_) {
  absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
  return *g_imw_zenoh_singleton;
}

// Detaches the session so ~IMWZenoh() -> destroy_session() (z_drop) runs
// outside init_fini_mutex_, avoiding deadlocks with in-flight Zenoh callbacks
// and keeping the instance alive until concurrent GetSingleton() callers
// finish.
std::shared_ptr<IMWZenoh> DetachSessionLocked()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(IMWZenoh::init_fini_mutex_) {
  g_imw_init_refcount = 0;
  g_imw_entity_refcount = 0;
  g_imw_destroy_when_unused = false;
  g_imw_active_config->clear();
  return std::exchange(*g_imw_zenoh_singleton, nullptr);
}

imw_ret_t CreateSessionLocked(const char* config)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(IMWZenoh::init_fini_mutex_) {
  auto singleton = std::make_shared<IMWZenoh>();
  const imw_ret_t ret = singleton->create_session(config);
  if (ret != IMW_OK) {
    return ret;
  }
  *g_imw_zenoh_singleton = std::move(singleton);
  *g_imw_active_config = config;
  g_imw_init_refcount = 1;
  g_imw_entity_refcount = 0;
  g_imw_destroy_when_unused = false;
  LOG(INFO) << "Created a zenoh session with libimw_zenoh version: "
            << IMWZenoh::version();
  return IMW_OK;
}

imw_ret_t AttachOrCreateSessionLocked(const char* config)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(IMWZenoh::init_fini_mutex_) {
  if (*g_imw_zenoh_singleton == nullptr) {
    return CreateSessionLocked(config);
  }
  g_imw_init_refcount++;
  if (!g_imw_active_config->empty() && *g_imw_active_config != config) {
    LOG(WARNING)
        << "A Zenoh session is already initialized with configuration: "
        << *g_imw_active_config
        << ". The provided config will be ignored: " << config;
  }
  return IMW_OK;
}

template <typename Fn>
imw_ret_t CreateEntity(Fn&& fn)
    ABSL_LOCKS_EXCLUDED(IMWZenoh::init_fini_mutex_) {
  std::shared_ptr<IMWZenoh> singleton;
  {
    absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
    if (*g_imw_zenoh_singleton == nullptr) return IMW_NOT_INITIALIZED;
    g_imw_entity_refcount++;
    singleton = *g_imw_zenoh_singleton;
  }
  const imw_ret_t ret = fn(*singleton);
  if (ret != IMW_OK) {
    std::shared_ptr<IMWZenoh> to_destroy;
    {
      absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
      if (*g_imw_zenoh_singleton == singleton) {
        if (g_imw_entity_refcount > 0) g_imw_entity_refcount--;
        if (IsSessionUnusedLocked() && g_imw_destroy_when_unused) {
          to_destroy = DetachSessionLocked();
        }
      }
    }
  }
  return ret;
}

template <typename Fn>
imw_ret_t DestroyEntity(Fn&& fn)
    ABSL_LOCKS_EXCLUDED(IMWZenoh::init_fini_mutex_) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;
  // Undeclare the Zenoh entity before decrementing g_imw_entity_refcount so
  // IsSessionUnusedLocked() never becomes true while an entity callback is
  // active.
  const imw_ret_t ret = fn(*singleton);
  if (ret == IMW_OK) {
    std::shared_ptr<IMWZenoh> to_destroy;
    {
      absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
      if (*g_imw_zenoh_singleton == singleton) {
        if (g_imw_entity_refcount > 0) g_imw_entity_refcount--;
        if (IsSessionUnusedLocked() && g_imw_destroy_when_unused) {
          to_destroy = DetachSessionLocked();
        }
      }
    }
  }
  return ret;
}

}  // namespace

void IMWZenoh::static_data_callback(z_loaned_sample_t* sample,
                                    void* untyped_context) {
  IMWZenohDataCallbackContext* zenoh_context =
      static_cast<IMWZenohDataCallbackContext*>(untyped_context);

  zenoh_context->get_imw_zenoh_instance()->data_callback(
      zenoh_context->get_subscription_keyexpr(), sample);
}

void IMWZenoh::static_liveliness_callback(z_loaned_sample_t* sample,
                                          void* untyped_context) {
  IMWZenohDataCallbackContext* zenoh_context =
      static_cast<IMWZenohDataCallbackContext*>(untyped_context);

  zenoh_context->get_imw_zenoh_instance()->liveliness_callback(
      zenoh_context->get_subscription_keyexpr(), sample);
}

void IMWZenoh::static_closure_drop(void* untyped_context) {
  // This callback is run by Zenoh during a call to z_undeclare_subscriber().
  // The intent is to use it to free the user_context memory that was allocated
  // on the heap and passed to Zenoh as part of the call to
  // declare_subscriber().
  IMWZenohDataCallbackContext* zenoh_context =
      static_cast<IMWZenohDataCallbackContext*>(untyped_context);
  delete zenoh_context;
}

void IMWZenoh::static_queryable_callback(z_loaned_query_t* query,
                                         void* untyped_context) {
  IMWZenohQueryableContext* context =
      static_cast<IMWZenohQueryableContext*>(untyped_context);
  context->get_imw_zenoh_instance()->queryable_callback(
      context->get_queryable_keyexpr(), query);
}

void IMWZenoh::static_queryable_drop(void* untyped_context) {
  // This callback is run by Zenoh during a call to z_undeclare_queryable().
  // The intent is to use it to free the user_context memory that was allocated
  // on the heap and passed to Zenoh as part of the call to
  // declare_queryable().
  IMWZenohQueryableContext* typed_context =
      static_cast<IMWZenohQueryableContext*>(untyped_context);
  delete typed_context;
}

void IMWZenoh::static_query_callback(z_loaned_reply_t* reply,
                                     void* untyped_context) {
  IMWZenohQueryContext* context =
      static_cast<IMWZenohQueryContext*>(untyped_context);
  context->imw_zenoh_instance_->query_callback(
      context->keyexpr_, context->callback_, reply, context->user_context_,
      &context->options_);
}

void IMWZenoh::static_query_drop(void* untyped_context) {
  IMWZenohQueryContext* typed_context =
      static_cast<IMWZenohQueryContext*>(untyped_context);
  if (typed_context != nullptr && typed_context->on_done_ != nullptr) {
    typed_context->on_done_(typed_context->keyexpr_,
                            typed_context->user_context_);
  }
  delete typed_context;
}

void IMWZenoh::static_liveliness_get_callback(z_loaned_reply_t* reply,
                                              void* untyped_context) {
  IMWLivelinessGetContext* context =
      static_cast<IMWLivelinessGetContext*>(untyped_context);
  context->imw_zenoh_instance_->liveliness_get_callback(
      reply, context->callback_, context->user_context_);
}

void IMWZenoh::static_liveliness_get_drop(void* untyped_context) {
  IMWLivelinessGetContext* typed_context =
      static_cast<IMWLivelinessGetContext*>(untyped_context);
  if (typed_context != nullptr && typed_context->on_done_ != nullptr) {
    typed_context->on_done_(typed_context->keyexpr_.c_str(),
                            typed_context->user_context_);
  }
  delete typed_context;
}

imw_ret_t imw_init(const char* config) {
  if (config == nullptr || config[0] == '\0') {
    LOG(ERROR) << "Zenoh config must not be NULL or empty";
    return IMW_ERROR;
  }
  std::shared_ptr<IMWZenoh> to_destroy;
  {
    absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
    if (*g_imw_zenoh_singleton == nullptr || !IsSessionUnusedLocked() ||
        *g_imw_active_config == config) {
      return AttachOrCreateSessionLocked(config);
    }
    LOG(INFO) << "Recreating idle Zenoh session with new configuration.";
    to_destroy = DetachSessionLocked();
  }
  // Close the old idle session outside `init_fini_mutex_` before opening the
  // new one. If another thread calls `imw_init` while unlocked, the second
  // `AttachOrCreateSessionLocked` call attaches to that session instead of
  // looping or blocking teardown.
  to_destroy.reset();

  absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
  return AttachOrCreateSessionLocked(config);
}

imw_ret_t imw_fini() {
  std::shared_ptr<IMWZenoh> to_destroy;
  {
    absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
    if (g_imw_init_refcount > 0) {
      g_imw_init_refcount--;
    }
    if (IsSessionUnusedLocked() && g_imw_destroy_when_unused) {
      to_destroy = DetachSessionLocked();
    }
  }
  return IMW_OK;
}

imw_ret_t imw_destroy_session_when_unused() {
  std::shared_ptr<IMWZenoh> to_destroy;
  {
    absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
    if (*g_imw_zenoh_singleton == nullptr) {
      g_imw_destroy_when_unused = false;
      return IMW_OK;
    }
    if (IsSessionUnusedLocked()) {
      to_destroy = DetachSessionLocked();
    } else {
      g_imw_destroy_when_unused = true;
    }
  }
  return IMW_OK;
}

bool imw_is_initialized() {
  absl::MutexLock lock(&IMWZenoh::init_fini_mutex_);
  return *g_imw_zenoh_singleton != nullptr;
}

imw_ret_t imw_create_publisher(const char* keyexpr, const char* qos) {
  return CreateEntity(
      [&](IMWZenoh& s) { return s.create_publisher(keyexpr, qos); });
}

imw_ret_t imw_destroy_publisher(const char* keyexpr) {
  return DestroyEntity(
      [&](IMWZenoh& s) { return s.destroy_publisher(keyexpr); });
}

imw_ret_t imw_publish(const char* keyexpr, const void* bytes,
                      const size_t bytes_len) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;

  return singleton->publish(keyexpr, bytes, bytes_len);
}

imw_ret_t imw_create_subscription(const char* keyexpr,
                                  imw_subscription_callback_fn* callback,
                                  const char* qos, void* user_context) {
  return CreateEntity([&](IMWZenoh& s) {
    return s.create_subscription(keyexpr, callback, qos, user_context);
  });
}

imw_ret_t imw_destroy_subscription(const char* keyexpr,
                                   imw_subscription_callback_fn* callback,
                                   const void* user_context) {
  return DestroyEntity([&](IMWZenoh& s) {
    return s.destroy_subscription(keyexpr, callback, user_context);
  });
}

int imw_keyexpr_includes(const char* left, const char* right) {
  return IMWZenoh::keyexpr_includes(left, right);
}

int imw_keyexpr_intersects(const char* left, const char* right) {
  return IMWZenoh::keyexpr_intersects(left, right);
}

int imw_keyexpr_is_canon(const char* keyexpr) {
  return IMWZenoh::keyexpr_is_canon(keyexpr);
}

imw_ret_t imw_queryable_reply(const void* query_context, const char* keyexpr,
                              const void* reply_bytes,
                              const size_t reply_bytes_len) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;
  return singleton->queryable_reply(query_context, keyexpr, reply_bytes,
                                    reply_bytes_len);
}

imw_ret_t imw_create_queryable(const char* keyexpr,
                               imw_queryable_callback_fn* callback,
                               void* user_context,
                               imw_queryable_options_t* options) {
  return CreateEntity([&](IMWZenoh& s) {
    return s.create_queryable(keyexpr, callback, user_context, options);
  });
}

imw_ret_t imw_destroy_queryable(const char* keyexpr,
                                imw_queryable_callback_fn* callback,
                                void* user_context) {
  return DestroyEntity([&](IMWZenoh& s) {
    return s.destroy_queryable(keyexpr, callback, user_context);
  });
}

imw_ret_t imw_query(const char* keyexpr, imw_query_callback_fn* callback,
                    imw_query_on_done_callback_fn* on_done,
                    const void* query_payload, const size_t query_payload_len,
                    void* user_context, imw_query_options_t* options) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;
  return singleton->query(keyexpr, callback, on_done, query_payload,
                          query_payload_len, user_context, options);
}

imw_ret_t imw_set(const char* keyexpr, const void* bytes,
                  const size_t bytes_len) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;
  return singleton->set(keyexpr, bytes, bytes_len);
}

imw_ret_t imw_delete_keyexpr(const char* keyexpr) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;
  return singleton->delete_keyexpr(keyexpr);
}

imw_ret_t imw_create_liveliness_subscription(
    const char* keyexpr, imw_liveliness_callback_fn* callback,
    bool notify_about_existing_tokens, void* user_context) {
  return CreateEntity([&](IMWZenoh& s) {
    return s.create_liveliness_subscription(
        keyexpr, callback, notify_about_existing_tokens, user_context);
  });
}

imw_ret_t imw_destroy_liveliness_subscription(
    const char* keyexpr, imw_liveliness_callback_fn* callback,
    const void* user_context) {
  return DestroyEntity([&](IMWZenoh& s) {
    return s.destroy_liveliness_subscription(keyexpr, callback, user_context);
  });
}

// Liveliness tokens are bound directly to PubSub methods rather than
// standalone RAII handles, so they use GetSingleton() rather than
// CreateEntity/DestroyEntity and are cleaned up automatically in
// IMWZenoh::destroy_session().
imw_ret_t imw_declare_liveliness_token(const char* keyexpr) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;

  return singleton->declare_liveliness_token(keyexpr);
}

imw_ret_t imw_drop_liveliness_token(const char* keyexpr) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;

  return singleton->drop_liveliness_token(keyexpr);
}

imw_ret_t imw_liveliness_get(const char* keyexpr,
                             imw_liveliness_get_callback_fn* callback,
                             imw_liveliness_get_on_done_callback_fn* on_done,
                             void* user_context) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;

  return singleton->liveliness_get(keyexpr, callback, on_done, user_context);
}

const char* const imw_version() { return IMWZenoh::version(); }

imw_ret_t imw_publisher_has_matching_subscribers(const char* keyexpr,
                                                 bool* has_matching) {
  const std::shared_ptr<IMWZenoh> singleton = GetSingleton();
  if (singleton == nullptr) return IMW_NOT_INITIALIZED;
  return singleton->publisher_has_matching_subscribers(keyexpr, has_matching);
}

}  // namespace intrinsic
