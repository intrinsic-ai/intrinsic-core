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

#include "intrinsic/icon/control/services/geometry_from_geometry_library.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

#include "grpcpp/server_builder.h"
#include "grpcpp/support/channel_arguments.h"
#include "internal/testing.h"
#include "intrinsic/geometry/api/geometry.h"
#include "intrinsic/geometry/api/shape_factory.h"
#include "intrinsic/geometry/proto/geometry_service.grpc.pb.h"
#include "intrinsic/geometry/service/in_memory_geometry_service.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/geometry/storage/geometry_service_storage.h"

namespace intrinsic::icon {
namespace {

TEST(GeometryFromGeometryLibraryTest, ValidStub) {
  auto geo_service = std::make_unique<InMemoryGeometryService>();
  grpc::ServerBuilder builder;
  builder.RegisterService(geo_service.get());
  std::unique_ptr<grpc::Server> geo_server = builder.BuildAndStart();
  ASSERT_NE(geo_server, nullptr);

  auto geo_lib = GetGeometryServiceGeometryLibrary(
      intrinsic_proto::geometry::GeometryService::NewStub(
          geo_server->InProcessChannel(grpc::ChannelArguments())));
  Geometry sphere = MakeSphere(1.0);

  ASSERT_OK_AND_ASSIGN(auto geo_refs,
                       geo_lib->Serializer().SaveGeometry(sphere));

  auto stub = intrinsic_proto::geometry::GeometryService::NewStub(
      geo_server->InProcessChannel(grpc::ChannelArguments()));
  ASSERT_OK_AND_ASSIGN(auto service,
                       GeometryFromGeometryLibrary::Create(std::move(stub)));
  EXPECT_NE(service->GetGeometryLibrary(), nullptr);

  ASSERT_OK_AND_ASSIGN(
      auto deserialized_sphere,
      service->GetGeometryLibrary()->Deserializer().GetGeometry(geo_refs, {}));
  EXPECT_EQ(deserialized_sphere, sphere);
}

}  // namespace
}  // namespace intrinsic::icon
