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

#ifndef MIDDLEWARE_ZENOH_IMW_ZENOH_DATA_CALLBACK_CONTEXT_H_
#define MIDDLEWARE_ZENOH_IMW_ZENOH_DATA_CALLBACK_CONTEXT_H_

#include <memory>

namespace intrinsic {

class IMWZenohSubscription;

class IMWZenohDataCallbackContext {
 public:
  explicit IMWZenohDataCallbackContext(
      std::weak_ptr<IMWZenohSubscription> subscription);
  ~IMWZenohDataCallbackContext();

  IMWZenohDataCallbackContext(IMWZenohDataCallbackContext&& other) = default;
  IMWZenohDataCallbackContext(const IMWZenohDataCallbackContext&) = delete;
  IMWZenohDataCallbackContext& operator=(const IMWZenohDataCallbackContext&) =
      delete;

  const std::weak_ptr<IMWZenohSubscription>& get_subscription() const {
    return subscription_;
  }

 private:
  std::weak_ptr<IMWZenohSubscription> subscription_;
};

}  // namespace intrinsic

#endif  // MIDDLEWARE_ZENOH_IMW_ZENOH_DATA_CALLBACK_CONTEXT_H_
