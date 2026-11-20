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

#include "intrinsic/platform/pubsub/kvstore.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "grpcpp/client_context.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/support/channel_arguments.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/buffer_factory.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/mutable_buffer.h"
#include "intrinsic_sdk/intrinsic/middleware/imw.h"
#include "intrinsic/platform/common/proto/workcell_info.pb.h"
#include "intrinsic/platform/pubsub/admin_set_grpc/v1/admin_set.grpc.pb.h"
#include "intrinsic/platform/pubsub/admin_set_grpc/v1/admin_set.pb.h"
#include "intrinsic/platform/pubsub/subscription.h"
#include "intrinsic/platform/pubsub/zenoh_subscription_data.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_handle.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_helpers.h"
#include "intrinsic/stats/scoped_span.h"  
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/time/deadline_timeout.h"

ABSL_FLAG(bool, use_replicated_kv_store, false,
          "If true, use the replicated KV store.");
ABSL_FLAG(std::string, admin_set_proxy_endpoint,
          "zenoh-router.app-intrinsic-base.svc.cluster.local:8081",
          "Override the default admin set proxy URL");

namespace intrinsic {

using platform::proto::WorkcellInfo;

namespace {
constexpr absl::Duration kHighConsistencyInitialGetTimeout = absl::Seconds(10);
constexpr absl::Duration kVerificationGetTimeout = absl::Milliseconds(100);
constexpr absl::Duration kVerificationRetryInterval = absl::Milliseconds(500);
constexpr absl::Duration kVerificationRetryDelayMin = absl::Milliseconds(10);
constexpr absl::Duration kVerificationRetryDelayMax = absl::Milliseconds(2500);
constexpr double kVerificationRetryDelayFactor = 5.0;
constexpr absl::string_view kWorkcellInfoKey = "workcell_info";
constexpr size_t kPayloadByteSizeWarningThreshold = 25 * 1024 * 1024;  // 25 MiB
// `imw_query_options_t::timeout_ms == 0` falls back to Zenoh's 10s default,
// while 1ms truncates multi-MB or TCP replies on ZeroDuration() or short
// budgets.
constexpr int64_t kMinZenohQueryTimeoutMs = 250;

using VerificationMode =
    KeyValueStore::SetWithVerificationOptions::VerificationMode;

enum class ConsistencyPollOutcome {
  kStillInitial,
  kCommitted,
  kConflict,
};

bool IsProtobufEncoding(imw_encoding_t encoding) {
  switch (encoding) {
    case IMW_ENCODING_VARINT_PREFIXED_BYTES:
    case IMW_ENCODING_APPLICATION_PROTOBUF:
      return true;
    case IMW_ENCODING_APPLICATION_OCTET_STREAM:
    case IMW_ENCODING_UNKNOWN:
      return false;
  }
  return false;
}

bool IsRawEncoding(imw_encoding_t encoding) {
  switch (encoding) {
    case IMW_ENCODING_APPLICATION_OCTET_STREAM:
      return true;
    case IMW_ENCODING_VARINT_PREFIXED_BYTES:
    case IMW_ENCODING_APPLICATION_PROTOBUF:
    case IMW_ENCODING_UNKNOWN:
      return false;
  }
  return false;
}

bool MatchesExpectedEncoding(imw_encoding_t actual, imw_encoding_t expected) {
  if (IsProtobufEncoding(expected)) {
    return IsProtobufEncoding(actual);
  }
  return actual == expected;
}

// Renders a verification mode for logs, traces and error messages. The switch
// deliberately has no `default:` case, so adding a mode raises -Wswitch here
// instead of silently reporting the wrong one.
absl::string_view ToString(VerificationMode mode) {
  switch (mode) {
    case VerificationMode::kFirstReply:
      return "kFirstReply";
    case VerificationMode::kHighConsistency:
      return "kHighConsistency";
  }
  return "kUnknown";  // Only reachable via an out-of-range cast.
}

absl::Status VerificationTimeoutError(VerificationMode mode,
                                      absl::string_view key) {
  return absl::DeadlineExceededError(absl::StrFormat(
      "Timeout waiting for verification (mode: %s) for key '%s'",
      ToString(mode), key));
}

struct ConsistencyExpectedState {
  imw_encoding_t encoding = IMW_ENCODING_VARINT_PREFIXED_BYTES;
  absl::string_view bytes;
  std::optional<imw_encoding_t> initial_encoding;
  absl::string_view initial_bytes;
};

absl::Status ClassifyConsistencyMessage(
    const void* response_bytes, size_t response_bytes_len,
    imw_encoding_t encoding, const ConsistencyExpectedState& expected,
    ConsistencyPollOutcome& out) {
  const absl::string_view actual_bytes =
      (response_bytes != nullptr && response_bytes_len > 0)
          ? absl::string_view(static_cast<const char*>(response_bytes),
                              response_bytes_len)
          : absl::string_view();
  if (MatchesExpectedEncoding(encoding, expected.encoding) &&
      actual_bytes == expected.bytes) {
    out = ConsistencyPollOutcome::kCommitted;
    return absl::OkStatus();
  }
  if (expected.initial_encoding.has_value() &&
      encoding == *expected.initial_encoding &&
      actual_bytes == expected.initial_bytes) {
    out = ConsistencyPollOutcome::kStillInitial;
    return absl::FailedPreconditionError("Value matches initial state");
  }
  out = ConsistencyPollOutcome::kConflict;
  return absl::OkStatus();
}

std::optional<absl::Status> EvaluateConsistencyPoll(
    absl::string_view key, const absl::StatusOr<ConsistencyPollOutcome>& poll,
    bool had_initial_state) {
  if (poll.ok()) {
    if (*poll == ConsistencyPollOutcome::kCommitted) {
      return absl::OkStatus();
    }
    if (*poll == ConsistencyPollOutcome::kConflict) {
      return absl::AbortedError(absl::StrFormat(
          "Value for key '%s' was modified by another process while "
          "waiting for verification (mode: %s)",
          key, ToString(VerificationMode::kHighConsistency)));
    }
    return std::nullopt;
  }
  if (absl::IsNotFound(poll.status())) {
    if (had_initial_state) {
      return absl::AbortedError(absl::StrFormat(
          "Value for key '%s' was modified by another process while "
          "waiting for verification (mode: %s)",
          key, ToString(VerificationMode::kHighConsistency)));
    }
    return std::nullopt;
  }
  if (absl::IsDeadlineExceeded(poll.status()) ||
      absl::IsFailedPrecondition(poll.status())) {
    return std::nullopt;
  }
  return absl::InternalError(absl::StrFormat(
      "Unexpected error while waiting for verification (mode: %s) when "
      "setting key '%s': %s",
      ToString(VerificationMode::kHighConsistency), key,
      poll.status().ToString()));
}

// Waits out the retry backoff, but never past the point where one more poll
// still fits in the budget. Sleeping through the tail of the budget would
// spend it without ever looking for the value again.
void SleepForBackoff(absl::Duration delay, absl::Time deadline) {
  const absl::Duration sleepable =
      ToTimeout(deadline) - kVerificationGetTimeout;
  if (sleepable <= absl::ZeroDuration()) {
    return;
  }
  absl::SleepFor(std::min(delay, sleepable));
}

void ReleaseImmutableBufferDeleter(void* /*data*/, void* context) {
  std::unique_ptr<ImmutableBuffer> owner(
      static_cast<ImmutableBuffer*>(context));
}

absl::Status CheckImwSetResult(imw_ret_t ret) {
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error setting a key, return code: %d", ret));
  }
  return absl::OkStatus();
}

imw_ret_t DispatchImwSet(const std::string& prefixed_name, const void* data,
                         size_t size, imw_encoding_t encoding,
                         ImmutableBuffer owned_buffer) {
  if (owned_buffer.valid()) {
    auto heap_owner =
        std::make_unique<ImmutableBuffer>(std::move(owned_buffer));
    const void* owned_data = heap_owner->data();
    return Zenoh().imw_set_with_encoding_and_deleter(
        prefixed_name.c_str(), owned_data, size, encoding,
        &ReleaseImmutableBufferDeleter, heap_owner.release());
  }
  return Zenoh().imw_set_with_encoding(prefixed_name.c_str(), data, size,
                                       encoding);
}

absl::Status DispatchAndCheckImwSet(const std::string& prefixed_name,
                                    const void* data, size_t size,
                                    imw_encoding_t encoding,
                                    ImmutableBuffer owned_buffer) {

  const stats::ScopedSpan net_span("KVStoreClient::ZenohTransportSet");

  const imw_ret_t ret = DispatchImwSet(prefixed_name, data, size, encoding,
                                       std::move(owned_buffer));

  net_span.AddAttribute("zenoh.ret_code", static_cast<int64_t>(ret));

  return CheckImwSetResult(ret);
}

template <typename ResultT, typename OnMessageFn>
struct SingleMessageQueryState {
  absl::Mutex mutex;
  absl::Notification notif;
  ResultT value{};
  absl::Status status = absl::NotFoundError("Key not found");
  bool finished = false;
  std::optional<OnMessageFn> handler;
  std::unique_ptr<stats::ScopedSpan> query_span;  

