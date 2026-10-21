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

#ifndef INTRINSIC_SCENE_VALIDATE_GO_SCENE_OBJECT_VALIDATION_C_H_
#define INTRINSIC_SCENE_VALIDATE_GO_SCENE_OBJECT_VALIDATION_C_H_

#include "absl/base/attributes.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 on success, absl::StatusCode on failure.
// If failure, *error_message is allocated and must be freed by caller.
ABSL_ATTRIBUTE_UNUSED int intrinsic_scene_object_go_ValidateSceneObject(
    const char* proto_data, int proto_len, char** error_message);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // INTRINSIC_SCENE_VALIDATE_GO_SCENE_OBJECT_VALIDATION_C_H_
