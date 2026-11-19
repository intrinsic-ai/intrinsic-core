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

#ifndef INTRINSIC_WORLD_SERVICE_OBJECTS_WORLD_COMPARATOR_H_
#define INTRINSIC_WORLD_SERVICE_OBJECTS_WORLD_COMPARATOR_H_

#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "intrinsic/world/proto/object_world_refs.pb.h"
#include "intrinsic/world/proto/object_world_service.pb.h"

namespace intrinsic {
namespace object_world {

class WorldComparator {
 public:
  using Frame = intrinsic_proto::world::Frame;
  using Object = intrinsic_proto::world::Object;
  using ObjectReference = intrinsic_proto::world::ObjectReference;

  static absl::StatusOr<WorldComparator> Compare(
      const intrinsic_proto::world::World& base_world,
      const intrinsic_proto::world::World& changed_world,
      intrinsic_proto::world::CompareWorldsRequest::IdentificationMode id_mode);

  const std::vector<ObjectReference>& GetAdded() const;
  const std::vector<Object>& GetRemoved() const;
  const std::vector<std::pair<ObjectReference, std::string>>& GetModified()
      const;
  const std::vector<Frame>& GetAddedFrames() const;
  const std::vector<Frame>& GetRemovedFrames() const;
  const std::vector<std::pair<Frame, std::string>>& GetModifiedFrames() const;

 private:
  WorldComparator() = default;

  absl::Status CompareObjectFrames(
      const Object& first_object, const Object& second_object,
      intrinsic_proto::world::CompareWorldsRequest::IdentificationMode id_mode);
  std::vector<ObjectReference> added_;
  std::vector<Object> removed_;
  std::vector<std::pair<ObjectReference, std::string>> modified_;
  std::vector<Frame> added_frames_;
  std::vector<Frame> removed_frames_;
  std::vector<std::pair<Frame, std::string>> modified_frames_;
};

}  // namespace object_world
}  // namespace intrinsic

#endif  // INTRINSIC_WORLD_SERVICE_OBJECTS_WORLD_COMPARATOR_H_