  explicit SingleMessageQueryState(OnMessageFn fn) : handler(std::move(fn)) {}

  void HandleMessage(const void* response_bytes, size_t response_bytes_len,
                     const imw_message_metadata_t* metadata) {
    if (metadata != nullptr && metadata->kind == IMW_MESSAGE_KIND_DELETE) {
      return;
    }
    absl::MutexLock lock(&mutex);
    if (finished || status.ok() || !handler.has_value()) {
      return;
    }

    if (query_span) {
      query_span->AddAttribute("zenoh.returned", true);
      query_span.reset();
    }

    const imw_encoding_t encoding = metadata != nullptr
                                        ? metadata->encoding
                                        : IMW_ENCODING_VARINT_PREFIXED_BYTES;
    if (absl::Status message_status =
            (*handler)(response_bytes, response_bytes_len, encoding, value);
        message_status.ok() || absl::IsNotFound(status)) {
      status = std::move(message_status);
    }
  }
};

// Owns callback functors alongside QueryContext for transfer to IMWZenoh via
// `imw_query_options_t::user_context_deleter`. Inherits QueryContext so
// round-trip void* -> QueryContext* -> OwnedQueryContext* casts are standard.
struct OwnedQueryContext : public QueryContext {
  imw_callback_functor_t owned_callback;
  imw_message_callback_functor_t owned_message_callback;
  imw_on_done_functor_t owned_on_done;

  OwnedQueryContext(imw_callback_functor_t cb, imw_on_done_functor_t done)
      : owned_callback(std::move(cb)), owned_on_done(std::move(done)) {
    callback = &owned_callback;
    on_done = &owned_on_done;
  }

  OwnedQueryContext(imw_message_callback_functor_t cb,
                    imw_on_done_functor_t done)
      : owned_message_callback(std::move(cb)), owned_on_done(std::move(done)) {
    message_callback = &owned_message_callback;
    on_done = &owned_on_done;
  }
};

void DeleteOwnedQueryContext(void* user_context) {
  delete static_cast<OwnedQueryContext*>(
      static_cast<QueryContext*>(user_context));
}

}  // namespace

