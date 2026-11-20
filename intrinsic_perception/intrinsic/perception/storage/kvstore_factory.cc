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

#include "intrinsic/perception/storage/kvstore_factory.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/log/die_if_null.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/util/status/status_macros.h"

namespace intrinsic::perception {

KeyValueStoreFactory CreateKeyValueStoreFactory() {
  static const absl::NoDestructor<PubSub> kPubSub;
  const PubSub* pubsub = kPubSub.get();
  return [pubsub](absl::string_view store)
             -> absl::StatusOr<std::shared_ptr<KeyValueStore>> {
    std::optional<std::string> prefix =
        store.empty() ? std::nullopt : std::make_optional(std::string(store));
    INTR_ASSIGN_OR_RETURN(KeyValueStore kvstore,
                          pubsub->KeyValueStore(std::move(prefix)));
    return std::make_shared<KeyValueStore>(std::move(kvstore));
  };
}

KeyValueStoreFactory CreateKeyValueStoreFactory(
    absl_nonnull std::shared_ptr<KeyValueStore> kvstore) {
  return
      [kvstore =
           ABSL_DIE_IF_NULL(std::move(kvstore))](absl::string_view /*store*/)
          -> absl::StatusOr<std::shared_ptr<KeyValueStore>> { return kvstore; };
}

const KeyValueStoreFactory& DefaultKeyValueStoreFactory() {
  static const absl::NoDestructor<KeyValueStoreFactory> kFactory(
      CreateKeyValueStoreFactory());
  return *kFactory;
}

}  // namespace intrinsic::perception
