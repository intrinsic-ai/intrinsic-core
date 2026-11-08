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

#ifndef INTRINSIC_WORLD_SERVICE_TEST_UTILS_H_
#define INTRINSIC_WORLD_SERVICE_TEST_UTILS_H_

#include <memory>
#include <string>

#include "absl/log/check.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "grpcpp/server_builder.h"
#include "intrinsic/geometry/service/in_memory_geometry_service.h"
#include "intrinsic/world/service/test/world_service_fake.h"
#include "intrinsic/world/world.h"

namespace intrinsic {

struct WorldServers {
  std::unique_ptr<geo::InMemoryGeometryService> geometry_service;
  std::unique_ptr<grpc::Server> geometry_server;
  std::string geometry_service_address;
  std::unique_ptr<FakeWorldService> world_service;
};

// Initializes a FakeWorldService and geo::InMemoryGeometryService from a world
// .gzf file. This method can be used for tests which require initializing ICON
// with a world service.
absl::StatusOr<WorldServers> InitializeWorldService(
    absl::string_view world_gzf_path);

// Initializes a FakeWorldService and geo::InMemoryGeometryService from a world
// object. This method can be used for tests which require initializing ICON
// with a world service.
//
// Worlds can be sourced from a few places:
//   - Golden files via intrinsic/world/gzfile/gzfile.h
//   - Built via World::CreateEmptyWorld() and world/test/world_test_utils.h
absl::StatusOr<WorldServers> InitializeWorldService(World world);

}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_TEST_UTILS_H_
