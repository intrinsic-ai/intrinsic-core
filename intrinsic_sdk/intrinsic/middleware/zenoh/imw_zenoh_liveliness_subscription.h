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

#ifndef MIDDLEWARE_ZENOH_IMW_ZENOH_LIVELINESS_SUBSCRIPTION_H_
#define MIDDLEWARE_ZENOH_IMW_ZENOH_LIVELINESS_SUBSCRIPTION_H_

#include <cstddef>
#include <string>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"
#include "zenoh.h"  // NOLINT(build/include_subdir)

namespace intrinsic {

class IMWZenohLivelinessSubscription {
 public:
  IMWZenohLivelinessSubscription(const std::string& keyexpr,
                                 imw_liveliness_callback_fn* callback,
                                 void* user_context);

  IMWZenohLivelinessSubscription(IMWZenohLivelinessSubscription&& other) =
      delete;
  IMWZenohLivelinessSubscription(const IMWZenohLivelinessSubscription&) =
      delete;
  IMWZenohLivelinessSubscription& operator=(
      const IMWZenohLivelinessSubscription&) = delete;

  ~IMWZenohLivelinessSubscription();

  void set_zenoh_sub(z_owned_subscriber_t zenoh_sub) { zenoh_sub_ = zenoh_sub; }
  z_owned_subscriber_t& get_zenoh_sub() { return zenoh_sub_; }
  const std::string& get_keyexpr() const { return keyexpr_; }
  imw_liveliness_callback_fn* get_callback() const { return callback_; }
  const void* get_user_context() const { return user_context_; }
  void disable() ABSL_LOCKS_EXCLUDED(mutex_);
  void invoke(const char* keyexpr, bool alive) ABSL_LOCKS_EXCLUDED(mutex_);
  struct Statistics {
    size_t n_messages;
    size_t n_bytes;
  };
  Statistics get_statistics() ABSL_LOCKS_EXCLUDED(mutex_);

 private:
  const std::string keyexpr_;
  imw_liveliness_callback_fn* const callback_;
  void* const user_context_;
  z_owned_subscriber_t zenoh_sub_;
  absl::Mutex mutex_;  // protects internal data structures
  bool active_ ABSL_GUARDED_BY(mutex_) = true;
  size_t n_bytes ABSL_GUARDED_BY(mutex_) = 0;
  size_t n_messages ABSL_GUARDED_BY(mutex_) = 0;
};

}  // namespace intrinsic

#endif  // MIDDLEWARE_ZENOH_IMW_ZENOH_LIVELINESS_SUBSCRIPTION_H_
