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

#ifndef INTRINSIC_UTIL_GRPC_CHANNEL_H_
#define INTRINSIC_UTIL_GRPC_CHANNEL_H_

#include <memory>
#include <string>
#include <string_view>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "grpcpp/channel.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/util/grpc/channel_interface.h"
#include "intrinsic/util/grpc/connection_params.h"

namespace intrinsic {

// A channel to an Intrinsic gRPC service at the specified address.
class Channel : public ChannelInterface {
 public:
  struct OrgInfo {
    std::string org;
    std::string project;

    // Creates an OrgInfo from a string with the format "ORG@PROJECT".
    // Returns an error if the format is not as expected.
    static absl::StatusOr<OrgInfo> FromString(std::string_view org_project_str);
  };

  // Creates a channel to an Intrinsic gRPC service based on the provided
  // connection parameters.  `timeout` specifies the maximum amount of time to
  // wait for a response from the server before giving up on creating a channel.
  static absl::StatusOr<std::shared_ptr<Channel>> MakeFromAddress(
      const ConnectionParams& params,
      absl::Duration timeout = connect::kGrpcClientConnectDefaultTimeout);

  // Creates a channel to an Intrinsic gRPC service based on org info and
  // cluster. If cluster is empty, no server name will be added to metadata,
  // which is useful for services that are not cluster-specific like solution
  // discovery.
  static absl::StatusOr<std::shared_ptr<Channel>> MakeFromCluster(
      const OrgInfo& org_info, std::string_view cluster,
      std::string_view instance_name = "",
      std::string_view header = "x-resource-instance-name",
      absl::Duration timeout = connect::kGrpcClientConnectDefaultTimeout);

  // Creates a channel to an Intrinsic gRPC service based on org info and
  // solution name. This involves a call to solution discovery service to
  // resolve solution name to cluster name.
  static absl::StatusOr<std::shared_ptr<Channel>> MakeFromSolution(
      const OrgInfo& org_info, std::string_view solution_name,
      std::string_view instance_name = "",
      std::string_view header = "x-resource-instance-name",
      absl::Duration timeout = connect::kGrpcClientConnectDefaultTimeout);

  // Constructs a Channel with given connection parameters.
  explicit Channel(std::shared_ptr<grpc::Channel> channel,
                   std::string_view instance_name = "",
                   std::string_view header = "x-resource-instance-name");

  std::shared_ptr<grpc::Channel> GetChannel() const override;

  ClientContextFactory GetClientContextFactory() const override;

 private:
  std::shared_ptr<grpc::Channel> channel_;

  // The ingress instance name.  This determines which VirtualService in
  // kubernetes is targeted.  If empty, the header information is not added to
  // the gRPC connection.
  const std::string instance_name_;
  // The header to be used when establishing a gRPC connection to the ingress.
  // The header's value will be instance_name.
  const std::string header_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_UTIL_GRPC_CHANNEL_H_
