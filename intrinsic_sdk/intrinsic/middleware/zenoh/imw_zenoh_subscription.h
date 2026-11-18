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

#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/synchronization/mutex.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"
#include "zenoh.h"  // NOLINT(build/include_subdir)

namespace intrinsic {

class IMWZenohSubscription {
 public:
  IMWZenohSubscription(const std::string& keyexpr,
                       imw_subscription_callback_fn* callback,
                       void* user_context);

  IMWZenohSubscription(IMWZenohSubscription&& other) = delete;
  IMWZenohSubscription(const IMWZenohSubscription&) = delete;
  IMWZenohSubscription& operator=(const IMWZenohSubscription&) = delete;

  ~IMWZenohSubscription();

  void set_zenoh_sub(z_owned_subscriber_t zenoh_sub) { zenoh_sub_ = zenoh_sub; }
  z_owned_subscriber_t& get_zenoh_sub() { return zenoh_sub_; }
  const std::string& get_keyexpr() const { return keyexpr_; }
  void add_callback(imw_subscription_callback_fn* fptr, void* user_context);
  bool remove_callback(imw_subscription_callback_fn* fptr,
                       const void* user_context);
  bool clear_callbacks();
  bool is_empty() const { return callbacks_.empty(); }
  void invoke_callbacks(const char* keyexpr, const void* blob, size_t blob_len,
                        const imw_message_metadata_t& metadata);
  struct Statistics {
    size_t n_messages;
    size_t n_bytes;
  };
  Statistics get_statistics();

 private:
  std::string keyexpr_;
  std::vector<std::pair<imw_subscription_callback_fn*, void*>> callbacks_;
  z_owned_subscriber_t zenoh_sub_;
  absl::Mutex mutex_;  // protects internal data structures
  size_t n_bytes;
  size_t n_messages;
};

}  // namespace intrinsic

#endif  // MIDDLEWARE_ZENOH_IMW_ZENOH_SUBSCRIPTION_H_
