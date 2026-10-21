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

#ifndef INTRINSIC_UTIL_STATUS_STATUS_CONVERSION_RPC_H_
#define INTRINSIC_UTIL_STATUS_STATUS_CONVERSION_RPC_H_

#include "absl/status/status.h"
#include "google/rpc/status.pb.h"

namespace intrinsic {

google::rpc::Status ToGoogleRpcStatus(const absl::Status& status);
absl::Status ToAbslStatus(const google::rpc::Status& status);
absl::Status ToAbslStatusWithPayloads(const google::rpc::Status& status,
                                      const absl::Status& copy_payloads_from);

// Aliases for backward compatibility
inline google::rpc::Status SaveStatusAsRpcStatus(const absl::Status& status) {
  return ToGoogleRpcStatus(status);
}

inline absl::Status MakeStatusFromRpcStatus(const google::rpc::Status& status) {
  return ToAbslStatus(status);
}

inline absl::Status MakeStatusFromRpcStatusWithPayloads(
    const google::rpc::Status& status, const absl::Status& copy_payloads_from) {
  return ToAbslStatusWithPayloads(status, copy_payloads_from);
}

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STATUS_STATUS_CONVERSION_RPC_H_
