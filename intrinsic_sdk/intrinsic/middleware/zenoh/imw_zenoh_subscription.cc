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

#include <string>
#include <utility>  // for std::move

#include "absl/log/log.h"

using std::string;

namespace intrinsic {

IMWZenohSubscription::IMWZenohSubscription(
    const std::string& keyexpr, imw_subscription_callback_fn* callback,
    void* user_context)
    : keyexpr_(keyexpr), n_bytes(0), n_messages(0) {
  z_internal_subscriber_null(&zenoh_sub_);
  callbacks_.push_back(std::make_pair(callback, user_context));
}

IMWZenohSubscription::~IMWZenohSubscription() {
  // by the time this destructor fires, the subscription must have
  // previously been released by the owning IMWZenoh object that has
  // access to the Zenoh session. We'll just sanity-check it here.
  if (z_internal_check(zenoh_sub_)) {
    LOG(ERROR) << "~IMWZenohSubscription had a valid sub!";
  }
}

void IMWZenohSubscription::add_callback(imw_subscription_callback_fn* fptr,
                                        void* user_context) {
  absl::MutexLock lock(&mutex_);
  callbacks_.push_back(std::make_pair(fptr, user_context));
}

bool IMWZenohSubscription::clear_callbacks() {
  mutex_.ForgetDeadlockInfo();
  absl::MutexLock lock(&mutex_);
  callbacks_.clear();
  return true;
}

bool IMWZenohSubscription::remove_callback(imw_subscription_callback_fn* fptr,
                                           const void* user_context) {
  // The deadlock cycle detector does not like this, but I am currently
  // convinced this is safe.
  mutex_.ForgetDeadlockInfo();
  absl::MutexLock lock(&mutex_);
  for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it) {
    if (it->first == fptr && it->second == user_context) {
      callbacks_.erase(it);
      return true;
    }
  }
  LOG(ERROR) << "Tried to remove callback to " << keyexpr_
             << " but could not find it";
  return false;
}

void IMWZenohSubscription::invoke_callbacks(
    const char* keyexpr, const void* blob, size_t blob_len,
    const imw_message_metadata_t& /*metadata*/) {
  // People writing callbacks probably are assuming they are non-reentrant,
  // so we'll use a mutex to make sure that's true!
  absl::MutexLock lock(&mutex_);
  n_messages++;
  n_bytes += blob_len;
  for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it) {
    it->first(keyexpr, blob, blob_len, it->second);
  }
}

IMWZenohSubscription::Statistics IMWZenohSubscription::get_statistics() {
  // Lock the mutex to avoid tearing if a messages arrives at this very
  // instant, causing a statistics field update as we are reading it.
  absl::MutexLock lock(&mutex_);
  Statistics s;
  s.n_messages = n_messages;
  s.n_bytes = n_bytes;
  return s;
}

}  // namespace intrinsic
