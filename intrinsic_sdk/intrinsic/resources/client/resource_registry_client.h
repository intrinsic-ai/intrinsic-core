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

#ifndef INTRINSIC_RESOURCES_CLIENT_RESOURCE_REGISTRY_CLIENT_H_
#define INTRINSIC_RESOURCES_CLIENT_RESOURCE_REGISTRY_CLIENT_H_

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "intrinsic/connect/cc/grpc/channel.h"
#include "intrinsic/resources/client/resource_registry_client_interface.h"
#include "intrinsic/resources/proto/resource_registry.grpc.pb.h"
#include "intrinsic/resources/proto/resource_registry.pb.h"

namespace intrinsic {
namespace resources {

class ResourceRegistryClient;

// Creates a client that connects to the public resource registry service.
// Parameter connection_timeout is used when establishing the initial connection
// to the service. Parameter timeout is the timeout used for every request.
absl::StatusOr<std::unique_ptr<ResourceRegistryClient>>
CreateResourceRegistryClient(
    absl::string_view grpc_address, absl::Duration timeout = absl::Seconds(60),
    absl::Duration connection_timeout =
        intrinsic::connect::kGrpcClientConnectDefaultTimeout);

// A client for the public resource registry service.
class ResourceRegistryClient : public ResourceRegistryClientInterface {
 public:
  explicit ResourceRegistryClient(
      std::unique_ptr<
          intrinsic_proto::resources::ResourceRegistry::StubInterface>
          stub,
      absl::Duration timeout)
      : stub_(std::move(stub)), timeout_(timeout) {}

  absl::StatusOr<std::vector<intrinsic_proto::resources::ResourceInstance>>
  ListResources(const intrinsic_proto::resources::ListResourceInstanceRequest::
                    StrictFilter& filter) const override;

  absl::StatusOr<intrinsic_proto::resources::ResourceInstance> GetResource(
      absl::string_view name) const override;

 private:
  std::unique_ptr<intrinsic_proto::resources::ResourceRegistry::StubInterface>
      stub_;
  const absl::Duration timeout_;
};

}  // namespace resources
}  // namespace intrinsic

#endif  // INTRINSIC_RESOURCES_CLIENT_RESOURCE_REGISTRY_CLIENT_H_
