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
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
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

using VerificationMode =
    KeyValueStore::SetWithVerificationOptions::VerificationMode;

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

absl::StatusOr<std::optional<std::string>> FetchCurrentRawState(
    absl::StatusOr<std::string> get_result, absl::string_view key) {
  switch (get_result.status().code()) {
    case absl::StatusCode::kOk:
      // Value exists.
      return std::move(get_result.value());
    case absl::StatusCode::kNotFound:
      // Value does not exist (unset).
      return std::nullopt;
    case absl::StatusCode::kDeadlineExceeded:
      // Transient network timeout reading value. Propagate to retry.
      return get_result.status();
    default:
      return absl::InternalError(
          absl::StrFormat("Unexpected error while waiting for verification "
                          "(mode: %s) when setting key '%s': %s",
                          ToString(VerificationMode::kHighConsistency), key,
                          get_result.status().ToString()));
  }
}

}  // namespace

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

  const stats::ScopedSpan set_span("KVStoreClient::Set");
  set_span.AddAttribute("kv.key", std::string(key));
  set_span.AddAttribute("kv.high_consistency", false);


  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKeyexpr(key));
  // Should not happen since ValidKeyexpr was called before this.
  INTR_ASSIGN_OR_RETURN(std::string prefixed_name,
                        ZenohHandle::add_key_prefix(key, key_prefix_));
  LOG(INFO) << "KVStore Set for key: " << prefixed_name;

  std::string value_serialized;
  {

    const stats::ScopedSpan serialize_span("KVStoreClient::ProtoSerializeAny",
                                           set_span.span());

    value_serialized = value.SerializeAsString();

    serialize_span.AddAttribute("payload.bytes",
                                static_cast<int64_t>(value_serialized.size()));

  }
  const size_t payload_size = value_serialized.size();
  if (payload_size > kPayloadByteSizeWarningThreshold) {
    LOG(WARNING) << "Large KVStore SetRequest payload detected. Key: " << key
                 << ", Size: " << payload_size << " bytes.";
  }

  imw_ret_t ret;
  {

    const stats::ScopedSpan net_span("KVStoreClient::ZenohTransportSet",
                                     set_span.span());

    ret = Zenoh().imw_set(prefixed_name.c_str(), value_serialized.data(),
                          payload_size);

    net_span.AddAttribute("zenoh.ret_code", static_cast<int64_t>(ret));

  }
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error setting a key, return code: %d", ret));
  }
  return absl::OkStatus();
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

