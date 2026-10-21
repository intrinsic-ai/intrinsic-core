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

// Package traversal provides functions to traverse protobuf messages and process
// fields annotated with dependency metadata.
package traversal

import (
	"fmt"

	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protoreflect"
	"google.golang.org/protobuf/reflect/protoregistry"
	anypb "google.golang.org/protobuf/types/known/anypb"

	"intrinsic/assets/dependencies/utils"
	"intrinsic/util/proto/registryutil"

	fieldmetadatapb "intrinsic/assets/proto/field_metadata_go_proto"
	deppb "intrinsic/assets/proto/v1/dependency_go_proto"
	rdpb "intrinsic/assets/proto/v1/resolved_dependency_go_proto"

	descriptorpb "google.golang.org/protobuf/types/descriptorpb"
)

var (
	resolvedDependencyFullName = (&rdpb.ResolvedDependency{}).ProtoReflect().Descriptor().FullName()
	anyFullName                = (&anypb.Any{}).ProtoReflect().Descriptor().FullName()
)

// FieldContext describes a field within a config message.
type FieldContext struct {
	// Path is the location of the field within the config message, using dots
	// between fields, [i] for list indices, and [key] for map keys (string keys are wrapped in single
	// quotes), e.g. `tools[1].extension.pump_dep` or `deps['left']`.
	Path string
	// Optional reports whether the field is declared with the `optional` keyword.
	Optional bool
}

// ResolvedDependencyFunc is a function that returns a processed ResolvedDependency message
// based on the provided dependency annotation. The input ResolvedDependency
// is the existing value of the field, which will be nil if the
// field is not present in the config.
//
// Implementations may modify the input ResolvedDependency message.
type ResolvedDependencyFunc func(dep *deppb.Dependency, msg *rdpb.ResolvedDependency, field FieldContext) (*rdpb.ResolvedDependency, error)

// ForEachResolvedDependency walks through the message descriptor and finds
// ResolvedDependency fields with dependency annotations. For each such field,
// it executes the provided function and updates the field with the returned
// message. If the field is not present in the original message, it is created
// only if the function returns a non-nil message.
//
// It recursively traverses into message fields if they are known to contain
// ResolvedDependency fields with dependency annotations.
//
// If a file descriptor set is provided, this function will also traverse into Any fields that can be
// unpacked using the descriptors in the set.
//
// Note: recursive message definitions are not fully traversed. If a message type contains a field
// of the same type (directly or indirectly), the recursion will stop at the first re-occurrence of
// that type in the current traversal path.
func ForEachResolvedDependency(config *anypb.Any, fds *descriptorpb.FileDescriptorSet, fn ResolvedDependencyFunc) error {
	if config == nil {
		return nil
	}
	if fds == nil {
		return nil
	}
	types, err := registryutil.NewTypesFromFileDescriptorSet(fds)
	if err != nil {
		return fmt.Errorf("failed to create types from file descriptor set: %w", err)
	}
	if _, err := processAny(config.ProtoReflect(), types, make(map[protoreflect.MessageDescriptor]struct{}), fn, FieldContext{}); err != nil {
		return err
	}
	return nil
}

