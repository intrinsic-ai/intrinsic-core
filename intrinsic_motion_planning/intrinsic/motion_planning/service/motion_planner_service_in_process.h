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

#ifndef INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_IN_PROCESS_H_
#define INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_IN_PROCESS_H_

#include <memory>
#include <string>
#include <string_view>

#include "grpcpp/server.h"
#include "intrinsic/geometry/storage/geometry_library.h"
#include "intrinsic/motion_planning/motion_planner/motion_planner_flags.h"
#include "intrinsic/motion_planning/proto/v1/motion_planner_service.grpc.pb.h"
#include "intrinsic/motion_planning/service/motion_planner_service.h"
#include "intrinsic/world/proto/object_world_service.grpc.pb.h"
#include "intrinsic/world/service/test/world_service_fake.h"

namespace intrinsic {

// Sets up a grpc server running the full motion planner service internally.
// This is just for testing as our usual setup includes a shared motion planner
// service running in its own container.
class MotionPlannerServiceInProcess {
 public:
  MotionPlannerServiceInProcess();
  ~MotionPlannerServiceInProcess();

  std::string GetAddress() const { return address_; }

  // Build a MotionPlannerService with an optional non-owned GeometryLibrary.
  //
  // If `geometry_library` is nullptr, this creates an in-memory
  // GeometryLibrary to use instead.
  static std::unique_ptr<MotionPlannerServiceInProcess> Create(
      FakeWorldService* fake_world_service);

  static std::unique_ptr<MotionPlannerServiceInProcess> Create(
      intrinsic_proto::world::internal::WorldService::StubInterface*
          world_service,
      intrinsic_proto::world::ObjectWorldService::StubInterface*
          object_world_service,
      std::shared_ptr<GeometryLibrary> geometry_library,
      const MotionPlannerFlags& flags = {});

  std::shared_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>
  GetStub() const;

  std::unique_ptr<
      intrinsic_proto::motion_planning::v1::MotionPlannerService::Stub>
  GetUniqueStub() const;

 private:
  // Local address of the service.
  std::string address_;
  std::unique_ptr<MotionPlannerService> service_;
  // Holds a reference to service internally.
  std::unique_ptr<grpc::Server> server_;

  // When we use this with pybind, we need to take ownership of these two
  // pointers.
  std::unique_ptr<intrinsic_proto::world::internal::WorldService::StubInterface>
      world_service_stub_;
  std::unique_ptr<intrinsic_proto::world::ObjectWorldService::StubInterface>
      object_world_service_stub_;
  std::shared_ptr<GeometryLibrary> geometry_library_;
};

}  // namespace intrinsic

#endif  // INTRINSIC_MOTION_PLANNING_SERVICE_MOTION_PLANNER_SERVICE_IN_PROCESS_H_
