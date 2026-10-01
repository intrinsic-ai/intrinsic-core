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

#ifndef INTRINSIC_PLATFORM_PUBSUB_PUBSUB_H_
#define INTRINSIC_PLATFORM_PUBSUB_PUBSUB_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "google/rpc/status.pb.h"
#include "intrinsic/platform/pubsub/adapters/pubsub.pb.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/liveliness_query.h"
#include "intrinsic/platform/pubsub/liveliness_subscription.h"
#include "intrinsic/platform/pubsub/publisher.h"
#include "intrinsic/platform/pubsub/pubsub_callbacks.h"
#include "intrinsic/platform/pubsub/queryable.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/platform/pubsub/topic_config.h"
#include "intrinsic/util/proto/type_url.h"
#include "intrinsic/util/status/status_conversion_rpc.h"
#include "intrinsic/util/status/status_macros.h"

// The PubSub class implements an interface to a publisher-subscriber
// system, a one-to-many communication bus that allows sending protocol buffers
// across a non-real-time context with a relatively low-latency.
// Subscribers receive messages published to a topic serially in the order
// that they were published. A Subscriber may receive messages published to a
// topic before the time of subscription.
//
// Creating a PubSub client:
//
//   #include "intrinsic/platform/pubsub/pubsub.h"
//
//   PubSub pubsub;
//
// Publishing a message (see go/intrinsic-dds-topic-naming-design for naming
// convention):
//
//   INTR_ASSIGN_OR_RETURN(auto publisher,
//                     pubsub->CreatePublisher(response_topic, TopicConfig()));
//   TestMessage message;
//   INTR_RETURN_IF_ERROR(publisher.Publish("/test/my_topic", message));
//
// Note: Be careful to not destroy the PubSub instance after creating a
// Publisher.
//
// Creating a Subscription to a topic:
//
//   INTR_ASSIGN_OR_RETURN(
//      auto sub,
//      pubsub.CreateSubscription<proto::TestMessage>("/test/my_topic",
//         [](const TestMessage& message) {
//           // Here we process the received message.
//         },
//         [](absl::string_view packet, absl::Status error) {
//           // Here we handle receiving of an invalid message (Unimplemented).
//      });
// Note: Be careful to not destroy the PubSub instance after creating a
// Subscription.
//
namespace intrinsic {

class PubSubData;

// This class is thread-safe.
class PubSub {
 public:
  PubSub();
  explicit PubSub(absl::string_view participant_name);
  explicit PubSub(absl::string_view participant_name, absl::string_view config);

  PubSub(const PubSub&) = delete;
  PubSub& operator=(const PubSub&) = delete;
  PubSub(PubSub&&) = default;
  PubSub& operator=(PubSub&&) = default;
  virtual ~PubSub();

  // Hints that the underlying Zenoh session should be torn down when no PubSub
  // instances or child entities (publishers, subscriptions, queryables)
  // remain. If the session is currently unused, it is torn down immediately.
  // Otherwise, a sticky per-session latch is armed and teardown is deferred
  // until the last active PubSub instance and child entity are destroyed.
  static void DestroySessionWhenUnused();

  absl::StatusOr<Publisher> CreatePublisher(absl::string_view topic,
                                            const TopicConfig& config) const;