absl::Status KeyValueStore::SetWithVerification(
    absl::string_view key, const google::protobuf::Any& value,
    const SetWithVerificationOptions& options) {

  const stats::ScopedSpan set_span("KVStoreClient::SetWithVerification");
  set_span.AddAttribute("kv.key", std::string(key));
  set_span.AddAttribute("kv.verification_mode",
                        std::string(ToString(options.mode)));


  if (options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "SetWithVerificationOptions::timeout must be positive");
  }
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKeyexpr(key));
  INTR_ASSIGN_OR_RETURN(std::string prefixed_name,
                        ZenohHandle::add_key_prefix(key, key_prefix_));
  LOG(INFO) << "KVStore SetWithVerification for key: " << prefixed_name;

  // Get the initial value if high consistency is requested.
  std::optional<std::string> initial_state;
  if (options.mode ==
      SetWithVerificationOptions::VerificationMode::kHighConsistency) {
    // This read precedes the write, so it is not part of the verification
    // budget. It is still clamped to the caller's timeout so that a caller
    // asking for a short verification cannot be blocked here for ten seconds.
    absl::StatusOr<std::string> initial_result = GetRawWithRawKey(
        prefixed_name,
        std::min(kHighConsistencyInitialGetTimeout, options.timeout));
    if (initial_result.ok()) {
      initial_state = std::move(initial_result.value());
    } else if (!absl::IsNotFound(initial_result.status())) {
      // If the initial read fails due to a transient error (e.g. deadline
      // exceeded), we intentionally leave initial_state empty (nullopt). This
      // ensures that if the key was already populated, our conflict check later
      // will see the pre-existing value and abort the operation. The exception
      // is a pre-existing value that already matches the bytes we wrote: the
      // write is then a no-op and returning OK is correct. The caller can
      // retry the set idempotently. This is preferred over skipping the
      // conflict check.
      LOG_EVERY_N_SEC(WARNING, 1)
          << "Initial read failed during high consistency check for key '"
          << key << "': " << initial_result.status();
    }
  }

  std::string value_serialized;
  {

    const stats::ScopedSpan serialize_span("KVStoreClient::ProtoSerializeAny",
                                           set_span.span());

    value_serialized = value.SerializeAsString();

    serialize_span.AddAttribute("payload.bytes",
                                static_cast<int64_t>(value_serialized.size()));

  }
  const size_t payload_size = value_serialized.size();
  if (payload_size > kPayloadByteSizeWarningThreshold) {
    LOG(WARNING) << "Large KVStore SetRequest payload detected. Key: " << key
                 << ", Size: " << payload_size << " bytes.";
  }

  imw_ret_t ret;
  {

    const stats::ScopedSpan net_span("KVStoreClient::ZenohTransportSet",
                                     set_span.span());

    ret = Zenoh().imw_set(prefixed_name.c_str(), value_serialized.data(),
                          payload_size);

    net_span.AddAttribute("zenoh.ret_code", static_cast<int64_t>(ret));

  }
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error setting a key, return code: %d", ret));
  }

  switch (options.mode) {
    case SetWithVerificationOptions::VerificationMode::kFirstReply:
      return VerifyFirstReply(prefixed_name, key, options.timeout);
    case SetWithVerificationOptions::VerificationMode::kHighConsistency:
      return VerifyHighConsistency(prefixed_name, key, value_serialized,
                                   initial_state, options.timeout);
  }
  return absl::InvalidArgumentError("Unknown verification mode");
}

absl::Status KeyValueStore::VerifyFirstReply(const std::string& prefixed_name,
                                             absl::string_view key,
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
    absl::StatusOr<std::string> raw_result = GetRawWithRawKey(
        prefixed_name, std::min(kVerificationRetryInterval, remaining));
    if (raw_result.ok()) {
      return absl::OkStatus();
    }
    if (!absl::IsNotFound(raw_result.status()) &&
        !absl::IsDeadlineExceeded(raw_result.status())) {
      return raw_result.status();
    }
    SleepForBackoff(current_delay, deadline);
    current_delay = std::min(current_delay * kVerificationRetryDelayFactor,
                             kVerificationRetryDelayMax);
  }
}

absl::Status KeyValueStore::VerifyHighConsistency(
    const std::string& prefixed_name, absl::string_view key,
    absl::string_view serialized_value,
    std::optional<absl::string_view> initial_state, absl::Duration timeout) {

  const stats::ScopedSpan wait_span("KVStoreClient::VerifyHighConsistency");

  const absl::Time deadline = ToDeadline(timeout);
  absl::Duration current_delay = kVerificationRetryDelayMin;
  while (true) {
    // Checked before polling rather than after, so that an exhausted budget
    // never spends another (zero-timeout) round trip before reporting.
    const absl::Duration remaining = ToTimeout(deadline);
    if (remaining <= absl::ZeroDuration()) {
      return VerificationTimeoutError(VerificationMode::kHighConsistency, key);
    }
    absl::StatusOr<std::optional<std::string>> current_state =
        FetchCurrentRawState(
            GetRawWithRawKey(prefixed_name,
                             std::min(kVerificationRetryInterval, remaining)),
            key);

    if (!absl::IsDeadlineExceeded(current_state.status())) {
      INTR_RETURN_IF_ERROR(current_state.status());

      if (current_state->has_value() &&
          current_state->value() == serialized_value) {
        // Key value is committed.
        return absl::OkStatus();
      }

      std::optional<absl::string_view> current_bytes;
      if (current_state->has_value()) {
        current_bytes = current_state->value();
      }

      if (current_bytes != initial_state) {
        return absl::AbortedError(absl::StrFormat(
            "Value for key '%s' was modified by another process while "
            "waiting for verification (mode: %s)",
            key, ToString(VerificationMode::kHighConsistency)));
      }
    }

    SleepForBackoff(current_delay, deadline);
    current_delay = std::min(current_delay * kVerificationRetryDelayFactor,
                             kVerificationRetryDelayMax);
  }
}

