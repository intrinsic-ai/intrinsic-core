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

#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_subscription.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"

namespace intrinsic {
namespace {

bool HasOtherThread(const std::vector<std::thread::id>& threads,
                    std::thread::id self_tid) {
  return std::any_of(
      threads.begin(), threads.end(),
      [self_tid](std::thread::id tid) { return tid != self_tid; });
}

void RemoveOneThread(std::vector<std::thread::id>* threads,
                     std::thread::id self_tid) {
  auto it = std::find(threads->begin(), threads->end(), self_tid);
  if (it != threads->end()) {
    threads->erase(it);
  }
}

void InvokeCallbackFn(imw_subscription_callback_fn* legacy_cb,
                      imw_subscription_message_callback_fn* message_cb,
                      const char* keyexpr, const void* blob, size_t blob_len,
                      const imw_message_metadata_t& metadata, void* user_ctx) {
  if (legacy_cb != nullptr) {
    legacy_cb(keyexpr, blob, blob_len, user_ctx);
  } else if (message_cb != nullptr) {
    message_cb(keyexpr, blob, blob_len, &metadata, user_ctx);
  }
}

}  // namespace

IMWZenohSubscription::IMWZenohSubscription(const std::string& keyexpr,
                                           CallbackEntry initial_entry)
    : keyexpr_(keyexpr), n_bytes(0), n_messages(0) {
  z_internal_subscriber_null(&zenoh_sub_);
  initial_entry.id = ++next_entry_id_;
  callbacks_.push_back(std::move(initial_entry));
}

IMWZenohSubscription::IMWZenohSubscription(
    const std::string& keyexpr, imw_subscription_callback_fn* callback,
    void* user_context)
    : IMWZenohSubscription(keyexpr,
                           CallbackEntry{.legacy_callback = callback,
                                         .message_callback = nullptr,
                                         .user_context = user_context}) {}

IMWZenohSubscription::IMWZenohSubscription(
    const std::string& keyexpr, imw_subscription_message_callback_fn* callback,
    void* user_context)
    : IMWZenohSubscription(keyexpr,
                           CallbackEntry{.legacy_callback = nullptr,
                                         .message_callback = callback,
                                         .user_context = user_context}) {}

IMWZenohSubscription::~IMWZenohSubscription() {
  if (z_internal_check(zenoh_sub_)) {
    LOG(ERROR) << "~IMWZenohSubscription had a valid sub!";
  }
}

void IMWZenohSubscription::AddEntry(CallbackEntry entry) {
  absl::MutexLock lock(&mutex_);
  entry.id = ++next_entry_id_;
  callbacks_.push_back(std::move(entry));
}

void IMWZenohSubscription::add_callback(imw_subscription_callback_fn* fptr,
                                        void* user_context) {
  AddEntry(CallbackEntry{.legacy_callback = fptr,
                         .message_callback = nullptr,
                         .user_context = user_context});
}

void IMWZenohSubscription::add_message_callback(
    imw_subscription_message_callback_fn* fptr, void* user_context) {
  AddEntry(CallbackEntry{.legacy_callback = nullptr,
                         .message_callback = fptr,
                         .user_context = user_context});
}

bool IMWZenohSubscription::clear_callbacks() {
  absl::MutexLock lock(&mutex_);
  for (auto it = callbacks_.begin(); it != callbacks_.end();) {
    it->removed = true;
    if (it->refs == 0) {
      it = callbacks_.erase(it);
    } else {
      ++it;
    }
  }
  return true;
}

void IMWZenohSubscription::wait_for_entry_drain(uint64_t id) {
  const std::thread::id self_tid = std::this_thread::get_id();
  absl::MutexLock lock(&mutex_);
  while (std::any_of(callbacks_.begin(), callbacks_.end(),
                     [id, self_tid](const CallbackEntry& entry) {
                       return entry.id == id &&
                              HasOtherThread(entry.active_threads, self_tid);
                     })) {
    cv_.Wait(&mutex_);
  }
}

void IMWZenohSubscription::wait_for_all_callbacks() {
  const std::thread::id self_tid = std::this_thread::get_id();
  absl::MutexLock lock(&mutex_);
  while (std::any_of(callbacks_.begin(), callbacks_.end(),
                     [self_tid](const CallbackEntry& entry) {
                       return HasOtherThread(entry.active_threads, self_tid);
                     })) {
    cv_.Wait(&mutex_);
  }
}

bool IMWZenohSubscription::is_empty() const {
  absl::MutexLock lock(&mutex_);
  for (const CallbackEntry& entry : callbacks_) {
    if (!entry.removed) {
      return false;
    }
  }
  return true;
}

bool IMWZenohSubscription::DetachEntry(
    imw_subscription_callback_fn* legacy_cb,
    imw_subscription_message_callback_fn* message_cb, const void* user_context,
    const char* kind_label, uint64_t* detached_id, bool* needs_drain) {
  const std::thread::id self_tid = std::this_thread::get_id();
  absl::MutexLock lock(&mutex_);
  for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it) {
    if (!it->removed && it->legacy_callback == legacy_cb &&
        it->message_callback == message_cb &&
        it->user_context == user_context) {
      it->removed = true;
      *detached_id = it->id;
      *needs_drain = HasOtherThread(it->active_threads, self_tid);
      if (it->refs == 0) {
        callbacks_.erase(it);
      }
      return true;
    }
  }
  LOG(ERROR) << "Tried to remove " << kind_label << " to " << keyexpr_
             << " but could not find it";
  return false;
}