template <typename ResultT, typename OnMessageFn>
absl::StatusOr<ResultT> KeyValueStore::QuerySingleMessage(
    const std::string& raw_key, absl::Duration timeout,
    OnMessageFn&& on_message) {
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Timeout must be zero or positive");
  }

  const stats::ScopedSpan get_span("KVStoreClient::QuerySingleMessage");
  get_span.AddAttribute("kv.raw_key", raw_key);
  auto transport_span = std::make_unique<stats::ScopedSpan>(
      "KVStoreClient::ZenohTransportQuery", get_span.span());


  using State = SingleMessageQueryState<ResultT, std::decay_t<OnMessageFn>>;
  auto state = std::make_shared<State>(std::forward<OnMessageFn>(on_message));

  state->query_span = std::move(transport_span);


  auto owned_ctx = std::make_unique<OwnedQueryContext>(
      imw_message_callback_functor_t(
          [state](const char* unused_keyexpr, const void* response_bytes,
                  const size_t response_bytes_len,
                  const imw_message_metadata_t* metadata) {
            state->HandleMessage(response_bytes, response_bytes_len, metadata);
          }),
      imw_on_done_functor_t(
          [state](const char* unused_keyexpr) { state->notif.Notify(); }));

  imw_query_options_t query_options{
      .timeout_ms = static_cast<uint64_t>(std::max<int64_t>(
          kMinZenohQueryTimeoutMs, absl::ToInt64Milliseconds(timeout))),
      .user_context_deleter = &DeleteOwnedQueryContext,
  };
  imw_ret ret = Zenoh().imw_query_messages(
      raw_key.c_str(), zenoh_query_message_static_callback,
      zenoh_query_static_on_done, /*query_payload=*/nullptr,
      /*query_payload_len=*/0, static_cast<QueryContext*>(owned_ctx.release()),
      &query_options);
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error getting a key, return code: %d", ret));
  }

  const bool notified =
      state->notif.WaitForNotificationWithTimeout(timeout + absl::Seconds(1));
  absl::MutexLock lock(&state->mutex);
  state->finished = true;
  state->handler.reset();
  state->query_span.reset();  
  if (!notified) {
    return absl::DeadlineExceededError("Timeout waiting for key");
  }
  if (!state->status.ok()) {
    return state->status;
  }

  return std::move(state->value);
}

// Out-of-line destructor keeps KVQuery non-trivially destructible so callers
// holding a KVQuery handle do not trigger -Wunused-variable warnings.
KVQuery::~KVQuery() = default;

KeyValueStore::KeyValueStore(std::optional<std::string> prefix_override)
    : key_prefix_(prefix_override.has_value() ? prefix_override.value()
                                              : kDefaultKeyPrefix) {
  if (absl::GetFlag(FLAGS_use_replicated_kv_store)) {
    key_prefix_ = kReplicationPrefix;
  }
}

/* static */
std::string KeyValueStore::MakeKeyFromVector(
    const std::vector<std::string>& parts) {
  std::string result;
  for (absl::string_view part : parts) {
    StripSlashesAndAppend(result, part);
  }
  return result;
}

/* static */
std::string KeyValueStore::MakeKeyImpl(
    std::initializer_list<absl::string_view> parts) {
  std::string result;
  for (absl::string_view part : parts) {
    StripSlashesAndAppend(result, part);
  }
  return result;
}

/* static */
void KeyValueStore::StripSlashesAndAppend(std::string& result,
                                          absl::string_view part) {
  size_t start = part.find_first_not_of('/');
  if (start == absl::string_view::npos) {
    return;
  }
  size_t end = part.find_last_not_of('/');
  absl::string_view trimmed = part.substr(start, end - start + 1);
  if (!result.empty()) {
    result.push_back('/');
  }
  result.append(trimmed.data(), trimmed.size());
}

absl::Status KeyValueStore::Set(absl::string_view key,
                                const google::protobuf::Any& value) {

  const stats::ScopedSpan serialize_span("KVStoreClient::ProtoSerializeAny");

  std::string value_serialized = value.SerializeAsString();

  serialize_span.AddAttribute("payload.bytes",
                              static_cast<int64_t>(value_serialized.size()));


  // TODO(b/585031754): Switch protobuf writes to
  // IMW_ENCODING_APPLICATION_PROTOBUF after reader rollout.
  return ExecuteSet(key, value_serialized.data(), value_serialized.size(),
                    IMW_ENCODING_VARINT_PREFIXED_BYTES, "Set",
                    ImmutableBuffer());
}

absl::Status KeyValueStore::Set(absl::string_view key,
                                const google::protobuf::Any& value,
                                std::optional<bool> high_consistency) {
  if (high_consistency.value_or(false)) {
    LOG_EVERY_N_SEC(WARNING, 60)
        << "Passing high_consistency to Set() is deprecated. "
        << "Use SetWithVerification() instead.";

    // These values are frozen to reproduce pre-deprecation
    // Set(key, value, /*high_consistency=*/true) semantics. Do not let them
    // drift with SetWithVerificationOptions defaults.
    return SetWithVerification(key, value,
                               {.mode = VerificationMode::kHighConsistency,
                                .timeout = absl::Seconds(30)});
  }

  return Set(key, value);
}

absl::StatusOr<std::string> KeyValueStore::PrepareSetKey(
    absl::string_view key, size_t size, imw_encoding_t encoding,
    absl::string_view op_name) const {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(key));
  INTR_ASSIGN_OR_RETURN(std::string prefixed_name,
                        ZenohHandle::add_key_prefix(key, key_prefix_));
  if (IsRawEncoding(encoding)) {
    LOG_EVERY_N_SEC(INFO, 10)
        << "KVStore " << op_name << " for key: " << prefixed_name;
  } else {
    LOG(INFO) << "KVStore " << op_name << " for key: " << prefixed_name;
    if (size > kPayloadByteSizeWarningThreshold) {
      LOG(WARNING) << "Large KVStore SetRequest payload detected. Key: " << key
                   << ", Size: " << size << " bytes.";
    }
  }
  return prefixed_name;
}

