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

#ifndef INTRINSIC_PLATFORM_PUBSUB_ZENOH_PUBSUB_DATA_H_
#define INTRINSIC_PLATFORM_PUBSUB_ZENOH_PUBSUB_DATA_H_

#include <string_view>

namespace intrinsic {

class PubSubData {
 public:
  explicit PubSubData(std::string_view config_param = std::string_view());
  ~PubSubData();

  PubSubData(const PubSubData&) = delete;
  PubSubData& operator=(const PubSubData&) = delete;
};

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_ZENOH_PUBSUB_DATA_H_