bool IMWZenohSubscription::detach_callback(imw_subscription_callback_fn* fptr,
                                           const void* user_context,
                                           uint64_t* detached_id,
                                           bool* needs_drain) {
  return DetachEntry(fptr, nullptr, user_context, "callback", detached_id,
                     needs_drain);
}

bool IMWZenohSubscription::detach_message_callback(
    imw_subscription_message_callback_fn* fptr, const void* user_context,
    uint64_t* detached_id, bool* needs_drain) {
  return DetachEntry(nullptr, fptr, user_context, "message callback",
                     detached_id, needs_drain);
}

std::vector<std::list<IMWZenohSubscription::CallbackEntry>::iterator>
IMWZenohSubscription::CollectTargetsLocked() {
  std::vector<std::list<CallbackEntry>::iterator> targets;
  targets.reserve(callbacks_.size());
  for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it) {
    if (!it->removed) {
      ++it->refs;
      targets.push_back(it);
    }
  }
  return targets;
}

void IMWZenohSubscription::invoke_callbacks(
    const char* keyexpr, const void* blob, size_t blob_len,
    const imw_message_metadata_t& metadata) {
  const std::thread::id self_tid = std::this_thread::get_id();
  mutex_.Lock();
  n_messages++;
  n_bytes += blob_len;
  const auto targets = CollectTargetsLocked();
  for (auto it : targets) {
    if (!it->removed) {
      it->active_threads.push_back(self_tid);
      imw_subscription_callback_fn* const legacy_cb = it->legacy_callback;
      imw_subscription_message_callback_fn* const message_cb =
          it->message_callback;
      void* const user_ctx = it->user_context;
      mutex_.Unlock();
      InvokeCallbackFn(legacy_cb, message_cb, keyexpr, blob, blob_len, metadata,
                       user_ctx);
      mutex_.Lock();
      RemoveOneThread(&it->active_threads, self_tid);
      if (it->removed) {
        cv_.SignalAll();
      }
    }
    if (--it->refs == 0 && it->removed) {
      callbacks_.erase(it);
    }
  }
  mutex_.Unlock();
}

IMWZenohSubscription::Statistics IMWZenohSubscription::get_statistics() {
  absl::MutexLock lock(&mutex_);
  Statistics s;
  s.n_messages = n_messages;
  s.n_bytes = n_bytes;
  return s;
}

}  // namespace intrinsic
