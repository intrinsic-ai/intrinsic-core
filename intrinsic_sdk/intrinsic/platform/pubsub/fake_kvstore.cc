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

#include "intrinsic/platform/pubsub/fake_kvstore.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/buffer_factory.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/zenoh_util/zenoh_helpers.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic {
namespace {

// Matches a single path segment pattern against a concrete path segment.
// In Zenoh key expressions:
// - `*` matches the entire segment.
// - `$*` matches zero or more characters within the segment (e.g. `foo$*bar`).
// - literal characters match exactly.
bool MatchesSegment(absl::string_view pattern, absl::string_view segment) {
  if (pattern == "*" || pattern == "$*") {
    return true;
  }
  if (!absl::StrContains(pattern, "$*")) {
    return pattern == segment;
  }
  std::vector<absl::string_view> sub_patterns = absl::StrSplit(pattern, "$*");

  // Segment must start with the prefix before the first $*
  if (!absl::StartsWith(segment, sub_patterns.front())) {
    return false;
  }
  segment.remove_prefix(sub_patterns.front().size());

  // Segment must end with the suffix after the last $*
  if (!absl::EndsWith(segment, sub_patterns.back())) {
    return false;
  }
  segment.remove_suffix(sub_patterns.back().size());

  // Match all intermediate sub-patterns greedily from left to right.
  for (size_t i = 1; i + 1 < sub_patterns.size(); ++i) {
    absl::string_view sub = sub_patterns[i];
    if (sub.empty()) {
      continue;
    }
    size_t pos = segment.find(sub);
    if (pos == absl::string_view::npos) {
      return false;
    }
    segment.remove_prefix(pos + sub.size());
  }

  return true;
}

// Recursive segment matcher that handles `**` (matching zero or more segments).
bool MatchesSegments(const std::vector<absl::string_view>& p_segs, size_t p_idx,
                     const std::vector<absl::string_view>& k_segs,
                     size_t k_idx) {
  if (p_idx == p_segs.size()) {
    return k_idx == k_segs.size();
  }

  if (p_segs[p_idx] == "**") {
    // `**` can match 0 to (k_segs.size() - k_idx) segments.
    for (size_t skip = 0; k_idx + skip <= k_segs.size(); ++skip) {
      if (MatchesSegments(p_segs, p_idx + 1, k_segs, k_idx + skip)) {
        return true;
      }
    }
    return false;
  }

  if (k_idx == k_segs.size()) {
    return false;
  }

  if (!MatchesSegment(p_segs[p_idx], k_segs[k_idx])) {
    return false;
  }

  return MatchesSegments(p_segs, p_idx + 1, k_segs, k_idx + 1);
}

}  // namespace

absl::Status FakeKeyValueStore::Set(absl::string_view key,
                                    const google::protobuf::Any& value) {
  INTR_RETURN_IF_ERROR(ValidZenohKey(key));

  absl::MutexLock lock(&mutex_);
  store_[std::string(key)] = value;
  return absl::OkStatus();
}

absl::Status FakeKeyValueStore::SetWithVerification(
    absl::string_view key, const google::protobuf::Any& value,
    const SetWithVerificationOptions& options) {
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "SetWithVerificationOptions::timeout must be positive");
  }
  return Set(key, value);
}

absl::Status FakeKeyValueStore::SetRaw(absl::string_view key,
                                       ImmutableBuffer value) {
  if (!value.valid()) {
    return absl::InvalidArgumentError("Cannot set an invalid ImmutableBuffer");
  }
  INTR_RETURN_IF_ERROR(ValidZenohKey(key));
  // Preserve the 256-byte alignment contract of `GetRaw`, `GetAllRaw`, and
  // `CreateRawSubscription` when storing an unaligned `Slice()`.
  if (value.alignment() < ImmutableBuffer::kAlignment) {
    INTR_ASSIGN_OR_RETURN(value,
                          CopyToImmutableBuffer(value.data(), value.size()));
  }

  absl::MutexLock lock(&mutex_);
  store_[std::string(key)] = value;
  return absl::OkStatus();
}

absl::Status FakeKeyValueStore::SetRawWithVerification(
    absl::string_view key, ImmutableBuffer value,
    const SetWithVerificationOptions& options) {
  if (options.timeout <= absl::ZeroDuration()) {
    return absl::InvalidArgumentError(
        "SetWithVerificationOptions::timeout must be positive");
  }
  return SetRaw(key, std::move(value));
}

absl::StatusOr<ImmutableBuffer> FakeKeyValueStore::GetRaw(
    absl::string_view key, absl::Duration timeout) {
  return GetTyped<ImmutableBuffer>(
      key, timeout, "' stores a Protobuf message, not raw bytes");
}

absl::StatusOr<KVQuery> FakeKeyValueStore::GetAll(absl::string_view key,
                                                  KeyValueCallback callback,
                                                  OnDoneCallback on_done) {
  return CollectMatches<google::protobuf::Any>(
      key, std::move(callback),
      [](const KeyValueCallback& cb, absl::string_view prefixed_key,
         const google::protobuf::Any& val) {
        cb(prefixed_key, std::make_unique<google::protobuf::Any>(val));
      },
      std::move(on_done));
}

