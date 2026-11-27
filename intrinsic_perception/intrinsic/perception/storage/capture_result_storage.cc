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
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/perception/cameras/capture_result.h"
#include "intrinsic/perception/proto/v1/capture_result.pb.h"
#include "intrinsic/perception/proto_conversion/v1/capture_result.h"
#include "intrinsic/perception/storage/kvstore_factory.h"
#include "intrinsic/perception/storage/kvstore_options.h"
#include "intrinsic/perception/storage/kvstore_utils.h"
#include "intrinsic/platform/pubsub/kvstore.h"
#include "intrinsic/platform/pubsub/storage_location.pb.h"
#include "intrinsic/util/status/status_macros.h"
#include "intrinsic/util/time/deadline_timeout.h"

namespace intrinsic::perception {

absl::StatusOr<perception::CaptureResult> GetCaptureResult(
    const intrinsic_proto::kvstore::StorageLocation& capture_result_location,
    KeyValueStoreFactoryRef kvstore_factory, absl::Duration timeout) {
  const absl::Time deadline = ToDeadline(timeout);
  INTR_ASSIGN_OR_RETURN(
      std::shared_ptr<KeyValueStore> kvstore,
      ResolveKeyValueStore(capture_result_location.store(), kvstore_factory));
  INTR_ASSIGN_OR_RETURN(
      intrinsic_proto::perception::v1::CaptureResult capture_result_proto,
      GetWithTransientKeyNotFoundRetry<
          intrinsic_proto::perception::v1::CaptureResult>(
          capture_result_location.key(), deadline,
          [&](absl::Duration remaining_timeout) {
            return kvstore->Get<intrinsic_proto::perception::v1::CaptureResult>(
                capture_result_location.key(), remaining_timeout);
          }));

  auto single_store_factory = [&](absl::string_view store)
      -> absl::StatusOr<std::shared_ptr<KeyValueStore>> {
    if (store == capture_result_location.store()) {
      return kvstore;
    }
    return ResolveKeyValueStore(store, kvstore_factory);
  };

  return intrinsic_proto::perception::v1::FromProto(
      std::move(capture_result_proto), {.kvstore_factory = single_store_factory,
                                        .timeout = ToTimeout(deadline)});
}

}  // namespace intrinsic::perception