  // Creates a subscription which invokes the specified callback when a package
  // is received on the topic. This function can only be invoked for proto
  // messages but not for google::protobuf::Message itself.
  //
  // Example 1: Creating a subscription where a lambda is passed in requires
  //   the caller to explicitly specify the template parameter T since it cannot
  //   be deduced from the lambda.
  //
  // PubSub pubsub;
  // INTR_ASSIGN_OR_RETURN(auto subscription,
  // pubsub.CreateSubscription<MyProto>(
  //   "some/topic", {}, [](const MyProto& my_proto){}));
  //
  // Example 2: Creating a subscription from a std::function allows us to
  //   automatically deduce the template parameter T.
  //
  // std::function<void(const MyProto&)> callback = [](const MyProto&){};
  // PubSub pubsub;
  // INTR_ASSIGN_OR_RETURN(auto subscription, pubsub.CreateSubscription(
  //   "some/topic", {}, callback));
  template <typename T>
  absl::StatusOr<Subscription> CreateSubscription(
      absl::string_view topic, const TopicConfig& config,
      SubscriptionOkCallback<T> msg_callback,
      SubscriptionErrorCallback error_callback = {}) const {
    static_assert(!std::is_same_v<google::protobuf::Message, T>,
                  "The wrong overload has been called. Please use the "
                  "CreateSubscription() overload which takes an exemplar (a "
                  "sample message) during its call.");
    return CreateSubscription(topic, config, T::default_instance(),
                              std::move(msg_callback),
                              std::move(error_callback));
  }

  // Creates a subscription using an exemplar, i.e. a sample proto message.
  //
  // This function requires an exemplar (a sample message) to be passed in
  // which can hold the payload of a PubSubPacket. The exemplar is used during
  // each callback to create a new proto message, to extract the payload from
  // the PubSubPacket and it is then passed to the actual callback function.
  template <typename T>
  absl::StatusOr<Subscription> CreateSubscription(
      absl::string_view topic, const TopicConfig& config, const T& exemplar,
      SubscriptionOkCallback<T> msg_callback,
      SubscriptionErrorCallback error_callback = {}) const {
    static_assert(std::is_base_of_v<google::protobuf::Message, T>,
                  "Protocol buffers are the only supported serialization "
                  "format for PubSub.");

    // This payload is shared between callbacks and may be read from multiple
    // threads. We need a shared_ptr here because a std::function must be
    // copyable.
    std::shared_ptr<T> shared_payload(exemplar.New());

    // The message callback is never copied. It is merely moved to this helper
    // lambda which is itself moved to the subscription class.
    auto package_to_payload =
        [callback = std::move(msg_callback),
         error_callback = std::move(error_callback),
         shared_payload = std::move(shared_payload)](
            const intrinsic_proto::pubsub::PubSubPacket& packet) {
          // Create a local copy of the shared payload which we can safely
          // modify in different threads.
          std::unique_ptr<T> payload(shared_payload->New());
          if (!packet.payload().UnpackTo(payload.get())) {
            HandleError(error_callback, packet, *payload);
            return;
          }
          callback(*payload);
        };
    return CreateSubscription(topic, config, std::move(package_to_payload));
  }

  // Creates a subscription for a raw PubSubPacket. This kind of subscription is
  // useful for filtering packets or processing them otherwise without the need
  // to deserialize the data.
  absl::StatusOr<Subscription> CreateSubscription(
      absl::string_view topic, const TopicConfig& config,
      SubscriptionOkCallback<intrinsic_proto::pubsub::PubSubPacket>
          msg_callback) const;

  absl::StatusOr<Subscription> CreateSubscription(
      absl::string_view topic, const TopicConfig& config,
      SubscriptionOkExpandedCallback<intrinsic_proto::pubsub::PubSubPacket>
          msg_callback) const;

  // Test if a key expression is "canonical", meaning that it has a valid
  // combination of wildcards, no illegal characters, no trailing slash, etc.
  bool KeyexprIsCanon(absl::string_view keyexpr) const;

  // Test if a key expression intersects another key expression. Helpful when
  // wildcards are involved and this becomes a non-trivial calculation.
  // Returns an error if any input is non-canonical.
  absl::StatusOr<bool> KeyexprIntersects(absl::string_view left,
                                         absl::string_view right) const;

  // Test if a key expression includes all keys of another key expression.
  // Helpful when wildcards are involved and this becomes non-trivial.
  // Returns an error if any input is non-canonical.
  absl::StatusOr<bool> KeyexprIncludes(absl::string_view left,
                                       absl::string_view right) const;

  // Returns a KeyValueStore implementation. This is only supported when
  // building with Zenoh. See go/intrinsic-kv-store for more details.
  absl::StatusOr<intrinsic::KeyValueStore> KeyValueStore(
      std::optional<std::string> prefix_override = std::nullopt) const;

