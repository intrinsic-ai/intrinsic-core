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

#ifndef MIDDLEWARE_ZENOH_IMW_ZENOH_QUERY_CONTEXT_H_
#define MIDDLEWARE_ZENOH_IMW_ZENOH_QUERY_CONTEXT_H_

#include <string>

#include "intrinsic_sdk/intrinsic/middleware/imw.h"

namespace intrinsic {

class IMWZenoh;

class IMWZenohQueryContext {
 public:
  IMWZenohQueryContext(IMWZenoh* imw_zenoh_instance, const char* keyexpr,
                       imw_query_callback_fn* callback,
                       imw_query_message_callback_fn* message_callback,
                       imw_query_on_done_callback_fn* on_done,
                       void* user_context, imw_query_options_t* options)
      : imw_zenoh_instance_(imw_zenoh_instance),
        keyexpr_(keyexpr != nullptr ? keyexpr : ""),
        callback_(callback),
        message_callback_(message_callback),
        on_done_(on_done),
        user_context_(user_context) {
    if (options != nullptr) {
      options_ = *options;
    }
  }

  IMWZenoh* imw_zenoh_instance_;
  std::string keyexpr_;
  imw_query_callback_fn* callback_;
  imw_query_message_callback_fn* message_callback_;
  imw_query_on_done_callback_fn* on_done_;
  void* user_context_;
  imw_query_options_t options_;
};

}  // namespace intrinsic

#endif  // MIDDLEWARE_ZENOH_IMW_ZENOH_QUERY_CONTEXT_H_