absl::StatusOr<google::protobuf::Any> KeyValueStore::GetAny(
    absl::string_view key, absl::Duration timeout) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(key));
  INTR_ASSIGN_OR_RETURN(absl::StatusOr<std::string> raw_key,
                        ZenohHandle::add_key_prefix(key, key_prefix_));
  LOG(INFO) << "KVStore Get for key: " << *raw_key;
  return GetAnyWithRawKey(*raw_key, timeout);
}

absl::StatusOr<std::string> KeyValueStore::GetRawWithRawKey(
    const std::string& raw_key, absl::Duration timeout) {
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Timeout must be zero or positive");
  }

  const stats::ScopedSpan get_span("KVStoreClient::GetRawWithRawKey");
  get_span.AddAttribute("kv.raw_key", raw_key);
  auto query_span = std::make_unique<stats::ScopedSpan>(
      "KVStoreClient::ZenohTransportQuery", get_span.span());


  std::string value;
  absl::Notification notif;
  absl::Status lambda_status = absl::NotFoundError("Key not found");
  // We can capture all variables by reference because we wait for the
  // imw_query() to finish before before returning. This ensures that all local
  // variables will outlive the callback.
  auto reply_functor = std::make_unique<imw_callback_functor_t>(
      [&value,
       &query_span,  
       &lambda_status](const char* unused_keyexpr, const void* response_bytes,
                       const size_t response_bytes_len) {

        if (query_span) {
          query_span->AddAttribute("zenoh.returned", true);
          query_span.reset();
        }

        if (response_bytes != nullptr) {
          value.assign(static_cast<const char*>(response_bytes),
                       response_bytes_len);
        } else {
          value.clear();
        }
        lambda_status = absl::OkStatus();
      });
  auto on_done_functor = std::make_unique<imw_on_done_functor_t>(
      [&notif](const char* unused_keyexpr) { notif.Notify(); });
  KVQuery query(std::move(reply_functor), std::move(on_done_functor));

  imw_query_options_t query_options{
      .timeout_ms = static_cast<uint64_t>(timeout / absl::Milliseconds(1))};
  imw_ret ret;
  bool returned = false;
  {
    ret = Zenoh().imw_query(raw_key.c_str(), zenoh_query_static_callback,
                            zenoh_query_static_on_done, nullptr, 0,
                            query.GetContext(), &query_options);
    if (ret != IMW_OK) {
      return absl::InternalError(
          absl::StrFormat("Error getting a key, return code: %d", ret));
    }
    returned = notif.WaitForNotificationWithTimeout(timeout + absl::Seconds(1));
  }
  if (!returned) {
    return absl::DeadlineExceededError("Timeout waiting for key");
  }
  if (!lambda_status.ok()) {
    return lambda_status;
  }

  return std::move(value);
}

absl::StatusOr<google::protobuf::Any> KeyValueStore::GetAnyWithRawKey(
    const std::string& raw_key, absl::Duration timeout) {

  const stats::ScopedSpan get_span("KVStoreClient::GetAny");
  get_span.AddAttribute("kv.raw_key", raw_key);


  INTR_ASSIGN_OR_RETURN(std::string raw_bytes,
                        GetRawWithRawKey(raw_key, timeout));


  const stats::ScopedSpan parse_span("KVStoreClient::ProtoDeserializeAny",
                                     get_span.span());
  parse_span.AddAttribute("payload.bytes",
                          static_cast<int64_t>(raw_bytes.size()));


  google::protobuf::Any value;
  if (!value.ParseFromString(raw_bytes)) {
    return absl::InternalError(
        absl::StrFormat("Failed to parse response for key '%s'", raw_key));
  }
  return value;
}

