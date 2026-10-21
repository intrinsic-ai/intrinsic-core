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

#ifndef INTRINSIC_UTIL_STATUS_STATUS_CONVERSION_PROTO_H_
#define INTRINSIC_UTIL_STATUS_STATUS_CONVERSION_PROTO_H_

#include "absl/status/status.h"
#include "intrinsic/util/status/status.pb.h"

namespace intrinsic {

// Convert an absl::Status to a Intrinsic-specific StatusProto. The proto will
// contain the code. If the code is not Ok then it will also contain the message
// and any additional payloads that the input Status may have.
//
// The out parameter must not be a nullptr.
void SaveStatusToProto(const absl::Status& status,
                       intrinsic_proto::StatusProto* out);

// Convert a StatusProto to an absl::Status. This takes the code and message as
// well as payloads and creates a new absl::Status with that information.
absl::Status MakeStatusFromProto(const intrinsic_proto::StatusProto& proto);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STATUS_STATUS_CONVERSION_PROTO_H_