  // Returns if the enabled pubsub implementation supports queryables.
  bool SupportsQueryables() const;

  // Creates a new typed Queryable.
  //
  // The callback is invoked for requests to the queryable. There are two forms
  // of call types: with and without a request proto. With a request makes the
  // queryable similar to a service call, without a request it's more like a
  // storage module to simply retrieve an item by key. For the service form, the
  // lambda is called with context, request, and response, for the storage
  // variant only with context and response. Both functions return an
  // absl::Status to indicate success or failure (ideally containing an
  // ExtendedStatus).
  //
  // The queryable callback must adhere to one of the following signatures:
  //
  // 1. Service variant
  // std::function<absl::Status(const QueryableContext&,
  //                            const RequestType&,
  //                            ResponseType&)>
  // where RequestType and ResponseType must be proto message types.
  //
  // 2. Retrievable variant
  // std::function<absl::Status(const QueryableContext&, ResponseType&)>
  // where ResponseType must be a proto message type.
  //
  // Note: If SupportsQueryable() returns false this will return an
  // absl::UnimplementedError.
  template <typename CallbackType>
  absl::StatusOr<Queryable> CreateQueryable(absl::string_view keyexpr,
                                            CallbackType callback) {
    using Traits = QueryableCallbackTraits<CallbackType>;
    using ResponseT = typename Traits::ResponseType;

    // Note that "if constexpr" is evaluated at *compile*-time. So either this
    // or the other branch is compiled. This builds on the queryable traits and
    // SFINAE (try to use template with request, if that fails substitution of
    // template arguments try the other).
    if constexpr (Traits::kHasRequest) {
      using RequestT = typename Traits::RequestType;
      auto inner_callback =
          [callback, registered_keyexpr = std::string(keyexpr)](
              std::string_view callback_keyexpr,
              const intrinsic_proto::pubsub::PubSubQueryRequest&
                  request_packet) {
            RequestT request;
            intrinsic_proto::pubsub::PubSubQueryResponse response_packet;

            if (!request_packet.request().UnpackTo(&request)) {
              *response_packet.mutable_error() =
                  ToGoogleRpcStatus(absl::InternalError(absl::StrFormat(
                      "Failed to deserialize query message for key '%s' "
                      "(registered key expression: %s, got type URL: %s, "
                      "expected message type: %s)",
                      callback_keyexpr, registered_keyexpr,
                      request_packet.request().type_url(),
                      RequestT::descriptor()->full_name())));
              return response_packet;
            }

            const QueryableContext context{
                .trace_id = request_packet.trace_id(),
                .span_id = request_packet.span_id()};

            ResponseT response;
            absl::Status response_status =
                callback(callback_keyexpr, context, request, response);

            if (!response_status.ok()) {
              *response_packet.mutable_error() =
                  ToGoogleRpcStatus(response_status);
            } else {
              response_packet.mutable_response()->PackFrom(response);
            }
            return response_packet;
          };

      INTR_ASSIGN_OR_RETURN(Queryable queryable,
                            CreateQueryableImpl(keyexpr, inner_callback));
      LOG(INFO) << absl::StrFormat(
          "Queryable (callable) listening for '%s' (request: %s, response: %s)",
          keyexpr, RequestT::descriptor()->full_name(),
          ResponseT::descriptor()->full_name());
      return queryable;

    } else {  // has no request
      auto inner_callback =
          [callback, registered_keyexpr = std::string(keyexpr)](
              std::string_view callback_keyexpr,
              const intrinsic_proto::pubsub::PubSubQueryRequest&
                  request_packet) {
            intrinsic_proto::pubsub::PubSubQueryResponse response_packet;

            const QueryableContext context{
                .trace_id = request_packet.trace_id(),
                .span_id = request_packet.span_id()};

            ResponseT response;
            absl::Status response_status =
                callback(callback_keyexpr, context, response);

            if (!response_status.ok()) {
              *response_packet.mutable_error() =
                  ToGoogleRpcStatus(response_status);
            } else {
              response_packet.mutable_response()->PackFrom(response);
            }
            return response_packet;
          };

      INTR_ASSIGN_OR_RETURN(Queryable queryable,
                            CreateQueryableImpl(keyexpr, inner_callback));
      LOG(INFO) << absl::StrFormat(
          "Queryable (retrievable) listening for '%s' (response: %s)", keyexpr,
          ResponseT::descriptor()->full_name());
      return queryable;
    }
  }

