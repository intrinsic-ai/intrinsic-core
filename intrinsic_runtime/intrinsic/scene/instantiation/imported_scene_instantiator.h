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

#ifndef INTRINSIC_SCENE_INSTANTIATION_IMPORTED_SCENE_INSTANTIATOR_H_
#define INTRINSIC_SCENE_INSTANTIATION_IMPORTED_SCENE_INSTANTIATOR_H_

#include <memory>

#include "absl/status/statusor.h"
#include "intrinsic/kubernetes/acl/cc/client_context.h"
#include "intrinsic/scene/proto/v1/imported_scene.pb.h"
#include "intrinsic/scene/proto/v1/scene_object_internal.pb.h"

namespace intrinsic {

// Abstract interface for instantiating an imported scene.
class ImportedSceneInstantiator {
 public:
  virtual ~ImportedSceneInstantiator() = default;

  // Instantiates an imported scene and return the instantiated result. A shared
  // status can be passed in for status reporting.
  virtual absl::StatusOr<
      intrinsic_proto::scene_object::v1::InstantiateImportedSceneResponse>
  InstantiateImportedScene(
      const intrinsic_proto::scene_object::v1::InstantiateImportedSceneRequest&
          request,
      const acl::User& identity,
      std::shared_ptr<
          intrinsic_proto::scene_object::v1::InstantiateImportedSceneStatus>
          status) = 0;
};

}  // namespace intrinsic

#endif  // INTRINSIC_SCENE_INSTANTIATION_IMPORTED_SCENE_INSTANTIATOR_H_
