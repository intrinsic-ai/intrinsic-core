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

#ifndef INTRINSIC_UTIL_PROTO_MERGE_H_
#define INTRINSIC_UTIL_PROTO_MERGE_H_

#include "absl/status/status.h"
#include "google/protobuf/message.h"

namespace intrinsic {

// For any field set in `from` that isn't set in `to`, copies the field from
// `from` to `to`, except for unknown fields. For fields that are submessages,
// presence of the submessage is checked, and if copied, copies the entire
// submessage from `from`, does not check for presence of fields in submessage.
//
// Singular fields are only considered set if
// google::protobuf::Reflection::HasField(field) would return true, and repeated
// fields will only be listed if google::protobuf::Reflection::FieldSize(field)
// would return non-zero.
absl::Status MergeUnset(const google::protobuf::Message& from,
                        google::protobuf::Message& to);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_PROTO_MERGE_H_
