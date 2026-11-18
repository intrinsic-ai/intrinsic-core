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

#ifndef MIDDLEWARE_ZENOH_IMW_ZENOH_SUBSCRIPTION_H_
#define MIDDLEWARE_ZENOH_IMW_ZENOH_SUBSCRIPTION_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"
#include "zenoh.h"  // NOLINT(build/include_subdir)

namespace intrinsic {

class IMWZenohSubscription {
 public:
  IMWZenohSubscription(const std::string& keyexpr,
                       imw_subscription_callback_fn* callback,
                       void* user_context);

  IMWZenohSubscription(const std::string& keyexpr,
                       imw_subscription_message_callback_fn* callback,
                       void* user_context);

  IMWZenohSubscription(IMWZenohSubscription&& other) = delete;
  IMWZenohSubscription(const IMWZenohSubscription&) = delete;
  IMWZenohSubscription& operator=(const IMWZenohSubscription&) = delete;

  ~IMWZenohSubscription();

  void set_zenoh_sub(z_owned_subscriber_t zenoh_sub) { zenoh_sub_ = zenoh_sub; }
  z_owned_subscriber_t& get_zenoh_sub() { return zenoh_sub_; }
  const std::string& get_keyexpr() const { return keyexpr_; }
  void add_callback(imw_subscription_callback_fn* fptr, void* user_context);
  void add_message_callback(imw_subscription_message_callback_fn* fptr,
                            void* user_context);
  bool detach_callback(imw_subscription_callback_fn* fptr,
                       const void* user_context, uint64_t* detached_id,
                       bool* needs_drain);
  bool detach_message_callback(imw_subscription_message_callback_fn* fptr,
                               const void* user_context, uint64_t* detached_id,
                               bool* needs_drain);
  void wait_for_entry_drain(uint64_t id);
  bool clear_callbacks();
  void wait_for_all_callbacks();
  bool is_empty() const;
  void invoke_callbacks(const char* keyexpr, const void* blob, size_t blob_len,
                        const imw_message_metadata_t& metadata);
  struct Statistics {
    size_t n_messages;
    size_t n_bytes;
  };
  Statistics get_statistics();

 private:
  struct CallbackEntry {
    uint64_t id = 0;
    imw_subscription_callback_fn* legacy_callback = nullptr;
    imw_subscription_message_callback_fn* message_callback = nullptr;
    void* user_context = nullptr;
    std::vector<std::thread::id> active_threads;
    size_t refs = 0;
    bool removed = false;
  };

  IMWZenohSubscription(const std::string& keyexpr, CallbackEntry initial_entry);

  void AddEntry(CallbackEntry entry);
  std::vector<std::list<CallbackEntry>::iterator> CollectTargetsLocked()
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  bool DetachEntry(imw_subscription_callback_fn* legacy_cb,
                   imw_subscription_message_callback_fn* message_cb,
                   const void* user_context, const char* kind_label,
                   uint64_t* detached_id, bool* needs_drain);

  std::string keyexpr_;
  z_owned_subscriber_t zenoh_sub_;
  mutable absl::Mutex mutex_;  // leaf lock guarding callback list and stats
  absl::CondVar cv_;
  uint64_t next_entry_id_ ABSL_GUARDED_BY(mutex_) = 0;
  std::list<CallbackEntry> callbacks_ ABSL_GUARDED_BY(mutex_);
  size_t n_bytes ABSL_GUARDED_BY(mutex_);
  size_t n_messages ABSL_GUARDED_BY(mutex_);
};

}  // namespace intrinsic

#endif  // MIDDLEWARE_ZENOH_IMW_ZENOH_SUBSCRIPTION_H_