std::optional<KeyValueStore::EncodedValue>
KeyValueStore::ReadInitialHighConsistencyState(const std::string& prefixed_name,
                                               absl::string_view key,
                                               absl::Duration timeout) {
  // This read precedes the write, so it is not part of the verification
  // budget. It is still clamped to the caller's timeout so that a caller
  // asking for a short verification cannot be blocked here for ten seconds.
  absl::StatusOr<EncodedValue> initial_result = GetEncodedWithRawKey(
      prefixed_name, std::min(kHighConsistencyInitialGetTimeout, timeout));
  if (initial_result.ok()) {
    return std::move(initial_result.value());
  }
  if (!absl::IsNotFound(initial_result.status())) {
    LOG_EVERY_N_SEC(WARNING, 1)
        << "Initial read failed during high consistency check for key '" << key
        << "': " << initial_result.status();
  }
  return std::nullopt;
}

absl::Status KeyValueStore::VerifyAfterSet(
    const std::string& prefixed_name, absl::string_view key,
    imw_encoding_t expected_encoding, absl::string_view expected_bytes,
    const std::optional<EncodedValue>& initial_state,
    const SetWithVerificationOptions& options) {
  switch (options.mode) {
    case VerificationMode::kFirstReply:
      return VerifyFirstReply(prefixed_name, key, expected_encoding,
                              options.timeout);
    case VerificationMode::kHighConsistency:
      return VerifyHighConsistency(prefixed_name, key, expected_encoding,
                                   expected_bytes, initial_state,
                                   options.timeout);
  }
  return absl::InvalidArgumentError("Unknown verification mode");
}

absl::Status KeyValueStore::ExecuteSet(
    absl::string_view key, const void* data, size_t size,
    imw_encoding_t encoding, absl::string_view op_name,
    ImmutableBuffer owned_buffer,
    const SetWithVerificationOptions* verification) {

  const stats::ScopedSpan set_span(absl::StrCat("KVStoreClient::", op_name));
  set_span.AddAttribute("kv.key", std::string(key));
  if (verification != nullptr) {
    set_span.AddAttribute("kv.verification_mode",
                          std::string(ToString(verification->mode)));
  } else {
    set_span.AddAttribute("kv.high_consistency", false);
  }


  INTR_ASSIGN_OR_RETURN(std::string prefixed_name,
                        PrepareSetKey(key, size, encoding, op_name));

  if (verification == nullptr) {
    return DispatchAndCheckImwSet(prefixed_name, data, size, encoding,
                                  std::move(owned_buffer));
  }

  std::optional<EncodedValue> initial_state;
  ImmutableBuffer expected_owner;
  absl::string_view expected_bytes;
  if (verification->mode == VerificationMode::kHighConsistency) {
    initial_state = ReadInitialHighConsistencyState(prefixed_name, key,
                                                    verification->timeout);
    expected_owner = owned_buffer;
    const void* expected_ptr =
        expected_owner.valid() ? expected_owner.data() : data;
    const size_t expected_size =
        expected_owner.valid() ? expected_owner.size() : size;
    if (expected_ptr != nullptr && expected_size > 0) {
      expected_bytes = absl::string_view(static_cast<const char*>(expected_ptr),
                                         expected_size);
    }
  }

  INTR_RETURN_IF_ERROR(DispatchAndCheckImwSet(
      prefixed_name, data, size, encoding, std::move(owned_buffer)));

  return VerifyAfterSet(prefixed_name, key, encoding, expected_bytes,
                        initial_state, *verification);
}

absl::Status KeyValueStore::SetWithVerification(
    absl::string_view key, const google::protobuf::Any& value,
    const SetWithVerificationOptions& options) {
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "SetWithVerificationOptions::timeout must be positive");
  }

  const stats::ScopedSpan serialize_span("KVStoreClient::ProtoSerializeAny");

  std::string value_serialized = value.SerializeAsString();

  serialize_span.AddAttribute("payload.bytes",
                              static_cast<int64_t>(value_serialized.size()));


  // TODO(b/585031754): Switch protobuf writes to
  // IMW_ENCODING_APPLICATION_PROTOBUF after reader rollout.
  return ExecuteSet(key, value_serialized.data(), value_serialized.size(),
                    IMW_ENCODING_VARINT_PREFIXED_BYTES, "SetWithVerification",
                    ImmutableBuffer(), &options);
}

absl::Status KeyValueStore::SetRaw(absl::string_view key,
                                   ImmutableBuffer value) {
  if (!value.valid()) {
    return absl::InvalidArgumentError("Cannot set an invalid ImmutableBuffer");
  }
  const void* data = value.data();
  const size_t size = value.size();
  return ExecuteSet(key, data, size, IMW_ENCODING_APPLICATION_OCTET_STREAM,
                    "SetRaw", std::move(value));
}

absl::Status KeyValueStore::SetRaw(absl::string_view key,
                                   MutableBuffer&& value) {
  if (!value.valid()) {
    return absl::InvalidArgumentError("Cannot set an invalid MutableBuffer");
  }
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(key));
  INTR_RETURN_IF_ERROR(ZenohHandle::add_key_prefix(key, key_prefix_).status());
  INTR_ASSIGN_OR_RETURN(ImmutableBuffer frozen,
                        FreezeMutableBuffer(std::move(value)));
  return SetRaw(key, std::move(frozen));
}

absl::Status KeyValueStore::SetRawWithVerification(
    absl::string_view key, ImmutableBuffer value,
    const SetWithVerificationOptions& options) {
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "SetWithVerificationOptions::timeout must be positive");
  }
  if (!value.valid()) {
    return absl::InvalidArgumentError("Cannot set an invalid ImmutableBuffer");
  }
  const void* data = value.data();
  const size_t size = value.size();
  return ExecuteSet(key, data, size, IMW_ENCODING_APPLICATION_OCTET_STREAM,
                    "SetRawWithVerification", std::move(value), &options);
}

