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

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"

namespace intrinsic {

absl::Status FakeKeyValueStore::Set(absl::string_view key,
                                    const google::protobuf::Any& value) {
  absl::MutexLock lock(&mutex_);
  store_[std::string(key)] = value;
  return absl::OkStatus();
}

absl::Status FakeKeyValueStore::SetWithVerification(
    absl::string_view key, const google::protobuf::Any& value,
    const SetWithVerificationOptions& /*options*/) {
  return Set(key, value);
}

absl::Status FakeKeyValueStore::Delete(absl::string_view key) {
  absl::MutexLock lock(&mutex_);
  if (!store_.contains(key)) {
    return absl::NotFoundError(absl::StrCat("Key not found: ", key));
  }
  store_.erase(key);
  return absl::OkStatus();
}

absl::StatusOr<google::protobuf::Any> FakeKeyValueStore::GetAny(
    absl::string_view key, absl::Duration timeout) {
  absl::MutexLock lock(&mutex_);
  auto it = store_.find(key);
  if (it == store_.end()) {
    return absl::NotFoundError(absl::StrCat("Key not found: ", key));
  }
  return it->second;
}

}  // namespace intrinsic
