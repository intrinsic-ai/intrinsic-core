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

#ifndef INTRINSIC_PLATFORM_PUBSUB_QUERYABLE_H_
#define INTRINSIC_PLATFORM_PUBSUB_QUERYABLE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/message.h"
#include "intrinsic/platform/pubsub/adapters/pubsub.pb.h"

namespace intrinsic {

class Queryable;

namespace internal {
// This is used internally as an implementation-independent callback mechanism.
// Users of the PubSub API will use QueryableCallback
using GeneralQueryableCallback =
    std::function<intrinsic_proto::pubsub::PubSubQueryResponse(
        std::string_view keyexpr,
        const intrinsic_proto::pubsub::PubSubQueryRequest&)>;
// Heap-allocated via std::unique_ptr so the user_context pointer passed to
// imw_create_queryable remains valid when Queryable is moved.
struct QueryableLink {
  GeneralQueryableCallback callback;
};

}  // namespace internal

class Queryable {
 public:
  Queryable() = default;
  Queryable(const Queryable&) = delete;
  Queryable& operator=(const Queryable&) = delete;

  Queryable(Queryable&& other) noexcept;
  Queryable& operator=(Queryable&& other) noexcept;

  virtual ~Queryable();

  static absl::StatusOr<Queryable> Create(
      std::string_view keyexpr, internal::GeneralQueryableCallback callback);

  virtual intrinsic_proto::pubsub::PubSubQueryResponse Invoke(
      std::string_view keyexpr,
      const intrinsic_proto::pubsub::PubSubQueryRequest& request_packet);

  std::string_view GetKeyExpression() { return keyexpr_; }

 private:
  Queryable(std::string_view keyexpr,
            internal::GeneralQueryableCallback callback)
      : keyexpr_(keyexpr),
        link_(std::make_unique<internal::QueryableLink>(
            internal::QueryableLink{.callback = std::move(callback)})) {}

  void Reset();

  std::string keyexpr_;
  std::unique_ptr<internal::QueryableLink> link_;
};

// This struct is passed to Queryable callbacks as first argument to pass
// additional context, e.g., tracing information.
struct QueryableContext {
  uint64_t trace_id;  // This can be used to keep tracing in a request
  uint64_t span_id;  // The parent span ID to associate calls in this request to
};

// Helper struct to deduce the types from the QueryableCallback lambda.
template <typename CallbackType>
struct QueryableCallbackTraits
    : public QueryableCallbackTraits<decltype(&CallbackType::operator())> {};

template <typename ClassType, typename RequestT, typename ResponseT>
struct QueryableCallbackTraits<absl::Status (ClassType::*)(
    std::string_view, const QueryableContext&, RequestT, ResponseT) const> {
  using RequestType = std::remove_cvref_t<RequestT>;
  using ResponseType = std::remove_cvref_t<ResponseT>;
  static constexpr bool kHasRequest = true;
  static_assert(std::is_const_v<std::remove_reference_t<RequestT>> &&
                    std::is_reference_v<RequestT>,
                "The request parameter needs to be a const reference.");
  static_assert(std::is_reference_v<ResponseT>,
                "The response parameter needs to be a reference.");
  static_assert(!std::is_const_v<std::remove_reference_t<ResponseT>>,
                "The response parameter needs cannot be a const argument.");
  static_assert(std::is_base_of_v<google::protobuf::Message, RequestType>,
                "Request argument must be a proto message type");
  static_assert(std::is_base_of_v<google::protobuf::Message, ResponseType>,
                "Response argument must be a proto message type");
};

template <typename ClassType, typename ResponseT>
struct QueryableCallbackTraits<absl::Status (ClassType::*)(
    std::string_view, const QueryableContext&, ResponseT) const> {
  using ResponseType = std::remove_cvref_t<ResponseT>;
  static constexpr bool kHasRequest = false;
  static_assert(std::is_reference_v<ResponseT>,
                "The response parameter needs to be a reference.");
  static_assert(!std::is_const_v<std::remove_reference_t<ResponseT>>,
                "The response parameter needs cannot be a const argument.");
  static_assert(std::is_base_of_v<google::protobuf::Message, ResponseType>,
                "Response argument must be a proto message type");
};

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_QUERYABLE_H_