absl::StatusOr<KVQuery> FakeKeyValueStore::GetAllRaw(
    absl::string_view keyexpr, RawKeyValueCallback callback,
    OnDoneCallback on_done, RawQueryErrorCallback /*on_error*/) {
  return CollectMatches<ImmutableBuffer>(
      keyexpr, std::move(callback),
      [](const RawKeyValueCallback& cb, absl::string_view prefixed_key,
         const ImmutableBuffer& val) { cb(prefixed_key, val); },
      std::move(on_done));
}

absl::Status FakeKeyValueStore::Delete(absl::string_view key) {
  INTR_RETURN_IF_ERROR(ValidZenohKey(key));

  absl::MutexLock lock(&mutex_);
  auto it = store_.find(key);
  if (it == store_.end()) {
    return absl::NotFoundError(absl::StrCat("Key not found: ", key));
  }
  store_.erase(it);
  return absl::OkStatus();
}

absl::StatusOr<google::protobuf::Any> FakeKeyValueStore::GetAny(
    absl::string_view key, absl::Duration timeout) {
  return GetTyped<google::protobuf::Any>(
      key, timeout, "' stores raw bytes, not a Protobuf message");
}

template <typename T>
absl::StatusOr<T> FakeKeyValueStore::GetTyped(
    absl::string_view key, absl::Duration timeout,
    absl::string_view mismatch_error) {
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Timeout must be zero or positive");
  }
  INTR_RETURN_IF_ERROR(ValidZenohKey(key));

  absl::MutexLock lock(&mutex_);
  auto it = store_.find(key);
  if (it == store_.end()) {
    return absl::NotFoundError(absl::StrCat("Key not found: ", key));
  }
  if (!std::holds_alternative<T>(it->second)) {
    return absl::InvalidArgumentError(
        absl::StrCat("Key '", key_prefix(), "/", key, mismatch_error));
  }
  return std::get<T>(it->second);
}

template <typename T, typename CallbackFn, typename EmitFn>
absl::StatusOr<KVQuery> FakeKeyValueStore::CollectMatches(
    absl::string_view keyexpr, CallbackFn callback, EmitFn emit,
    OnDoneCallback on_done) {
  INTR_RETURN_IF_ERROR(ValidZenohKeyexpr(keyexpr));

  const std::vector<absl::string_view> pattern_segments =
      absl::StrSplit(keyexpr, '/');
  std::vector<std::pair<std::string, T>> matches;
  {
    absl::MutexLock lock(&mutex_);
    for (const auto& [entry_key, entry_val] : store_) {
      if (std::holds_alternative<T>(entry_val) &&
          MatchesSegments(pattern_segments, 0, absl::StrSplit(entry_key, '/'),
                          0)) {
        matches.emplace_back(absl::StrCat(key_prefix(), "/", entry_key),
                             std::get<T>(entry_val));
      }
    }
  }

  if (callback != nullptr) {
    for (const auto& [prefixed_key, val] : matches) {
      emit(callback, prefixed_key, val);
    }
  }

  if (on_done) {
    on_done(absl::StrCat(key_prefix(), "/", keyexpr));
  }

  return KVQuery();
}

absl::StatusOr<std::vector<std::string>> FakeKeyValueStore::ExecuteList(
    absl::string_view keyexpr, absl::Duration timeout) {
  if (timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Timeout must be zero or positive");
  }
  INTR_RETURN_IF_ERROR(ValidZenohKeyexpr(keyexpr));

  const std::vector<absl::string_view> pattern_segments =
      absl::StrSplit(keyexpr, '/');
  std::vector<std::string> keys;
  absl::MutexLock lock(&mutex_);
  for (const auto& [entry_key, unused_val] : store_) {
    std::string prefixed_key = absl::StrCat(key_prefix(), "/", entry_key);
    if (MatchesSegments(pattern_segments, 0, absl::StrSplit(prefixed_key, '/'),
                        0)) {
      keys.push_back(std::move(prefixed_key));
    }
  }
  return keys;
}

absl::StatusOr<KVQuery> FakeKeyValueStore::ExecuteGetAll(
    absl::string_view keyexpr, KeyValueCallback callback,
    OnDoneCallback on_done, std::optional<absl::Duration> timeout) {
  if (timeout.has_value() && *timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Timeout must be zero or positive");
  }
  return GetAll(keyexpr, std::move(callback), std::move(on_done));
}

absl::StatusOr<KVQuery> FakeKeyValueStore::ExecuteGetAllRaw(
    absl::string_view keyexpr, RawKeyValueCallback callback,
    OnDoneCallback on_done, RawQueryErrorCallback on_error,
    std::optional<absl::Duration> timeout) {
  if (timeout.has_value() && *timeout < absl::ZeroDuration()) {
    return absl::InvalidArgumentError("Timeout must be zero or positive");
  }
  return GetAllRaw(keyexpr, std::move(callback), std::move(on_done),
                   std::move(on_error));
}

}  // namespace intrinsic
