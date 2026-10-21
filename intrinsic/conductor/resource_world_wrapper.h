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

#ifndef INTRINSIC_CONDUCTOR_RESOURCE_WORLD_WRAPPER_H_
#define INTRINSIC_CONDUCTOR_RESOURCE_WORLD_WRAPPER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/conductor/resource_reader_interface.h"

namespace intrinsic::conductor {

class ResourceWorld {
 public:
  static absl::StatusOr<std::unique_ptr<ResourceWorld>> Create(
      const std::string& hss_address, const std::string& ows_address,
      std::unique_ptr<const ResourceReaderInterface> reader);
  ~ResourceWorld();

  ResourceWorld(const ResourceWorld&) = delete;
  ResourceWorld& operator=(const ResourceWorld&) = delete;

  absl::Status UpdateResourceSetWorldRelations(
      const std::string& local_world_id) const;

  const ResourceReaderInterface* GetResourceReader() const {
    return resource_reader_.get();
  }
  uintptr_t GetHandle() const { return handle_; }

 private:
  ResourceWorld(uintptr_t handle,
                std::unique_ptr<const ResourceReaderInterface> resource_reader)
      : handle_(handle), resource_reader_(std::move(resource_reader)) {}
  uintptr_t handle_;
  std::unique_ptr<const ResourceReaderInterface> resource_reader_;
};

class ResourceReader : public ResourceReaderInterface {
 public:
  static std::unique_ptr<ResourceReader> Create(const std::string& hss_address,
                                                const std::string& rtr_address);
  ~ResourceReader() override;

  ResourceReader(const ResourceReader&) = delete;
  ResourceReader& operator=(const ResourceReader&) = delete;

  absl::StatusOr<
      std::vector<intrinsic_proto::resources::GeometricResourceInstanceData>>
  GeometricResourceInstanceData(
      const intrinsic_proto::config::ResourceSet& rs) const override;

  absl::StatusOr<std::pair<
      std::vector<intrinsic_proto::resources::GeometricResourceInstanceData>,
      intrinsic_proto::world::ObjectWorldUpdates>>
  GetGeometricResourceSetData() const override;

  uintptr_t GetHandle() const override { return handle_; }

 private:
  explicit ResourceReader(uintptr_t handle) : handle_(handle) {}
  uintptr_t handle_;
};

}  // namespace intrinsic::conductor

#endif  // INTRINSIC_CONDUCTOR_RESOURCE_WORLD_WRAPPER_H_
