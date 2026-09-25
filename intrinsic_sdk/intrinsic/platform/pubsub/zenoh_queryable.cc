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

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "intrinsic/platform/pubsub/adapters/pubsub.pb.h"
#include "intrinsic/platform/pubsub/queryable.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"

namespace intrinsic {

namespace {

// imw_queryable_callback_fn
static void ZenohQueryableCallback(const char* keyexpr, const void* query_bytes,
                                   const size_t query_bytes_len,
                                   const void* query_context,
                                   void* user_context) {
  // Decode query
  intrinsic_proto::pubsub::PubSubQueryRequest request_packet;
  if (!request_packet.ParseFromArray(query_bytes, query_bytes_len)) {
    LOG(ERROR) << absl::StrFormat(
        "Deserializing message failed for query. Key expression: %s", keyexpr);
    return;
  }

  internal::QueryableLink* link =
      static_cast<internal::QueryableLink*>(user_context);
  intrinsic_proto::pubsub::PubSubQueryResponse response_packet =
      link->callback(keyexpr, request_packet);

  // Encode and send response
  std::string reply_message;
  if (!response_packet.SerializeToString(&reply_message)) {
    LOG(ERROR) << absl::StrFormat(
        "Failed to serialize response message for key expression '%s'",
        keyexpr);
    return;
  }
  if (Zenoh().imw_queryable_reply(query_context, keyexpr, reply_message.c_str(),
                                  reply_message.size()) != IMW_OK) {
    LOG(ERROR) << absl::StrFormat(
        "Failed to send reply message for key expression '%s'", keyexpr);
  }
}

}  // namespace

Queryable::Queryable(Queryable&& other) noexcept
    : keyexpr_(std::move(other.keyexpr_)), link_(std::move(other.link_)) {
  other.keyexpr_.clear();
}

Queryable& Queryable::operator=(Queryable&& other) noexcept {
  if (this == &other) return *this;
  Reset();
  keyexpr_ = std::move(other.keyexpr_);
  link_ = std::move(other.link_);
  other.keyexpr_.clear();
  return *this;
}

absl::StatusOr<Queryable> Queryable::Create(
    std::string_view key, internal::GeneralQueryableCallback callback) {
  Queryable queryable(key, std::move(callback));
  if (imw_ret_t rv = Zenoh().imw_create_queryable(
          queryable.GetKeyExpression().data(), &ZenohQueryableCallback,
          queryable.link_.get(), nullptr);
      rv != IMW_OK) {
    queryable.link_.reset();
    return absl::InvalidArgumentError(absl::StrFormat(
        "Failed to create queryable for key '%s' (code %d)", key, rv));
  }
  return queryable;
}

Queryable::~Queryable() { Reset(); }

void Queryable::Reset() {
  // link_ may be nullptr when this is the left-over shell of a moved object
  if (link_) {
    // If this is NOT_INITIALIZED it means that the pubsub system has already
    // been finalized and the queryable is gone anyway
    if (imw_ret_t rv = Zenoh().imw_destroy_queryable(
            GetKeyExpression().data(), &ZenohQueryableCallback, link_.get());
        rv != IMW_OK && rv != IMW_NOT_INITIALIZED) {
      LOG(ERROR) << absl::StrFormat(
          "Failed to destroy queryable for '%s' (code %d)", keyexpr_, rv);
    }
    link_.reset();
    keyexpr_.clear();
  }
}

intrinsic_proto::pubsub::PubSubQueryResponse Queryable::Invoke(
    std::string_view keyexpr,
    const intrinsic_proto::pubsub::PubSubQueryRequest& request_packet) {
  return link_->callback(keyexpr, request_packet);
}

}  // namespace intrinsic