absl::Status KeyValueStore::SetRawWithVerification(
    absl::string_view key, MutableBuffer&& value,
    const SetWithVerificationOptions& options) {
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "SetWithVerificationOptions::timeout must be positive");
  }
  if (!value.valid()) {
    return absl::InvalidArgumentError("Cannot set an invalid MutableBuffer");
  }
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(key));
  INTR_RETURN_IF_ERROR(ZenohHandle::add_key_prefix(key, key_prefix_).status());
  INTR_ASSIGN_OR_RETURN(ImmutableBuffer frozen,
                        FreezeMutableBuffer(std::move(value)));
  return SetRawWithVerification(key, std::move(frozen), options);
}

absl::Status KeyValueStore::VerifyFirstReply(const std::string& prefixed_name,
                                             absl::string_view key,
                                             imw_encoding_t expected_encoding,
                                             absl::Duration timeout) {

  const stats::ScopedSpan wait_span("KVStoreClient::VerifyFirstReply");

  const absl::Time deadline = ToDeadline(timeout);
  absl::Duration current_delay = kVerificationRetryDelayMin;
  while (true) {
    // Checked before polling rather than after, so that an exhausted budget
    // never spends another (zero-timeout) round trip before reporting.
    const absl::Duration remaining = ToTimeout(deadline);
    if (remaining <= absl::ZeroDuration()) {
      return VerificationTimeoutError(VerificationMode::kFirstReply, key);
    }
    const absl::Duration poll_timeout =
        std::max(kVerificationGetTimeout,
                 std::min(kVerificationRetryInterval, remaining));
    const absl::Status reply_status =
        QuerySingleMessage<int>(
            prefixed_name, poll_timeout,
            [expected_encoding](const void* /*response_bytes*/,
                                const size_t /*response_bytes_len*/,
                                const imw_encoding_t encoding,
                                int& /*out*/) -> absl::Status {
              if (!MatchesExpectedEncoding(encoding, expected_encoding)) {
                return absl::FailedPreconditionError("Reply encoding mismatch");
              }
              return absl::OkStatus();
            })
            .status();
    if (reply_status.ok()) {
      return absl::OkStatus();
    }
    if (!absl::IsNotFound(reply_status) &&
        !absl::IsDeadlineExceeded(reply_status) &&
        !absl::IsFailedPrecondition(reply_status)) {
      return reply_status;
    }
    SleepForBackoff(current_delay, deadline);
    current_delay = std::min(current_delay * kVerificationRetryDelayFactor,
                             kVerificationRetryDelayMax);
  }
}

absl::Status KeyValueStore::VerifyHighConsistency(
    const std::string& prefixed_name, absl::string_view key,
    imw_encoding_t expected_encoding, absl::string_view expected_bytes,
    const std::optional<EncodedValue>& initial_state, absl::Duration timeout) {

  const stats::ScopedSpan wait_span("KVStoreClient::VerifyHighConsistency");

  const absl::Time deadline = ToDeadline(timeout);
  absl::Duration current_delay = kVerificationRetryDelayMin;
  const ConsistencyExpectedState expected_state{
      .encoding = expected_encoding,
      .bytes = expected_bytes,
      .initial_encoding =
          initial_state.has_value()
              ? std::optional<imw_encoding_t>(initial_state->encoding)
              : std::nullopt,
      .initial_bytes = initial_state.has_value()
                           ? absl::string_view(initial_state->bytes)
                           : absl::string_view(),
  };
  while (true) {
    // Checked before polling rather than after, so that an exhausted budget
    // never spends another (zero-timeout) round trip before reporting.
    const absl::Duration remaining = ToTimeout(deadline);
    if (remaining <= absl::ZeroDuration()) {
      return VerificationTimeoutError(VerificationMode::kHighConsistency, key);
    }
    const absl::Duration poll_timeout =
        std::max(kVerificationGetTimeout,
                 std::min(kVerificationRetryInterval, remaining));
    absl::StatusOr<ConsistencyPollOutcome> poll =
        QuerySingleMessage<ConsistencyPollOutcome>(
            prefixed_name, poll_timeout,
            [&expected_state](const void* response_bytes,
                              const size_t response_bytes_len,
                              const imw_encoding_t encoding,
                              ConsistencyPollOutcome& out) -> absl::Status {
              return ClassifyConsistencyMessage(response_bytes,
                                                response_bytes_len, encoding,
                                                expected_state, out);
            });

    if (std::optional<absl::Status> done =
            EvaluateConsistencyPoll(key, poll, initial_state.has_value());
        done.has_value()) {
      return *done;
    }

    SleepForBackoff(current_delay, deadline);
    current_delay = std::min(current_delay * kVerificationRetryDelayFactor,
                             kVerificationRetryDelayMax);
  }
}

absl::StatusOr<google::protobuf::Any> KeyValueStore::GetAny(
    absl::string_view key, absl::Duration timeout) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(key));
  INTR_ASSIGN_OR_RETURN(std::string raw_key,
                        ZenohHandle::add_key_prefix(key, key_prefix_));
  LOG(INFO) << "KVStore Get for key: " << raw_key;
  return GetAnyWithRawKey(raw_key, timeout);
}

