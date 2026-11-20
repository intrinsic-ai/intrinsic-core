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

#ifndef INTRINSIC_PERCEPTION_STORAGE_KVSTORE_FACTORY_H_
#define INTRINSIC_PERCEPTION_STORAGE_KVSTORE_FACTORY_H_

#include <memory>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/platform/pubsub/kvstore.h"

namespace intrinsic::perception {

// Factory callables must be safe for concurrent invocation across threads
// (e.g., during parallel multi-sensor `FromProto` / `ToProto` conversions).
using KeyValueStoreFactory =
    absl::AnyInvocable<absl::StatusOr<std::shared_ptr<KeyValueStore>>(
        absl::string_view store) const>;

// Non-owning reference counterpart to `KeyValueStoreFactory`. Referenced
// callables must likewise be safe for concurrent invocation across threads.
using KeyValueStoreFactoryRef =
    absl::FunctionRef<absl::StatusOr<std::shared_ptr<KeyValueStore>>(
        absl::string_view store)>;

// Production factory: creates a KeyValueStore for `store` via PubSub.
KeyValueStoreFactory CreateKeyValueStoreFactory();

// Test factory: always returns the given non-null `kvstore` (e.g.
// FakeKeyValueStore).
KeyValueStoreFactory CreateKeyValueStoreFactory(
    absl_nonnull std::shared_ptr<KeyValueStore> kvstore);

// Returns a reference to a lazily initialized process-wide default factory
// backed by PubSub.
const KeyValueStoreFactory& DefaultKeyValueStoreFactory();

}  // namespace intrinsic::perception

#endif  // INTRINSIC_PERCEPTION_STORAGE_KVSTORE_FACTORY_H_