absl::StatusOr<KVQuery> KeyValueStore::GetAll(absl::string_view keyexpr,
                                              KeyValueCallback callback,
                                              OnDoneCallback on_done) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(keyexpr));
  INTR_ASSIGN_OR_RETURN(absl::StatusOr<std::string> prefixed_name,
                        ZenohHandle::add_key_prefix(keyexpr, key_prefix_));
  auto functor = std::make_unique<imw_callback_functor_t>(
      [callback = std::move(callback)](const char* key,
                                       const void* response_bytes,
                                       const size_t response_bytes_len) {
        auto value = std::make_unique<google::protobuf::Any>();
        static_cast<void>(value->ParseFromString(absl::string_view(
            static_cast<const char*>(response_bytes), response_bytes_len)));
        callback(key, std::move(value));
      });
  auto on_done_functor = std::make_unique<imw_on_done_functor_t>(
      [on_done = std::move(on_done)](const char* keyexpr) {
        on_done(absl::string_view(keyexpr));
      });
  KVQuery query(std::move(functor), std::move(on_done_functor));
  imw_ret_t ret = Zenoh().imw_query(
      prefixed_name->c_str(), zenoh_query_static_callback,
      zenoh_query_static_on_done, nullptr, 0, query.GetContext(), nullptr);
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error getting a key, return code: %d", ret));
  }

  return std::move(query);
}

absl::StatusOr<std::vector<std::string>> KeyValueStore::ListAllKeys(
    absl::Duration timeout) {
  if (key_prefix_ == kReplicationPrefix) {
    return absl::UnimplementedError(
        "ListAllKeys is not supported for replicated KV store; use "
        "ListAllOnpremKeys or ListAllGlobalKeys instead.");
  }
  std::vector<std::string> keys;
  absl::string_view query_keyexpr = "**";
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(query_keyexpr));
  INTR_ASSIGN_OR_RETURN(
      absl::StatusOr<std::string> prefixed_name,
      ZenohHandle::add_key_prefix(query_keyexpr, key_prefix_));
  return ExecuteList(prefixed_name.value(), timeout);
}

absl::StatusOr<std::vector<std::string>> KeyValueStore::ListAllGlobalKeys(
    absl::Duration timeout) {
  if (key_prefix_ == kDefaultKeyPrefix) {
    return absl::UnimplementedError(
        "ListAllGlobalKeys is only supported for replicated KV store; use "
        "ListAllKeys instead.");
  }
  std::vector<std::string> keys;
  absl::string_view query_keyexpr = "global/**";
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(query_keyexpr));
  INTR_ASSIGN_OR_RETURN(
      absl::StatusOr<std::string> prefixed_name,
      ZenohHandle::add_key_prefix(query_keyexpr, key_prefix_));
  return ExecuteList(prefixed_name.value(), timeout);
}

absl::StatusOr<std::vector<std::string>> KeyValueStore::ListAllOnpremKeys(
    absl::string_view workcell_name, absl::Duration timeout) {
  if (key_prefix_ == kDefaultKeyPrefix) {
    return absl::UnimplementedError(
        "ListAllOnpremKeys is only supported for replicated KV store; use "
        "ListAllKeys instead.");
  }
  std::vector<std::string> keys;
  std::string query_keyexpr = absl::StrFormat("%s/**", workcell_name);
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(query_keyexpr));
  INTR_ASSIGN_OR_RETURN(
      absl::StatusOr<std::string> prefixed_name,
      ZenohHandle::add_key_prefix(query_keyexpr, key_prefix_));
  return ExecuteList(prefixed_name.value(), timeout);
}