absl::StatusOr<ImmutableBuffer> KeyValueStore::GetRaw(absl::string_view key,
                                                      absl::Duration timeout) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(key));
  INTR_ASSIGN_OR_RETURN(std::string raw_key,
                        ZenohHandle::add_key_prefix(key, key_prefix_));
  return QuerySingleMessage<ImmutableBuffer>(
      raw_key, timeout,
      [raw_key](const void* response_bytes, const size_t response_bytes_len,
                const imw_encoding_t encoding,
                ImmutableBuffer& out) -> absl::Status {
        if (!IsRawEncoding(encoding)) {
          if (IsProtobufEncoding(encoding)) {
            return absl::InvalidArgumentError(absl::StrFormat(
                "Key '%s' stores a Protobuf message, not raw bytes", raw_key));
          }
          return absl::InvalidArgumentError(absl::StrFormat(
              "Key '%s' has unsupported encoding (%d), not raw bytes", raw_key,
              static_cast<int>(encoding)));
        }
        INTR_ASSIGN_OR_RETURN(
            out, CopyToImmutableBuffer(response_bytes, response_bytes_len));
        return absl::OkStatus();
      });
}

absl::StatusOr<KeyValueStore::EncodedValue> KeyValueStore::GetEncodedWithRawKey(
    const std::string& raw_key, absl::Duration timeout) {
  return QuerySingleMessage<EncodedValue>(
      raw_key, timeout,
      [](const void* response_bytes, const size_t response_bytes_len,
         const imw_encoding_t encoding, EncodedValue& out) -> absl::Status {
        out.encoding = encoding;
        if (response_bytes != nullptr && response_bytes_len > 0) {
          out.bytes.assign(static_cast<const char*>(response_bytes),
                           response_bytes_len);
        } else {
          out.bytes.clear();
        }
        return absl::OkStatus();
      });
}

absl::StatusOr<google::protobuf::Any> KeyValueStore::GetAnyWithRawKey(
    const std::string& raw_key, absl::Duration timeout) {

  const stats::ScopedSpan get_span("KVStoreClient::GetAny");
  get_span.AddAttribute("kv.raw_key", raw_key);


  return QuerySingleMessage<google::protobuf::Any>(
      raw_key, timeout,
      [

          parent_span = get_span.span(),

          raw_key](const void* response_bytes, const size_t response_bytes_len,
                   const imw_encoding_t encoding,
                   google::protobuf::Any& out) -> absl::Status {
        if (!IsProtobufEncoding(encoding)) {
          if (IsRawEncoding(encoding)) {
            return absl::InvalidArgumentError(absl::StrFormat(
                "Key '%s' stores raw bytes, not a Protobuf message", raw_key));
          }
          return absl::InvalidArgumentError(absl::StrFormat(
              "Key '%s' has unsupported non-Protobuf encoding (%d)", raw_key,
              static_cast<int>(encoding)));
        }

        const stats::ScopedSpan parse_span("KVStoreClient::ProtoDeserializeAny",
                                           parent_span);
        parse_span.AddAttribute("payload.bytes",
                                static_cast<int64_t>(response_bytes_len));

        if (response_bytes == nullptr || response_bytes_len == 0) {
          out.Clear();
          return absl::OkStatus();
        }
        if (!out.ParseFromArray(response_bytes,
                                static_cast<int>(response_bytes_len))) {
          return absl::InternalError(absl::StrFormat(
              "Failed to parse response for key '%s'", raw_key));
        }
        return absl::OkStatus();
      });
}

absl::StatusOr<KVQuery> KeyValueStore::GetAll(absl::string_view keyexpr,
                                              KeyValueCallback callback,
                                              OnDoneCallback on_done) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(keyexpr));
  INTR_ASSIGN_OR_RETURN(absl::StatusOr<std::string> prefixed_name,
                        ZenohHandle::add_key_prefix(keyexpr, key_prefix_));
  auto owned_ctx = std::make_unique<OwnedQueryContext>(
      imw_callback_functor_t([callback = std::move(callback)](
                                 const char* key, const void* response_bytes,
                                 const size_t response_bytes_len) {
        auto value = std::make_unique<google::protobuf::Any>();
        static_cast<void>(value->ParseFromString(absl::string_view(
            static_cast<const char*>(response_bytes),
            response_bytes_len)));  // TODO: Handle return value
        callback(key, std::move(value));
      }),
      imw_on_done_functor_t([on_done = std::move(on_done)](const char* key) {
        on_done(absl::string_view(key));
      }));
  imw_query_options_t query_options{
      .user_context_deleter = &DeleteOwnedQueryContext,
  };
  imw_ret_t ret = Zenoh().imw_query(
      prefixed_name->c_str(), zenoh_query_static_callback,
      zenoh_query_static_on_done, nullptr, 0,
      static_cast<QueryContext*>(owned_ctx.release()), &query_options);
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error getting a key, return code: %d", ret));
  }

  return KVQuery();
}

absl::StatusOr<std::vector<std::string>> KeyValueStore::ListAllKeys(
    absl::Duration timeout) {
  if (key_prefix_ == kReplicationPrefix) {
    return absl::UnimplementedError(
        "ListAllKeys is not supported for replicated KV store; use "
        "ListAllOnpremKeys or ListAllGlobalKeys instead.");
  }
  absl::string_view query_keyexpr = "**";
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKeyexpr(query_keyexpr));
  INTR_ASSIGN_OR_RETURN(
      std::string prefixed_name,
      ZenohHandle::add_key_prefix(query_keyexpr, key_prefix_));
  return ExecuteList(prefixed_name, timeout);
}

absl::StatusOr<std::vector<std::string>> KeyValueStore::ListAllGlobalKeys(
    absl::Duration timeout) {
  if (key_prefix_ == kDefaultKeyPrefix) {
    return absl::UnimplementedError(
        "ListAllGlobalKeys is only supported for replicated KV store; use "
        "ListAllKeys instead.");
  }
  absl::string_view query_keyexpr = "global/**";
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKeyexpr(query_keyexpr));
  INTR_ASSIGN_OR_RETURN(
      std::string prefixed_name,
      ZenohHandle::add_key_prefix(query_keyexpr, key_prefix_));
  return ExecuteList(prefixed_name, timeout);
}

