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

#ifndef INTRINSIC_UTIL_STATUS_GET_EXTENDED_STATUS_H_
#define INTRINSIC_UTIL_STATUS_GET_EXTENDED_STATUS_H_

#include <optional>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/rpc/status.pb.h"
#include "grpcpp/support/status.h"
#include "intrinsic/util/status/extended_status.pb.h"

namespace intrinsic {

std::optional<intrinsic_proto::status::ExtendedStatus> GetExtendedStatus(
    const absl::Status& status);

template <typename T>
std::optional<intrinsic_proto::status::ExtendedStatus> GetExtendedStatus(
    const absl::StatusOr<T>& status_or) {
  return GetExtendedStatus(status_or.status());
}

std::optional<intrinsic_proto::status::ExtendedStatus> GetExtendedStatus(
    const grpc::Status& status);

std::optional<intrinsic_proto::status::ExtendedStatus> GetExtendedStatus(
    const google::rpc::Status& status);

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_STATUS_GET_EXTENDED_STATUS_H_
