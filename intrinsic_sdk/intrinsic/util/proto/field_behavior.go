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

// Package fieldbehavior provides utilities for working with protobuf field behaviors
// such as google.api.field_behavior (e.g. OUTPUT_ONLY).
package fieldbehavior

import (
	"errors"
	"fmt"
	"slices"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"

	annotationspb "google.golang.org/genproto/googleapis/api/annotations"
	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

// OutputOnlyFieldError is returned when a field annotated as OUTPUT_ONLY is populated.
type OutputOnlyFieldError struct {
	FieldPath string
}

func (e *OutputOnlyFieldError) Error() string {
	return fmt.Sprintf("field %q is output-only and must not be set", e.FieldPath)
}

// errSkipRecursion is a sentinel error used by outputOnlyAction to signal that traversal
// should not descend into the current field's submessages.
var errSkipRecursion = errors.New("skip recursion")

// isOutputOnly reports whether the field descriptor has the
// (google.api.field_behavior) = OUTPUT_ONLY option set.
func isOutputOnly(fd protoreflect.FieldDescriptor) bool {
	opts, ok := fd.Options().(*descriptorpb.FieldOptions)
	if !ok || opts == nil {
		return false
	}
	if !proto.HasExtension(opts, annotationspb.E_FieldBehavior) {
		return false
	}
	behaviors, ok := proto.GetExtension(opts, annotationspb.E_FieldBehavior).([]annotationspb.FieldBehavior)
	if !ok {
		return false
	}
	return slices.Contains(behaviors, annotationspb.FieldBehavior_OUTPUT_ONLY)
}

// outputOnlyAction is called when an OUTPUT_ONLY field is encountered during traversal.
// Returning errSkipRecursion stops traversal from descending into the field without
// treating it as a failure. Returning any other error terminates traversal with that error.
type outputOnlyAction func(m protoreflect.Message, fd protoreflect.FieldDescriptor, fieldPath string) error

// walkPopulatedOutputOnlyFields traverses populated fields across the message tree.
// refl.Range only yields populated fields (explicitly set optionals/messages, non-empty
// lists/maps, or non-zero primitives). When a populated field has (google.api.field_behavior) = OUTPUT_ONLY,
// action is invoked.
func walkPopulatedOutputOnlyFields(refl protoreflect.Message, path string, action outputOnlyAction) error {
	var walkErr error
	refl.Range(func(fd protoreflect.FieldDescriptor, val protoreflect.Value) bool {
		var fieldPath string
		if path != "" {
			fieldPath = fmt.Sprintf("%s.%s", path, fd.Name())
		} else {
			fieldPath = string(fd.Name())
		}

		if isOutputOnly(fd) {
			if err := action(refl, fd, fieldPath); err != nil {
				if errors.Is(err, errSkipRecursion) {
					return true // continue iteration across sibling fields without recursing into this one
				}
				walkErr = err
				return false // abort traversal on real error
			}
		}

		// Recurse into nested submessages if not skipped.
		if fd.Kind() == protoreflect.MessageKind || fd.Kind() == protoreflect.GroupKind {
			// Skip Any payloads to avoid attempting reflection on opaque bytes.
			if fd.Message() != nil && fd.Message().FullName() == "google.protobuf.Any" {
				return true
			}

			if fd.IsList() {
				list := val.List()
				for i := 0; i < list.Len(); i++ {
					elem := list.Get(i)
					elemPath := fmt.Sprintf("%s[%d]", fieldPath, i)
					if err := walkPopulatedOutputOnlyFields(elem.Message(), elemPath, action); err != nil {
						walkErr = err
						return false
					}
				}
			} else if fd.IsMap() {
				if fd.MapValue().Kind() == protoreflect.MessageKind {
					val.Map().Range(func(k protoreflect.MapKey, v protoreflect.Value) bool {
						entryPath := fmt.Sprintf("%s[%s]", fieldPath, k.String())
						if err := walkPopulatedOutputOnlyFields(v.Message(), entryPath, action); err != nil {
							walkErr = err
							return false
						}
						return true
					})
					if walkErr != nil {
						return false
					}
				}
			} else {
				if err := walkPopulatedOutputOnlyFields(val.Message(), fieldPath, action); err != nil {
					walkErr = err
					return false
				}
			}
		}
		return true
	})
	return walkErr
}

// ClearOutputOnly clears all fields annotated with OUTPUT_ONLY from the proto message recursively.
func ClearOutputOnly(m proto.Message) error {
	if m == nil {
		return nil
	}
	return walkPopulatedOutputOnlyFields(m.ProtoReflect(), "", func(m protoreflect.Message, fd protoreflect.FieldDescriptor, _ string) error {
		m.Clear(fd)
		return errSkipRecursion
	})
}

// ValidateNoOutputOnly recursively checks that no fields annotated with OUTPUT_ONLY are populated.
// If any populated field is annotated with OUTPUT_ONLY, an OutputOnlyFieldError is returned.
func ValidateNoOutputOnly(m proto.Message) error {
	if m == nil {
		return nil
	}
	refl := m.ProtoReflect()
	return walkPopulatedOutputOnlyFields(refl, string(refl.Descriptor().Name()), func(_ protoreflect.Message, _ protoreflect.FieldDescriptor, fieldPath string) error {
		return &OutputOnlyFieldError{FieldPath: fieldPath}
	})
}
