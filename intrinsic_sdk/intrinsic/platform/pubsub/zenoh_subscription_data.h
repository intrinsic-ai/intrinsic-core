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

#ifndef INTRINSIC_PLATFORM_PUBSUB_ZENOH_SUBSCRIPTION_DATA_H_
#define INTRINSIC_PLATFORM_PUBSUB_ZENOH_SUBSCRIPTION_DATA_H_

#include <memory>
#include <string>

#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"

namespace intrinsic {

struct SubscriptionData {
  std::unique_ptr<imw_callback_functor_t> callback_functor;
  std::unique_ptr<imw_message_callback_functor_t> message_callback_functor;
  std::string prefixed_name;
};

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_ZENOH_SUBSCRIPTION_DATA_H_
