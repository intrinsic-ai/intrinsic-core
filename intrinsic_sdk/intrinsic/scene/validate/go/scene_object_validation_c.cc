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

#include "intrinsic/scene/validate/go/scene_object_validation_c.h"

#include <stdlib.h>
#include <string.h>

#include "absl/status/status.h"
#include "intrinsic/scene/proto/v1/scene_object.pb.h"
#include "intrinsic/scene/validate/scene_object_validation.h"

// `string_view` can be used here but then we have to manually add null
// termination character.
static char* CopyString(const std::string& s) {
  char* res = static_cast<char*>(malloc(s.length() + 1));
  if (res == nullptr) {
    return nullptr;
  }
  strncpy(res, s.c_str(), s.length() + 1);
  return res;
}

extern "C" int intrinsic_scene_object_go_ValidateSceneObject(
    const char* proto_data, int proto_len, char** error_message) {
  auto status_compat = [error_message](const absl::Status& s) {
    if (s.ok()) {
      return 0;
    }

    if (error_message != nullptr) {
      *error_message = CopyString(std::string(s.message()));
    }
    return static_cast<int>(s.code());
  };

  if (proto_data == nullptr) {
    return status_compat(absl::InvalidArgumentError("proto_data is null"));
  }
  if (proto_len <= 0) {
    return status_compat(
        absl::InvalidArgumentError("proto_len is not positive"));
  }

  intrinsic_proto::scene_object::v1::SceneObject object;
  if (!object.ParseFromArray(proto_data, proto_len)) {
    return status_compat(
        absl::InvalidArgumentError("Failed to parse SceneObject proto"));
  }

  absl::Status status = intrinsic::scene_object::ValidateSceneObject(object);
  return status_compat(status);
}