absl::Status KeyValueStore::Delete(absl::string_view key) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(key));
  INTR_ASSIGN_OR_RETURN(absl::StatusOr<std::string> prefixed_name,
                        ZenohHandle::add_key_prefix(key, key_prefix_));
  LOG(INFO) << "KVStore Delete for key: " << *prefixed_name;
  imw_ret_t ret = Zenoh().imw_delete_keyexpr(prefixed_name->c_str());
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error deleting a key, return code: %d", ret));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<std::string>> KeyValueStore::ExecuteList(
    absl::string_view keyexpr, absl::Duration timeout) {
  std::vector<std::string> keys;
  absl::Notification notif;
  auto callback = std::make_unique<imw_callback_functor_t>(
      [&keys, &notif](const char* keyexpr, const void* unused_response_bytes,
                      const size_t unused_response_bytes_len) {
        if (notif.HasBeenNotified()) {
          return;
        }
        keys.push_back(keyexpr);
      });
  auto on_done_functor = std::make_unique<imw_on_done_functor_t>(
      [&notif](const char* unused_keyexpr) { notif.Notify(); });
  KVQuery query(std::move(callback), std::move(on_done_functor));
  imw_query_options_t query_options{
      .timeout_ms = static_cast<uint64_t>(timeout / absl::Milliseconds(1))};
  imw_ret ret = Zenoh().imw_query(keyexpr.data(), zenoh_query_static_callback,
                                  zenoh_query_static_on_done, nullptr, 0,
                                  query.GetContext(), &query_options);
  if (ret != IMW_OK) {
    return absl::InternalError(
        absl::StrFormat("Error getting a key, return code: %d", ret));
  }
  notif.WaitForNotificationWithTimeout(timeout);
  return std::move(keys);
}

// We need to make a grpc call to the admin set service to copy the key value
// from the source key to the target key.
absl::Status KeyValueStore::AdminCloudCopy(absl::string_view source_key,
                                           absl::string_view target_key,
                                           absl::Duration timeout) {
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(source_key));
  INTR_RETURN_IF_ERROR(intrinsic::ValidZenohKey(target_key));
  INTR_ASSIGN_OR_RETURN(absl::StatusOr<std::string> source_prefixed_name,
                        ZenohHandle::add_key_prefix(source_key, key_prefix_));

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

  absl::flat_hash_map<std::string, google::protobuf::Any> results;
  absl::Notification done;
  absl::Mutex mutex;

  auto callback = [&results, &mutex](
                      absl::string_view key,
                      std::unique_ptr<google::protobuf::Any> value) {
    absl::MutexLock lock(&mutex);
    if (value != nullptr) {
      results[key] = std::move(*value);
    }
  };

  auto on_done = [&done](absl::string_view key) { done.Notify(); };

  INTR_ASSIGN_OR_RETURN(
      KVQuery query, GetAll(keyexpr, std::move(callback), std::move(on_done)));

  if (!done.WaitForNotificationWithTimeout(timeout)) {
    return absl::DeadlineExceededError(
        absl::StrFormat("Timeout waiting for GetAll on keyexpr: %s", keyexpr));
  }

  return results;
}

absl::StatusOr<Subscription> KeyValueStore::CreateSubscription(
    absl::string_view key_expression, const TopicConfig& config,
    SubscriptionOkExpandedCallback<google::protobuf::Any> value_callback,
    DeletionCallback deletion_callback) const {
  INTR_ASSIGN_OR_RETURN(
      std::string prefixed_key_expression,
      ZenohHandle::add_key_prefix(key_expression, key_prefix_));
  LOG(INFO) << "KVStore Subscribe for key: " << prefixed_key_expression;
  auto subscription_data = std::make_unique<SubscriptionData>();
  subscription_data->prefixed_name = prefixed_key_expression;
  auto callback = std::make_unique<imw_callback_functor_t>(
      [value_callback, deletion_callback](const char* keyexpr, const void* blob,
                                          const size_t blob_len) {
        if (blob == nullptr || blob_len == 0) {
          if (deletion_callback != nullptr) {
            deletion_callback(keyexpr);
          }
          return;
        }
        google::protobuf::Any msg;
        bool success = msg.ParseFromArray(blob, blob_len);
        if (!success) {
          LOG_EVERY_N(ERROR, 1)
              << "Deserializing message failed. Key expression: " << keyexpr;
          return;
        }
        value_callback(keyexpr, msg);
      });
  subscription_data->callback_functor = std::move(callback);

  imw_ret_t ret = Zenoh().imw_create_subscription(
      prefixed_key_expression.c_str(), zenoh_static_callback,
      intrinsic::PubSubQoSToZenohQos(config.topic_qos).c_str(),
      subscription_data->callback_functor.get());
  if (ret != IMW_OK) {
    return absl::InternalError("Error creating a subscription");
  }
  return Subscription(prefixed_key_expression, std::move(subscription_data));
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