// forEachResolvedDependency walks through the message descriptor and finds
// ResolvedDependency fields with dependency annotations and executes the
// provided function. The boolean return value indicates whether any
// ResolvedDependency fields within this message (or its sub-messages) were
// updated. parent describes the field m within the root config message.
func forEachResolvedDependency(m protoreflect.Message, types *protoregistry.Types, stack map[protoreflect.MessageDescriptor]struct{}, fn ResolvedDependencyFunc, parent FieldContext) (bool, error) {
	md := m.Descriptor()
	if _, ok := stack[md]; ok {
		return false, nil
	}
	stack[md] = struct{}{}
	defer delete(stack, md)

	updated := false

	for i := 0; i < md.Fields().Len(); i++ {
		field := md.Fields().Get(i)

		// 1. Determine the MessageDescriptor of the field (or map value).
		var msgDesc protoreflect.MessageDescriptor
		if field.IsMap() {
			if field.MapValue().Kind() != protoreflect.MessageKind {
				continue
			}
			msgDesc = field.MapValue().Message()
		} else {
			if field.Kind() != protoreflect.MessageKind && field.Kind() != protoreflect.GroupKind {
				continue
			}
			msgDesc = field.Message()
		}

		// 2. Determine the action to perform on the message.
		//    The boolean input parameter indicates whether the message is already
		// 		present in the parent message. The FieldContext input parameter
		// 		provides additional context to the message. The boolean return value indicates
		// 		whether the message (or any of its sub-messages) was updated.
		var action func(protoreflect.Message, bool, FieldContext) (bool, error)

		// Default to creating ResolvedDependency fields if missing.
		createIfMissing := true

		if msgDesc.FullName() == resolvedDependencyFullName {
			if dep := getDependencyAnnotation(field); dep != nil {
				action = func(m protoreflect.Message, present bool, info FieldContext) (bool, error) {
					var resolvedDep *rdpb.ResolvedDependency
					if present {
						// The message is likely a dynamic proto, so we need to marshal and unmarshal.
						b, err := proto.Marshal(m.Interface())
						if err != nil {
							return false, err
						}
						resolvedDep = &rdpb.ResolvedDependency{}
						if err := proto.Unmarshal(b, resolvedDep); err != nil {
							return false, err
						}
					}

					res, err := fn(dep, resolvedDep, info)
					if err != nil {
						return false, err
					}
					if res == nil {
						return false, nil
					}

					// Write back changes to the original message.
					b, err := proto.Marshal(res)
					if err != nil {
						return false, err
					}
					if err := proto.Unmarshal(b, m.Interface()); err != nil {
						return false, err
					}
					return true, nil
				}
			}
		} else if msgDesc.FullName() == anyFullName {
			if types != nil {
				// Only process Any fields if they are already present.
				createIfMissing = false

				action = func(m protoreflect.Message, present bool, info FieldContext) (bool, error) {
					return processAny(m, types, stack, fn, info)
				}
			}
		} else if utils.HasResolvedDependency(msgDesc, utils.WithDependencyAnnotation()) {
			action = func(m protoreflect.Message, present bool, info FieldContext) (bool, error) {
				return forEachResolvedDependency(m, types, stack, fn, info)
			}
		}

		if action == nil {
			continue
		}

		// 3. Apply the action to the field (Singular, List, or Map).

		// Handle Oneof: skip if the oneof is not set to the current field.
		if oneof := field.ContainingOneof(); oneof != nil {
			if m.WhichOneof(oneof) != field {
				continue
			}
		}

		// Built only for fields that are actually visited.
		fieldInfo := FieldContext{
			Path:     joinField(parent.Path, field),
			Optional: field.HasOptionalKeyword(),
		}

		if field.IsMap() {
			if !m.Has(field) {
				continue
			}
			mapVal := m.Get(field).Map()
			var rangeErr error
			mapVal.Range(func(k protoreflect.MapKey, v protoreflect.Value) bool {
				elemInfo := fieldInfo
				elemInfo.Path = fieldInfo.Path + "[" + formatMapKey(k) + "]"
				u, err := action(v.Message(), true, elemInfo)
				if err != nil {
					rangeErr = err
					return false
				}
				if u {
					updated = true
				}
				return true
			})
			if rangeErr != nil {
				return false, rangeErr
			}
		} else if field.IsList() {
			if !m.Has(field) {
				continue
			}
			list := m.Mutable(field).List()
			for i := 0; i < list.Len(); i++ {
				elemInfo := fieldInfo
				elemInfo.Path = fmt.Sprintf("%s[%d]", fieldInfo.Path, i)
				u, err := action(list.Get(i).Message(), true, elemInfo)
				if err != nil {
					return false, err
				}
				if u {
					updated = true
				}
			}
		} else {
			// Singular field.
			initiallyPresent := m.Has(field)
			if !initiallyPresent && !createIfMissing {
				continue
			}

			var childMsg protoreflect.Message
			if initiallyPresent {
				childMsg = m.Mutable(field).Message()
			} else {
				childMsg = m.NewField(field).Message()
			}

			u, err := action(childMsg, initiallyPresent, fieldInfo)
			if err != nil {
				return false, err
			}
			if u {
				if !initiallyPresent {
					m.Set(field, protoreflect.ValueOfMessage(childMsg))
				}
				updated = true
			}
		}
	}
	return updated, nil
}

func processAny(m protoreflect.Message, types *protoregistry.Types, stack map[protoreflect.MessageDescriptor]struct{}, fn ResolvedDependencyFunc, parent FieldContext) (bool, error) {
	// The message is likely a dynamic proto, so we need to marshal and unmarshal.
	b, err := proto.Marshal(m.Interface())
	if err != nil {
		return false, err
	}
	anyMsg := &anypb.Any{}
	if err := proto.Unmarshal(b, anyMsg); err != nil {
		return false, err
	}

	msgType, err := types.FindMessageByName(anyMsg.MessageName())
	if err != nil {
		return false, nil
	}
	msg := msgType.New().Interface()
	if err := anyMsg.UnmarshalTo(msg); err != nil {
		return false, nil
	}

	updated, err := forEachResolvedDependency(msg.ProtoReflect(), types, stack, fn, parent)
	if err != nil {
		return false, err
	}
	if !updated {
		return false, nil
	}

	if err := anyMsg.MarshalFrom(msg); err != nil {
		return false, err
	}

	b, err = proto.Marshal(anyMsg)
	if err != nil {
		return false, err
	}
	if err := proto.Unmarshal(b, m.Interface()); err != nil {
		return false, err
	}
	return true, nil
}

// getDependencyAnnotation returns the dependency annotation for a field, if present.
func getDependencyAnnotation(field protoreflect.FieldDescriptor) *deppb.Dependency {
	options, ok := field.Options().(*descriptorpb.FieldOptions)
	if !ok {
		return nil
	}
	if !proto.HasExtension(options, fieldmetadatapb.E_FieldMetadata) {
		return nil
	}
	fieldMetadata := proto.GetExtension(options, fieldmetadatapb.E_FieldMetadata).(*fieldmetadatapb.FieldMetadata)
	return fieldMetadata.GetDependency()
}

func joinField(path string, fd protoreflect.FieldDescriptor) string {
	if path == "" {
		return fd.TextName()
	}
	return path + "." + fd.TextName()
}

func formatMapKey(k protoreflect.MapKey) string {
	if s, ok := k.Interface().(string); ok {
		// Keys are assumed not to contain single quotes or backslashes, so no escaping is done.
		return "'" + s + "'"
	}
	return fmt.Sprint(k.Interface())
}
