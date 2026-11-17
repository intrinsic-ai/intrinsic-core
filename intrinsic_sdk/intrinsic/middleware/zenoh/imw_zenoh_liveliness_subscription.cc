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

#include "intrinsic_sdk/intrinsic/middleware/zenoh/imw_zenoh_liveliness_subscription.h"

#include <string>

#include "absl/log/log.h"

namespace intrinsic {

IMWZenohLivelinessSubscription::IMWZenohLivelinessSubscription(
    const std::string& keyexpr, imw_liveliness_callback_fn* callback,
    void* user_context)
    : keyexpr_(keyexpr), callback_(callback), user_context_(user_context) {
  z_internal_subscriber_null(&zenoh_sub_);
}

IMWZenohLivelinessSubscription::~IMWZenohLivelinessSubscription() {
  // by the time this destructor fires, the subscription must have
  // previously been released by the owning IMWZenoh object that has
  // access to the Zenoh session. We'll just sanity-check it here.
  if (z_internal_check(zenoh_sub_)) {
    LOG(ERROR) << "~IMWZenohLivelinessSubscription had a valid sub!";
  }
}

void IMWZenohLivelinessSubscription::disable() {
  absl::MutexLock lock(&mutex_);
  active_ = false;
}

void IMWZenohLivelinessSubscription::invoke(const char* keyexpr, bool alive) {
  // People writing callbacks probably are assuming they are non-reentrant,
  // so we'll use a mutex to make sure that's true!
  absl::MutexLock lock(&mutex_);
  if (!active_ || callback_ == nullptr) {
    return;
  }
  n_messages++;
  callback_(keyexpr, alive, user_context_);
}

IMWZenohLivelinessSubscription::Statistics
IMWZenohLivelinessSubscription::get_statistics() {
  // Lock the mutex to avoid tearing if a messages arrives at this very
  // instant, causing a statistics field update as we are reading it.
  absl::MutexLock lock(&mutex_);
  Statistics s;
  s.n_messages = n_messages;
  s.n_bytes = n_bytes;
  return s;
}

}  // namespace intrinsic
