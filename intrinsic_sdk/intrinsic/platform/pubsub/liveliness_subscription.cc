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

#include "intrinsic/platform/pubsub/liveliness_subscription.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "intrinsic/platform/pubsub/liveliness_subscription_data.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"

namespace intrinsic {

LivelinessSubscription::LivelinessSubscription() = default;

LivelinessSubscription::LivelinessSubscription(
    absl::string_view key_expression,
    std::unique_ptr<LivelinessSubscriptionData> subscription_data)
    : key_expression_(key_expression),
      subscription_data_(std::move(subscription_data)) {}

LivelinessSubscription::LivelinessSubscription(LivelinessSubscription&& other)
    : key_expression_(std::move(other.key_expression_)),
      subscription_data_(std::move(other.subscription_data_)) {
  other.key_expression_.clear();
}

LivelinessSubscription& LivelinessSubscription::operator=(
    LivelinessSubscription&& other) {
  if (this == &other) return *this;
  Unsubscribe();
  key_expression_ = std::move(other.key_expression_);
  subscription_data_ = std::move(other.subscription_data_);
  other.key_expression_.clear();
  return *this;
}

LivelinessSubscription::~LivelinessSubscription() { Unsubscribe(); }

void LivelinessSubscription::Unsubscribe() {
  if (!key_expression_.empty() && subscription_data_ != nullptr) {
    Zenoh().imw_destroy_liveliness_subscription(
        subscription_data_->key_expression.c_str(),
        zenoh_static_liveliness_callback,
        subscription_data_->callback_functor.get());
    key_expression_.clear();
  }
}

}  // namespace intrinsic
