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

#include "intrinsic/platform/pubsub/zenoh_pubsub_data.h"

#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_config.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"

namespace intrinsic {

PubSubData::PubSubData(std::string_view config_param) {
  std::string config(config_param);
  if (config.empty()) {
    config = intrinsic::GetZenohPeerConfig();
    if (config.empty()) {
      LOG(FATAL) << "Could not get PubSub peer config";
    }
  }
  imw_ret_t ret = Zenoh().imw_init(config.c_str());
  if (ret != IMW_OK) {
    LOG(FATAL) << "Error creating a zenoh session with config " << config;
  }
}

PubSubData::~PubSubData() { Zenoh().imw_fini(); }

}  // namespace intrinsic