absl::StatusOr<std::vector<std::string>> KeyValueStore::ListAllOnpremKeys(
    absl::string_view workcell_name, absl::Duration timeout) {
  if (key_prefix_ == kDefaultKeyPrefix) {
    return absl::UnimplementedError(
        "ListAllOnpremKeys is only supported for replicated KV store; use "
        "ListAllKeys instead.");
  }
  std::string query_keyexpr = absl::StrFormat("%s/**", workcell_name);
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKeyexpr(query_keyexpr));
  INTR_ASSIGN_OR_RETURN(
      std::string prefixed_name,
      ZenohHandle::add_key_prefix(query_keyexpr, key_prefix_));
  return ExecuteList(prefixed_name, timeout);
}

absl::Status KeyValueStore::Delete(absl::string_view key) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(key));
  INTR_ASSIGN_OR_RETURN(std::string prefixed_name,
                        ZenohHandle::add_key_prefix(key, key_prefix_));
  LOG(INFO) << "KVStore Delete for key: " << prefixed_name;
  imw_ret_t ret = Zenoh().imw_delete_keyexpr(prefixed_name.c_str());
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error deleting a key, return code: %d", ret));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<std::string>> KeyValueStore::ExecuteList(
    absl::string_view keyexpr, absl::Duration timeout) {
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Timeout must be zero or positive");
  }
  struct ListState {
    absl::Mutex mutex;
    absl::Notification notif;
    std::vector<std::string> keys ABSL_GUARDED_BY(mutex);
    bool finished ABSL_GUARDED_BY(mutex) = false;
  };
  auto state = std::make_shared<ListState>();
  auto owned_ctx = std::make_unique<OwnedQueryContext>(
      imw_callback_functor_t([state](const char* reply_keyexpr,
                                     const void* unused_response_bytes,
                                     const size_t unused_response_bytes_len) {
        absl::MutexLock lock(&state->mutex);
        if (state->finished) {
          return;
        }
        state->keys.push_back(reply_keyexpr);
      }),
      imw_on_done_functor_t(
          [state](const char* unused_keyexpr) { state->notif.Notify(); }));
  imw_query_options_t query_options{
      .timeout_ms = static_cast<uint64_t>(std::max<int64_t>(
          kMinZenohQueryTimeoutMs, absl::ToInt64Milliseconds(timeout))),
      .user_context_deleter = &DeleteOwnedQueryContext,
  };
  std::string keyexpr_str(keyexpr);
  imw_ret ret = Zenoh().imw_query(
      keyexpr_str.c_str(), zenoh_query_static_callback,
      zenoh_query_static_on_done, /*query_payload=*/nullptr,
      /*query_payload_len=*/0, static_cast<QueryContext*>(owned_ctx.release()),
      &query_options);
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error getting a key, return code: %d", ret));
  }
  const bool notified =
      state->notif.WaitForNotificationWithTimeout(timeout + absl::Seconds(1));
  absl::MutexLock lock(&state->mutex);
  state->finished = true;
  if (!notified) {
    return absl::DeadlineExceededError("Timeout waiting for key list");
  }
  return std::move(state->keys);
}

// We need to make a grpc call to the admin set service to copy the key value
// from the source key to the target key.
absl::Status KeyValueStore::AdminCloudCopy(absl::string_view source_key,
                                           absl::string_view target_key,
                                           absl::Duration timeout) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(source_key));
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(target_key));
  INTR_RETURN_IF_ERROR(
      ZenohHandle::add_key_prefix(source_key, key_prefix_).status());

  INTR_ASSIGN_OR_RETURN(google::protobuf::Any value,
                        GetAny(source_key, timeout));

  std::shared_ptr<::grpc::Channel> channel = ::grpc::CreateCustomChannel(
      absl::GetFlag(FLAGS_admin_set_proxy_endpoint),
      ::grpc::                       // NOLINTNEXTLINE
      InsecureChannelCredentials(),  // NO_LINT(grpc_insecure_credential_linter)
      ::grpc::ChannelArguments());
  if (channel == nullptr) {
    return absl::InternalError("Failed to create channel");
  }
  auto stub =
      intrinsic_proto::pubsub::admin_set_grpc::v1::AdminSetService::NewStub(
          channel);
  if (stub == nullptr) {
    return absl::InternalError("Failed to create stub");
  }

  // Create a request to the admin set gRPC service.
  intrinsic_proto::pubsub::admin_set_grpc::v1::AdminSetRequest request;
  request.set_key(target_key);
  *request.mutable_value() = value;
  request.set_timeout_ms(absl::ToInt64Milliseconds(timeout));

  // Make the gRPC call.
  grpc::ClientContext context;
  intrinsic_proto::pubsub::admin_set_grpc::v1::AdminSetResponse response;
  grpc::Status status = stub->AdminCopy(&context, request, &response);
  if (!status.ok()) {
    return absl::InternalError(
        absl::StrFormat("gRPC call failed: %s", status.error_message()));
  }

  return absl::OkStatus();
}

