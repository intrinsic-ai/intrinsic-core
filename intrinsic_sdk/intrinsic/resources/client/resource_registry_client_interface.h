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

#ifndef INTRINSIC_RESOURCES_CLIENT_RESOURCE_REGISTRY_CLIENT_INTERFACE_H_
#define INTRINSIC_RESOURCES_CLIENT_RESOURCE_REGISTRY_CLIENT_INTERFACE_H_

#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "intrinsic/resources/proto/resource_registry.pb.h"

namespace intrinsic {
namespace resources {

// A client interface for the public resource registry service.
class ResourceRegistryClientInterface {
 public:
  virtual ~ResourceRegistryClientInterface() = default;

  virtual absl::StatusOr<
      std::vector<intrinsic_proto::resources::ResourceInstance>>
  ListResources(const intrinsic_proto::resources::ListResourceInstanceRequest::
                    StrictFilter& filter) const = 0;

  virtual absl::StatusOr<intrinsic_proto::resources::ResourceInstance>
  GetResource(absl::string_view id) const = 0;
};

}  // namespace resources
}  // namespace intrinsic

#endif  // INTRINSIC_RESOURCES_CLIENT_RESOURCE_REGISTRY_CLIENT_INTERFACE_H_
