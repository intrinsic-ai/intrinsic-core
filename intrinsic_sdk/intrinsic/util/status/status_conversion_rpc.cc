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

#include "intrinsic/util/status/status_conversion_rpc.h"

#include <cstddef>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "google/rpc/status.pb.h"
#include "intrinsic/util/grpc/limits.h"
#include "intrinsic/util/proto/type_url.h"

namespace intrinsic {

google::rpc::Status ToGoogleRpcStatus(const absl::Status& status) {
  google::rpc::Status ret;
  ret.set_code(static_cast<int>(status.code()));
  ret.set_message(status.message());
  status.ForEachPayload(
      [&](absl::string_view type_url, const absl::Cord& payload) {
        if (!type_url.starts_with(kTypeUrlPrefix)) {
          LOG(WARNING)
              << "Status payload " << type_url
              << " is not a proper type URL, not serializing into RPC status";
        } else {
          google::protobuf::Any* any = ret.add_details();
          any->set_type_url(type_url);
          // NOLINTNEXTLINE(google3-readability-redundant-string-conversions)
          any->set_value(std::string(payload));
        }
      });
  const size_t absolute_payload_size = ret.ByteSizeLong();
  if (absolute_payload_size > kGrpcRecommendedMaxMetadataHardLimit) {
    LOG(WARNING) << absl::StrFormat(
        "absl::Status converted to RPC or gRPC status is larger than "
        "recommended metadata hard limit (%zu > %zu). Will very likely result "
        "in error.",
        absolute_payload_size, kGrpcRecommendedMaxMetadataHardLimit);
  } else if (absolute_payload_size > kGrpcRecommendedMaxMetadataSoftLimit) {
    LOG(WARNING) << absl::StrFormat(
        "absl::Status converted to RPC or gRPC status is larger than "
        "recommended metadata soft limit (%zu > %zu). Will likely result in "
        "error.",
        absolute_payload_size, kGrpcRecommendedMaxMetadataSoftLimit);
  } else if (absolute_payload_size >
             (kGrpcRecommendedMaxMetadataSoftLimit / 2)) {
    LOG(WARNING) << absl::StrFormat(
        "absl::Status converted to RPC or gRPC status is larger than half the "
        "recommended metadata soft limit (%zu > %zu). Will possibly result in "
        "error.",
        absolute_payload_size, (kGrpcRecommendedMaxMetadataSoftLimit / 2));
  }
  return ret;
}

absl::Status ToAbslStatus(const google::rpc::Status& status) {
  if (status.code() == 0) return absl::OkStatus();
  absl::Status ret(static_cast<absl::StatusCode>(status.code()),
                   status.message());
  for (const google::protobuf::Any& detail : status.details()) {
    ret.SetPayload(detail.type_url(), absl::Cord(detail.value()));
  }
  return ret;
}

absl::Status ToAbslStatusWithPayloads(const google::rpc::Status& status,
                                      const absl::Status& copy_payloads_from) {
  if (status.code() == 0) return absl::OkStatus();

  absl::Status ret(static_cast<absl::StatusCode>(status.code()),
                   status.message());

  // Copy the status we base this on to the returned status
  copy_payloads_from.ForEachPayload(
      [&ret](absl::string_view type_url, const absl::Cord& payload) {
        ret.SetPayload(type_url, payload);
      });

  // Now copy the status details to payloads
  for (const google::protobuf::Any& detail : status.details()) {
    if (detail.type_url() == absl::StrCat(kTypeUrlPrefix, "util.StatusProto")) {
      // Google-specific status information that we cannot parse
      continue;
    }
    ret.SetPayload(detail.type_url(), absl::Cord(detail.value()));
  }
  return ret;
}

}  // namespace intrinsic