absl::StatusOr<absl::flat_hash_map<std::string, google::protobuf::Any>>
KeyValueStore::GetAllSynchronous(absl::string_view keyexpr,
                                 absl::Duration timeout) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(keyexpr));

  struct SyncState {
    absl::Mutex mutex;
    absl::Notification done;
    absl::flat_hash_map<std::string, google::protobuf::Any> results
        ABSL_GUARDED_BY(mutex);
    bool finished ABSL_GUARDED_BY(mutex) = false;
  };
  auto state = std::make_shared<SyncState>();

  auto callback = [state](absl::string_view key,
                          std::unique_ptr<google::protobuf::Any> value) {
    absl::MutexLock lock(&state->mutex);
    if (state->finished) {
      return;
    }
    if (value != nullptr) {
      state->results.insert_or_assign(std::string(key), std::move(*value));
    }
  };

  auto on_done = [state](absl::string_view /*key*/) { state->done.Notify(); };

  INTR_ASSIGN_OR_RETURN(
      KVQuery query, GetAll(keyexpr, std::move(callback), std::move(on_done)));

  const bool notified = state->done.WaitForNotificationWithTimeout(timeout);
  absl::MutexLock lock(&state->mutex);
  state->finished = true;
  if (!notified) {
    return absl::DeadlineExceededError(
        absl::StrFormat("Timeout waiting for GetAll on keyexpr: %s", keyexpr));
  }
  return std::move(state->results);
}

absl::StatusOr<Subscription> KeyValueStore::CreateMessageSubscription(
    absl::string_view key_expression, const TopicConfig& config,
    DeletionCallback deletion_callback,
    imw_message_callback_functor_t put_message_callback) const {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKeyexpr(key_expression));
  INTR_ASSIGN_OR_RETURN(
      std::string prefixed_key_expression,
      ZenohHandle::add_key_prefix(key_expression, key_prefix_));
  LOG(INFO) << "KVStore Subscribe for key: " << prefixed_key_expression;
  auto subscription_data = std::make_unique<SubscriptionData>();
  subscription_data->prefixed_name = prefixed_key_expression;
  subscription_data->message_callback_functor =
      std::make_unique<imw_message_callback_functor_t>(
          [deletion_callback = std::move(deletion_callback),
           put_message_callback = std::move(put_message_callback)](
              const char* keyexpr, const void* blob, const size_t blob_len,
              const imw_message_metadata_t* metadata) {
            if (metadata != nullptr &&
                metadata->kind == IMW_MESSAGE_KIND_DELETE) {
              if (deletion_callback != nullptr) {
                deletion_callback(keyexpr);
              }
              return;
            }
            if (put_message_callback != nullptr) {
              put_message_callback(keyexpr, blob, blob_len, metadata);
            }
          });

  imw_ret_t ret = Zenoh().imw_create_message_subscription(
      prefixed_key_expression.c_str(), zenoh_static_message_callback,
      intrinsic::PubSubQoSToZenohQos(config.topic_qos).c_str(),
      subscription_data->message_callback_functor.get());
  if (ret != IMW_OK) {
    return absl::InternalError("Error creating a subscription");
  }
  return Subscription(prefixed_key_expression, std::move(subscription_data));
}

absl::StatusOr<Subscription> KeyValueStore::CreateSubscription(
    absl::string_view key_expression, const TopicConfig& config,
    SubscriptionOkExpandedCallback<google::protobuf::Any> value_callback,
    DeletionCallback deletion_callback) const {
  imw_message_callback_functor_t put_callback =
      [value_callback = std::move(value_callback)](
          const char* keyexpr, const void* blob, const size_t blob_len,
          const imw_message_metadata_t* metadata) {
        if (metadata != nullptr && !IsProtobufEncoding(metadata->encoding)) {
          return;
        }
        google::protobuf::Any msg;
        if (blob != nullptr && blob_len > 0 &&
            !msg.ParseFromArray(blob, static_cast<int>(blob_len))) {
          LOG_EVERY_N(ERROR, 1)
              << "Deserializing message failed. Key expression: " << keyexpr;
          return;
        }
        if (value_callback != nullptr) {
          value_callback(keyexpr, msg);
        }
      };
  return CreateMessageSubscription(key_expression, config,
                                   std::move(deletion_callback),
                                   std::move(put_callback));
}

absl::StatusOr<Subscription> KeyValueStore::CreateRawSubscription(
    absl::string_view key_expression, const TopicConfig& config,
    SubscriptionOkExpandedCallback<ImmutableBuffer> value_callback,
    DeletionCallback deletion_callback) const {
  imw_message_callback_functor_t put_callback =
      [value_callback = std::move(value_callback)](
          const char* keyexpr, const void* blob, const size_t blob_len,
          const imw_message_metadata_t* metadata) {
        if (metadata == nullptr || !IsRawEncoding(metadata->encoding) ||
            value_callback == nullptr) {
          return;
        }
        absl::StatusOr<ImmutableBuffer> buf =
            CopyToImmutableBuffer(blob, blob_len);
        if (!buf.ok()) {
          LOG_EVERY_N(ERROR, 1)
              << "Failed to allocate ImmutableBuffer for key '" << keyexpr
              << "': " << buf.status();
          return;
        }
        value_callback(keyexpr, std::move(*buf));
      };
  return CreateMessageSubscription(key_expression, config,
                                   std::move(deletion_callback),
                                   std::move(put_callback));
}

absl::StatusOr<std::string> KeyValueStore::GetWorkcellReplicationNamespace(
    absl::Duration timeout) {
  absl::Time deadline = absl::Now() + timeout;
  while (absl::Now() < deadline) {
    absl::StatusOr<WorkcellInfo> workcell_info =
        Get<WorkcellInfo>(kWorkcellInfoKey);
    if (workcell_info.ok()) {
      LOG(INFO) << "Workcell info received. Workcell name: "
                << workcell_info->workcell_name();
      return workcell_info->workcell_name();
    } else if (workcell_info.status().code() == absl::StatusCode::kNotFound) {
      absl::SleepFor(absl::Milliseconds(100));
      continue;
    } else {
      LOG(ERROR) << "Failed to get workcell info: "
                 << workcell_info.status().message();
      return workcell_info.status();
    }
  }

  LOG(ERROR) << "Timed out waiting for workcell info";
  return absl::DeadlineExceededError("Timeout waiting for workcell info");
}

}  // namespace intrinsic
