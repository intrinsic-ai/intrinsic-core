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

#include "intrinsic/conductor/resource_world_wrapper.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "intrinsic/conductor/proto/wrapper_bridge.pb.h"
#include "intrinsic/config/proto/resource_set.pb.h"
#include "intrinsic/resources/proto/geometric_resource_data.pb.h"
#include "intrinsic/util/cgo/c_types.h"
#include "intrinsic/util/cgo/cpp_types.h"
#include "intrinsic/util/status/status_macros.h"

extern "C" {
void go_c_ResourceWorldCreate(go_c_StringIn hssAddress,
                              go_c_StringIn owsAddress,
                              go_c_Handle readerHandle, go_c_Handle* handleOut,
                              go_c_StatusOut statusOut);
void go_c_ResourceReaderCreate(go_c_StringIn hssAddress,
                               go_c_StringIn rtrAddress, go_c_Handle* handleOut,
                               go_c_StatusOut statusOut);
void go_c_ResourceWorldUpdateResourceSetWorldRelations(
    go_c_Handle handle, go_c_StringIn localWorldID, go_c_StatusOut statusOut);
void go_c_ResourceReaderGeometricResourceInstanceData(go_c_Handle handle,
                                                      go_c_ProtoIn rsIn,
                                                      go_c_ProtoOut out,
                                                      go_c_StatusOut statusOut);
void go_c_ResourceReaderGeometricResourceSetData(go_c_Handle handle,
                                                 go_c_ProtoOut out,
                                                 go_c_StatusOut statusOut);
void go_c_ResourceWorldClose(go_c_Handle handle);
void go_c_ResourceReaderClose(go_c_Handle handle);
}

namespace intrinsic::conductor {

using ::intrinsic::conductor::internal::GeometricResourceSetData;

absl::StatusOr<std::unique_ptr<ResourceWorld>> ResourceWorld::Create(
    const std::string& hss_address, const std::string& ows_address,
    std::unique_ptr<const ResourceReaderInterface> reader) {
  if (reader == nullptr) {
    return absl::InvalidArgumentError(
        "ResourceWorld::Create requires a non-null ResourceReader");
  }

  go_c_Handle handle = 0;
  INTR_RETURN_IF_ERROR(intrinsic::GetStatus([&](go_c_StatusOut status_out) {
    go_c_ResourceWorldCreate(intrinsic::ToStringIn(hss_address),
                             intrinsic::ToStringIn(ows_address),
                             reader->GetHandle(), &handle, status_out);
  }));
  if (handle == 0) {
    return absl::InternalError("ResourceWorld create returned null handle");
  }
  return std::unique_ptr<ResourceWorld>(
      new ResourceWorld(handle, std::move(reader)));
}

ResourceWorld::~ResourceWorld() {
  if (handle_ != 0) {
    go_c_ResourceWorldClose(handle_);
  }
}
absl::Status ResourceWorld::UpdateResourceSetWorldRelations(
    const std::string& local_world_id) const {
  return intrinsic::GetStatus([&](go_c_StatusOut status_out) {
    go_c_ResourceWorldUpdateResourceSetWorldRelations(
        handle_, intrinsic::ToStringIn(local_world_id), status_out);
  });
}

std::unique_ptr<ResourceReader> ResourceReader::Create(
    const std::string& hss_address, const std::string& rtr_address) {
  go_c_Handle handle = 0;
  absl::Status status = intrinsic::GetStatus([&](go_c_StatusOut status_out) {
    go_c_ResourceReaderCreate(intrinsic::ToStringIn(hss_address),
                              intrinsic::ToStringIn(rtr_address), &handle,
                              status_out);
  });

  if (!status.ok() || handle == 0) {
    LOG(ERROR) << "Failed to create ResourceReader: " << status.message();
    return nullptr;
  }
  return std::unique_ptr<ResourceReader>(new ResourceReader(handle));
}

ResourceReader::~ResourceReader() {
  if (handle_ != 0) {
    go_c_ResourceReaderClose(handle_);
  }
}

absl::StatusOr<
    std::vector<intrinsic_proto::resources::GeometricResourceInstanceData>>
ResourceReader::GeometricResourceInstanceData(
    const intrinsic_proto::config::ResourceSet& rs) const {
  GeometricResourceSetData proto;

  INTR_RETURN_IF_ERROR(intrinsic::GetStatus([&](go_c_StatusOut status_out) {
    go_c_ResourceReaderGeometricResourceInstanceData(
        handle_, intrinsic::ProtoIn(rs).ToGo(), intrinsic::ProtoOut(&proto),
        status_out);
  }));

  std::vector<intrinsic_proto::resources::GeometricResourceInstanceData> result(
      std::make_move_iterator(proto.mutable_data()->begin()),
      std::make_move_iterator(proto.mutable_data()->end()));

  return result;
}

absl::StatusOr<std::pair<
    std::vector<intrinsic_proto::resources::GeometricResourceInstanceData>,
    intrinsic_proto::world::ObjectWorldUpdates>>
ResourceReader::GetGeometricResourceSetData() const {
  GeometricResourceSetData proto;

  INTR_RETURN_IF_ERROR(intrinsic::GetStatus([&](go_c_StatusOut status_out) {
    go_c_ResourceReaderGeometricResourceSetData(
        handle_, intrinsic::ProtoOut(&proto), status_out);
  }));

  std::vector<intrinsic_proto::resources::GeometricResourceInstanceData> result(
      std::make_move_iterator(proto.mutable_data()->begin()),
      std::make_move_iterator(proto.mutable_data()->end()));

  return std::make_pair(std::move(result), std::move(*proto.mutable_owu()));
}

}  // namespace intrinsic::conductor
