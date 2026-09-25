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

#include <memory>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/platform/pubsub/zenoh_subscription_data.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"

namespace intrinsic {

Subscription::Subscription() = default;

Subscription::Subscription(absl::string_view topic_name,
                           std::unique_ptr<SubscriptionData> subscription_data)
    : topic_name_(topic_name),
      subscription_data_(std::move(subscription_data)) {}

Subscription::Subscription(Subscription&& other)
    : topic_name_(std::move(other.topic_name_)),
      subscription_data_(std::move(other.subscription_data_)) {
  other.topic_name_.clear();
}

Subscription& Subscription::operator=(Subscription&& other) {
  if (this == &other) return *this;
  Unsubscribe();
  topic_name_ = std::move(other.topic_name_);
  subscription_data_ = std::move(other.subscription_data_);
  other.topic_name_.clear();
  return *this;
}

Subscription::~Subscription() { Unsubscribe(); }

void Subscription::Unsubscribe() {
  if (!topic_name_.empty() && subscription_data_ != nullptr) {
    Zenoh().imw_destroy_subscription(
        subscription_data_->prefixed_name.c_str(), zenoh_static_callback,
        subscription_data_->callback_functor.get());
    topic_name_.clear();
  }
}

}  // namespace intrinsic