  struct QueryOptions {
    std::optional<uint64_t> trace_id;
    std::optional<uint64_t> span_id;
    std::optional<absl::Duration> timeout;
  };

  // Gets from one specific queryable (identified by key, NOT key expr).
  // Calls the queryable with the given request. This will block until a reply
  // is received. Returns the response or a status on error.
  //
  // Invoke with the expected response type as template argument like this:
  // INTR_ASSIGN_OR_RETURN(auto response,
  //                       pubsub.Query<my_package::MyResponse>("some/key",
  //                                                            request));
  //
  // Note: If SupportsQueryable() returns false this will return an
  // absl::UnimplementedError.
  template <typename ResponseT, typename RequestT>
  absl::StatusOr<ResponseT> CallOne(absl::string_view key,
                                    const RequestT& request,
                                    const QueryOptions& options = {}) {
    static_assert(std::is_base_of_v<google::protobuf::Message, RequestT>,
                  "Request must be a proto message");
    static_assert(std::is_base_of_v<google::protobuf::Message, ResponseT>,
                  "Response must be a proto message");

    intrinsic_proto::pubsub::PubSubQueryRequest request_packet =
        PrepareRequestPacket(request, options);

    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::pubsub::PubSubQueryResponse response_packet,
        GetOneImpl(key, request_packet, options));

    if (response_packet.has_error()) {
      return ToAbslStatus(response_packet.error());
    }

    ResponseT response;
    if (!response_packet.response().UnpackTo(&response)) {
      return absl::InvalidArgumentError("Failed to unpack response");
    }
    return response;
  }

  // Gets from one specific queryable (identified by key, NOT key expr).
  // Calls the queryable with the given request. This will block until a reply
  // is received. Returns the response or a status on error.
  //
  // Invoke with the expected response type as template argument like this:
  // INTR_ASSIGN_OR_RETURN(auto response,
  //                       pubsub.Query<my_package::MyResponse>("some/key",
  //                                                            request));
  //
  // Note: If SupportsQueryable() returns false this will return an
  // absl::UnimplementedError.
  template <typename ResponseT>
  absl::StatusOr<ResponseT> GetOne(absl::string_view key,
                                   const QueryOptions& options = {}) {
    static_assert(std::is_base_of_v<google::protobuf::Message, ResponseT>,
                  "Response must be a proto message");

    intrinsic_proto::pubsub::PubSubQueryRequest request_packet =
        PrepareRequestPacket(options);

    INTR_ASSIGN_OR_RETURN(
        intrinsic_proto::pubsub::PubSubQueryResponse response_packet,
        GetOneImpl(key, request_packet, options));

    if (response_packet.has_error()) {
      return ToAbslStatus(response_packet.error());
    }

    ResponseT response;
    if (!response_packet.response().UnpackTo(&response)) {
      return absl::InvalidArgumentError("Failed to unpack response");
    }
    return response;
  }

  using GetResult = absl::StatusOr<std::unique_ptr<google::protobuf::Message>>;

  template <typename RequestT, typename... ResponseT>
  absl::StatusOr<std::vector<GetResult>> Call(absl::string_view keyexpr,
                                              const RequestT& request,
                                              const QueryOptions& options = {});

  template <typename... ResponseT>
  absl::StatusOr<std::vector<GetResult>> Get(absl::string_view keyexpr,
                                             const QueryOptions& options = {});

