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

#include "intrinsic/perception/storage/capture_result_storage.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/perception/proto_conversion/v1/capture_result.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/pubsub.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/time/deadline_timeout.h"

namespace intrinsic::perception {

KeyValueStoreFactory CreateKeyValueStoreFactory() {
  return [pubsub = PubSub()](absl::string_view store)
             -> absl::StatusOr<std::shared_ptr<KeyValueStore>> {
    std::optional<std::string> prefix =
        store.empty() ? std::nullopt : std::make_optional(std::string(store));
    INTR_ASSIGN_OR_RETURN(KeyValueStore kvstore, pubsub.KeyValueStore(prefix));
    return std::make_shared<KeyValueStore>(std::move(kvstore));
  };
}

KeyValueStoreFactory CreateKeyValueStoreFactory(
    std::shared_ptr<KeyValueStore> kvstore) {
  return
      [kvstore = std::move(kvstore)](absl::string_view /*store*/)
          -> absl::StatusOr<std::shared_ptr<KeyValueStore>> { return kvstore; };
}

absl::StatusOr<perception::CaptureResult> GetCaptureResult(
    const intrinsic_proto::kvstore::StorageLocation& capture_result_location,
    const KeyValueStoreFactory& kvstore_factory, absl::Duration timeout) {
  const absl::Time deadline = ToDeadline(timeout);
  INTR_ASSIGN_OR_RETURN(std::shared_ptr<KeyValueStore> kvstore,
                        kvstore_factory(capture_result_location.store()));
  INTR_ASSIGN_OR_RETURN(
      auto capture_result_proto,
      kvstore->Get<intrinsic_proto::perception::v1::CaptureResult>(
          capture_result_location.key(), ToTimeout(deadline)));
  return FromProto(std::move(capture_result_proto));
}

}  // namespace intrinsic::perception
