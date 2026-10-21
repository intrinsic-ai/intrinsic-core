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

// Package sceneobjectvalidation is a go wrapper for intrinsic/scene/validate/scene_object_validation.h
package sceneobjectvalidation

import (
	"fmt"
	"unsafe"

	"google.golang.org/protobuf/proto"

	sopb "intrinsic/scene/proto/v1/scene_object_go_proto"
)

// #include <stdlib.h>
// #include "intrinsic/scene/validate/go/scene_object_validation_c.h"
import "C"

// ValidateSceneObject Returns OK if this is a well formed scene object.
func ValidateSceneObject(object *sopb.SceneObject) error {
	data, err := proto.Marshal(object)
	if err != nil {
		return fmt.Errorf("failed to marshal SceneObject: %w", err)
	}

	var c_data *C.char
	if len(data) > 0 {
		c_data = (*C.char)(unsafe.Pointer(&data[0]))
	}
	c_len := C.int(len(data))

	var c_err_msg *C.char
	defer func() {
		if c_err_msg != nil {
			// We are responsible for freeing up the memory across language boundary.
			C.free(unsafe.Pointer(c_err_msg))
		}
	}()

	status_code := C.intrinsic_scene_object_go_ValidateSceneObject(c_data, c_len, &c_err_msg)
	if status_code != 0 {
		err_str := "unknown error"
		if c_err_msg != nil {
			err_str = C.GoString(c_err_msg)
		}
		return fmt.Errorf("validation failed with code %d: %s", status_code, err_str)
	}
	return nil
}