  // Creates a subscription to liveliness notifications.
  //
  // Liveliness is a feature of Zenoh that allows participants of a Zenoh
  // network to keep track of each other's availability. Participants announce
  // their availability by declaring a liveliness token on a key expression
  // (see `DeclareLivelinessToken` method). Other participants can subscribe
  // to notifications about changes in the availability status by invoking
  // `CreateLivelinessSubscription`. Subscribers will receive notifications
  // when the following events occur:
  //  - The participant that declared a liveliness token becomes unavailable
  //    for any reason, such as a network partition or a process crash.
  //  - The participant that declared a liveliness token drops that token
  //    (see `DropLivelinessToken` method).
  //  - The participant that was unavailable becomes available again.
  //
  // Parameters:
  //  - keyexpr: Key expression for matching liveliness tokens. May contain
  //      wildcards. Subscribers will receive notifications about tokens
  //      declared on all key expressions that match `keyexpr`.
  //  - notify_about_existing_tokens: whether to notify subscribers about
  //      liveliness tokens that were declared before the subscription was
  //      created.
  //  - liveliness_callback: called when status of a liveliness token changes.
  //
  // Subscribing to liveliness notifications:
  // ----------------------------------------
  //
  // INTR_ASSIGN_OR_RETURN(
  //   LivelinessSubscription subscription,
  //   pubsub.CreateLivelinessSubscription(
  //     "workcells/**",
  //     true,
  //     [](std::string_view key, bool alive) {
  //       LOG(INFO) << "Key: " << key << "; alive: " << alive;
  //     }));
  //
  // This subscription will receive notifications when availability of
  // any liveliness token that matches "workcells/**" changes. Because the
  // `notify_about_existing_tokens` parameter is true, the subscription will
  // also receive notifications about currently available tokens that match
  // "workcells/**".
  //
  // Unsubscribing from notifications:
  // ---------------------------------
  //
  //  subscription.Unsubscribe();
  //
  absl::StatusOr<LivelinessSubscription> CreateLivelinessSubscription(
      absl::string_view keyexpr, bool notify_about_existing_tokens,
      LivelinessCallback liveliness_callback) const;

  // Declares a liveliness token on the given key expression.
  //
  // Liveliness tokens allow participants of a Zenoh network to announce their
  // availability to other participants.
  //
  // Declaring a liveliness token:
  // -----------------------------
  //
  //  INTR_RETURN_IF_ERROR(pubsub.DeclareLivelinessToken("workcells/node-123"));
  //
  // Once the token is declared, participants who subscribed to
  // "workcells/node-123" or a broader key expression (such as "workcells/**"),
  // will receive notifications about availability of this participant.
  absl::Status DeclareLivelinessToken(absl::string_view keyexpr);

  // Drops previously declared liveliness token on the given key expression.
  //
  // Subscribers to any key expression that matches `keyexpr` will be notified
  // that `keyexpr` is no longer available. More specifically, their
  // `LivelinessCallback` will be called with the following parameters:
  //  - key: same as `keyexpr`
  //  - alive: false.
  //
  // Dropping a liveliness token:
  // -----------------------------
  //
  //  INTR_RETURN_IF_ERROR(pubsub.DropLivelinessToken("workcells/node-123"));
  //
  absl::Status DropLivelinessToken(absl::string_view keyexpr);

  // Fetches a list of currently available liveliness tokens that match the
  // given key expression. Returns immediately. Tokens are passed to the
  // caller via callbacks.
  //
  // Callbacks are called on different threads. Tokens are received in arbitrary
  // order (not necessarily in the order they were declared).
  //
  // Parameters:
  //  - keyexpr: key expression for matching liveliness tokens.
  //  - callback: called for each matching token. Takes the key expression on
  //      which the token was declared.
  //  - on_done: called when all matching tokens are found. Takes `keyexpr` as
  //      a parameter.
  //
  // Returns:
  //  On success, returns a `LivelinessQuery` object. The caller must keep it
  //  alive until `on_done` returns.
  //
  // WARNING: both callbacks (`callback` and `on_done`) must return as quickly
  // as possible. Long-running callbacks may affect results of this
  // `LivelinessGet` call (some callbacks may be skipped). They may also affect
  // other clients of the PubSub API (e.g. PubSub may start dropping messages).
  // If the total duration of callbacks invoked by the same `LivelinessGet`
  // query exceeds 10 minutes, then no more callbacks will be called after 10
  // minutes.
  absl::StatusOr<LivelinessQuery> LivelinessGet(
      absl::string_view keyexpr, LivelinessGetCallback callback,
      LivelinessGetOnDoneCallback on_done);

