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

#ifndef INTRINSIC_ICON_SERVER_TESTING_FAKE_PART_COLLECTION_H_
#define INTRINSIC_ICON_SERVER_TESTING_FAKE_PART_COLLECTION_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "intrinsic/icon/proto/part_status.pb.h"
#include "intrinsic/icon/proto/v1/types.pb.h"
#include "intrinsic/icon/server/part_collection.h"

namespace intrinsic::icon {

static constexpr char kFakePartCollectionBoolPropertyName[] = "bool_prop";
static constexpr char kFakePartCollectionDoublePropertyName[] = "double_prop";
static constexpr char kFakePartCollectionPayloadPropertyName[] = "payload_prop";

// Creates a PartCollection that will return (otherwise empty) PartSignature,
// PartConfig and PartStatus data with the names in `part_names`, as well as an
// empty SafetyStatus.
absl::StatusOr<PartCollection> FakePartCollectionNamesOnly(
    const absl::flat_hash_set<std::string>& part_names);

// Creates a PartCollection that will return the given `statuses`, and
// (otherwise empty) PartSignature / PartConfig data with the part names from
// `statuses`, as well as an empty SafetyStatus
absl::StatusOr<PartCollection> FakePartCollectionWithStatuses(
    const absl::flat_hash_map<std::string, intrinsic_proto::icon::PartStatus>&
        statuses);

// Creates a PartCollection that will return the given `part_configs` and empty
// PartStatus / SafetyStatus data.
absl::StatusOr<PartCollection> FakePartCollectionWithConfigs(
    std::vector<intrinsic_proto::icon::v1::PartConfig> part_configs);

}  // namespace intrinsic::icon

#endif  // INTRINSIC_ICON_SERVER_TESTING_FAKE_PART_COLLECTION_H_
