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

#ifndef INTRINSIC_PERCEPTION_STORAGE_CAPTURE_RESULT_STORAGE_H_
#define INTRINSIC_PERCEPTION_STORAGE_CAPTURE_RESULT_STORAGE_H_

#include <memory>

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"

namespace intrinsic::perception {

using KeyValueStoreFactory =
    absl::AnyInvocable<absl::StatusOr<std::shared_ptr<KeyValueStore>>(
        absl::string_view store) const>;

// Production factory: creates a KeyValueStore for `store` via PubSub.
KeyValueStoreFactory CreateKeyValueStoreFactory();

// Test factory: always returns the given `kvstore` (e.g. FakeKeyValueStore).
KeyValueStoreFactory CreateKeyValueStoreFactory(
    std::shared_ptr<KeyValueStore> kvstore);

// Retrieves a capture result from the given storage location.
absl::StatusOr<perception::CaptureResult> GetCaptureResult(
    const intrinsic_proto::kvstore::StorageLocation& capture_result_location,
    const KeyValueStoreFactory& kvstore_factory, absl::Duration timeout);

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_STORAGE_CAPTURE_RESULT_STORAGE_H_