  // Synchronously fetches a list of currently available liveliness tokens that
  // match the given key expression.
  //
  // Parameters:
  //  - keyexpr: key expression for matching liveliness tokens.
  //
  // Returns a list of available liveliness tokens.
  //
  // Example:
  // --------
  //
  // INTR_ASSIGN_OR_RETURN(
  //   std::vector<std::string> tokens,
  //   pubsub.LivelinessGetAllSynchronous("workcells/**", absl::Seconds(10)));
  //
  // This call will return a list of liveliness tokens that meet the following
  // requirements:
  //  - Were declared before `LivelinessGetAllSynchronous` was called.
  //  - Currently available to the caller of `LivelinessGetAllSynchronous`.
  absl::StatusOr<std::vector<std::string>> LivelinessGetAllSynchronous(
      absl::string_view keyexpr);

 private:
  // A helper function invoked by LivelinessGetAllSynchronous when it
  // needs to wait for liveliness tokens. Can be overriden by subclasses
  // that need to perform additional actions before or after wait.
  virtual void WaitForLivelinessTokens(std::function<void()> wait_fn) {
    wait_fn();
  }

  static void HandleError(const SubscriptionErrorCallback& error_callback,
                          const intrinsic_proto::pubsub::PubSubPacket& packet,
                          const google::protobuf::Message& payload) {
    if (error_callback == nullptr) {
      LOG(ERROR) << absl::StrCat(packet)
                 << absl::InvalidArgumentError(absl::StrCat(
                        "Expected payload of type ", payload.GetTypeName(),
                        " but got ", packet.payload().type_url()));
      return;
    }
    error_callback(absl::StrCat(packet),
                   absl::InvalidArgumentError(absl::StrCat(
                       "Expected payload of type ", payload.GetTypeName(),
                       " but got ", packet.payload().type_url())));
  }

  static intrinsic_proto::pubsub::PubSubQueryRequest PrepareRequestPacket(
      const QueryOptions& options) {
    intrinsic_proto::pubsub::PubSubQueryRequest request_packet;
    if (options.trace_id.has_value()) {
      request_packet.set_trace_id(*options.trace_id);
    }
    if (options.span_id.has_value()) {
      request_packet.set_span_id(*options.span_id);
    }
    return request_packet;
  }

  static intrinsic_proto::pubsub::PubSubQueryRequest PrepareRequestPacket(
      const google::protobuf::Message& request, const QueryOptions& options) {
    intrinsic_proto::pubsub::PubSubQueryRequest request_packet =
        PrepareRequestPacket(options);
    request_packet.mutable_request()->PackFrom(request);
    return request_packet;
  }

  absl::StatusOr<intrinsic_proto::pubsub::PubSubQueryResponse> GetOneImpl(
      absl::string_view key,
      const intrinsic_proto::pubsub::PubSubQueryRequest& request,
      const QueryOptions& options);

  absl::StatusOr<std::vector<intrinsic_proto::pubsub::PubSubQueryResponse>>
  GetImpl(absl::string_view keyexpr,
          const intrinsic_proto::pubsub::PubSubQueryRequest& request,
          const QueryOptions& options);

  absl::StatusOr<Queryable> CreateQueryableImpl(
      absl::string_view keyexpr, internal::GeneralQueryableCallback callback);

