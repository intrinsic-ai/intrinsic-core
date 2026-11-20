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

#ifndef INTRINSIC_PLATFORM_PUBSUB_FAKE_KVSTORE_H_
#define INTRINSIC_PLATFORM_PUBSUB_FAKE_KVSTORE_H_

#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "google/protobuf/any.pb.h"
#include "intrinsic_sdk/intrinsic/middleware/buffers/immutable_buffer.h"
#include "intrinsic/platform/pubsub/kvstore.h"

namespace intrinsic {

// A fake implementation of KeyValueStore for use in unit tests.
//
// This class stores key-value pairs in an in-memory `absl::flat_hash_map`.
// It avoids dependencies on Zenoh or real PubSub infrastructure, making it
// fast and suitable for testing components that require a KeyValueStore.
//
// Example usage:
//   std::unique_ptr<KeyValueStore> kv_store =
//    std::make_unique<FakeKeyValueStore>();
//   kv_store->Set("my_key", my_proto_value);
//   // Pass kv_store to the class under test.
class FakeKeyValueStore : public KeyValueStore {
 public:
  FakeKeyValueStore() : KeyValueStore(/*prefix_override=*/std::nullopt) {}
  explicit FakeKeyValueStore(std::optional<std::string> prefix_override)
      : KeyValueStore(std::move(prefix_override)) {}
  ~FakeKeyValueStore() override = default;

  using KeyValueStore::GetRaw;
  using KeyValueStore::Set;
  using KeyValueStore::SetRaw;
  using KeyValueStore::SetRawWithVerification;
  using KeyValueStore::SetWithVerification;

  // Stores the key-value pair in memory.
  absl::Status Set(absl::string_view key,
                   const google::protobuf::Any& value) override;

  // Stores the key-value pair in memory, satisfying verification immediately.
  absl::Status SetWithVerification(
      absl::string_view key, const google::protobuf::Any& value,
      const SetWithVerificationOptions& options) override;

  absl::Status SetRaw(absl::string_view key, ImmutableBuffer value) override;

  absl::Status SetRawWithVerification(
      absl::string_view key, ImmutableBuffer value,
      const SetWithVerificationOptions& options) override;

  absl::StatusOr<ImmutableBuffer> GetRaw(absl::string_view key,
                                         absl::Duration timeout) override;

  absl::Status Delete(absl::string_view key) override;

 protected:
  absl::StatusOr<google::protobuf::Any> GetAny(absl::string_view key,
                                               absl::Duration timeout) override;

 private:
  template <typename T>
  absl::StatusOr<T> GetTyped(absl::string_view key, absl::Duration timeout,
                             absl::string_view mismatch_error);

  absl::Mutex mutex_;
  absl::flat_hash_map<std::string,
                      std::variant<google::protobuf::Any, ImmutableBuffer>>
      store_ ABSL_GUARDED_BY(mutex_);
};

}  // namespace intrinsic

#endif  // INTRINSIC_PLATFORM_PUBSUB_FAKE_KVSTORE_H_