  // We use a shared_ptr here because it allows us to auto generate the
  // destructor even when PubSubData is an incomplete type.
  std::shared_ptr<PubSubData> data_;
};

template <typename TupleType, size_t... I>
PubSub::GetResult TryConvertResponseProtoForTypes(
    const intrinsic_proto::pubsub::PubSubQueryResponse& response_packet,
    std::index_sequence<I...> /* unused */) {
  // On error, just return an error result
  if (response_packet.has_error()) {
    return ToAbslStatus(response_packet.error());
  }

  bool converted_ok = false;
  std::unique_ptr<google::protobuf::Message> response;

  // This is a fold expression "(lambda, ...)" that is expanded once for each I
  // (template argument). What we do is get the type at position I and then see
  // if we can unpack the response Any proto into that message type. If we can,
  // we mark conversion as ok and the remaining tries will just bail out
  // immediately.
  (
      [&] {
        if (converted_ok) return;
        // Try to convert Any proto to given message, if this fails, recurse
        TupleType args;
        using M = std::remove_reference_t<decltype(std::get<I>(args))>;

        std::unique_ptr<M> response_candidate =
            absl::WrapUnique(M::default_instance().New());
        if (response_packet.response().UnpackTo(response_candidate.get())) {
          response = std::move(response_candidate);
          converted_ok = true;
        }
      }(),
      ...);

  if (converted_ok) {
    return response;
  }

  // Conversion didn't work, we got a proto of a type we didn't expect. Return
  // an error.

  // This fold expression goes over the expected types to note the proto's full
  // name, just to show it to the user.
  std::vector<absl::string_view> expected_types;
  (
      [&] {
        TupleType args;
        using M = std::remove_reference_t<decltype(std::get<I>(args))>;
        expected_types.push_back(M::descriptor()->full_name());
      }(),
      ...);

  return absl::InvalidArgumentError(
      absl::StrFormat("Received message type %s, but expected any of [%s]",
                      StripTypeUrlPrefix(response_packet.response().type_url()),
                      absl::StrJoin(expected_types, ", ")));
}

template <typename RequestT, typename... ResponseT>
absl::StatusOr<std::vector<PubSub::GetResult>> PubSub::Call(
    absl::string_view keyexpr, const RequestT& request,
    const PubSub::QueryOptions& options) {
  intrinsic_proto::pubsub::PubSubQueryRequest request_packet =
      PrepareRequestPacket(request, options);

  INTR_ASSIGN_OR_RETURN(
      const std::vector<intrinsic_proto::pubsub::PubSubQueryResponse>
          response_packets,
      GetImpl(keyexpr, request_packet, options));

  // This tries to convert each of the received responses into one of the types
  // passed as template arguments.
  std::vector<PubSub::GetResult> results;
  results.reserve(response_packets.size());
  for (const auto& response : response_packets) {
    results.push_back(std::move(
        TryConvertResponseProtoForTypes<std::tuple<std::decay_t<ResponseT>...>>(
            response, std::index_sequence_for<std::decay_t<ResponseT>...>{})));
  }

  return results;
}

template <typename... ResponseT>
absl::StatusOr<std::vector<PubSub::GetResult>> PubSub::Get(
    absl::string_view keyexpr, const PubSub::QueryOptions& options) {
  intrinsic_proto::pubsub::PubSubQueryRequest request_packet =
      PrepareRequestPacket(options);

  INTR_ASSIGN_OR_RETURN(
      const std::vector<intrinsic_proto::pubsub::PubSubQueryResponse>
          response_packets,
      GetImpl(keyexpr, request_packet, options));

  // This tries to convert each of the received responses into one of the types
  // passed as template arguments.
  std::vector<PubSub::GetResult> results;
  results.reserve(response_packets.size());
  for (const auto& response : response_packets) {
    results.push_back(std::move(
        TryConvertResponseProtoForTypes<std::tuple<std::decay_t<ResponseT>...>>(
            response, std::index_sequence_for<std::decay_t<ResponseT>...>{})));
  }

  return results;
}

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_PUBSUB_H_
