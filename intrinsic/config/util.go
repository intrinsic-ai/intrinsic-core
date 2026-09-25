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

// Package util defines common functions for working with PPR protos.
package util

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"os"

	"intrinsic/kubernetes/workcell_spec/workcellspec"

	"cloud.google.com/go/firestore"
	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/reflect/protopath"
	"google.golang.org/protobuf/reflect/protorange"
	"google.golang.org/protobuf/reflect/protoreflect"
	"gopkg.in/yaml.v3"

	anywpb "intrinsic/config/proto/any_wrapper_go_proto"
	datafilespb "intrinsic/config/proto/data_files_go_proto"

	anypb "google.golang.org/protobuf/types/known/anypb"
)

const (
	typeURLAnyWrapper = "type.googleapis.com/intrinsic_proto.config.internal.AnyWrapper"
)

func wrapAnyProto(p *anypb.Any) (*anypb.Any, error) {
	if p.GetTypeUrl() == typeURLAnyWrapper {
		return p, nil
	}
	w := &anywpb.AnyWrapper{
		TypeUrl: p.GetTypeUrl(),
		Value:   p.GetValue(),
	}
	r, err := anypb.New(w)
	if err != nil {
		return nil, err
	}
	return r, nil
}

func unwrapAnyProto(p *anypb.Any) (*anypb.Any, error) {
	// Only unwrap protos that have been wrapped into our wrapper message.
	if p.GetTypeUrl() != typeURLAnyWrapper {
		return p, nil
	}
	w := &anywpb.AnyWrapper{}
	if err := p.UnmarshalTo(w); err != nil {
		return nil, err
	}
	r := &anypb.Any{
		TypeUrl: w.GetTypeUrl(),
		Value:   w.GetValue(),
	}
	return r, nil
}

func transformAnyProtos(m proto.Message, transform func(*anypb.Any) (*anypb.Any, error)) error {
	if m == nil {
		return nil
	}
	if anym, ok := m.(*anypb.Any); ok {
		// Another null-check since the interface may have been holding a nil
		// pointer but wasn't nil itself. The pointer to the concrete type we have
		// here can be safely checked for nil.
		if anym == nil {
			return nil
		}
		newm, err := transform(anym)
		if err != nil {
			return err
		}
		anym.TypeUrl = newm.TypeUrl
		anym.Value = newm.Value
		return nil
	}
	return protorange.Range(m.ProtoReflect(), func(p protopath.Values) error {
		last := p.Index(-1)
		fd := last.Step.FieldDescriptor()
		// Ignore all fields which don't contain a Message.
		if fd == nil || fd.Kind() != protoreflect.MessageKind {
			return nil
		}
		// Handle list case.
		if fd.IsList() {
			for i := 0; i < last.Value.List().Len(); i++ {
				a, ok := last.Value.List().Get(i).Message().Interface().(*anypb.Any)
				if !ok {
					return nil
				}
				t, err := transform(a)
				if err != nil {
					return fmt.Errorf("error in list field '%v' item %d: %v", fd.Name(), i, err)
				}
				last.Value.List().Set(i, protoreflect.ValueOfMessage(t.ProtoReflect()))
			}
			return nil
		}
		// Handle map case.
		if fd.IsMap() {
			if fd.MapValue().Kind() != protoreflect.MessageKind {
				return nil
			}
			var errCaptured error
			last.Value.Map().Range(func(k protoreflect.MapKey, v protoreflect.Value) bool {
				a, ok := v.Message().Interface().(*anypb.Any)
				if !ok {
					return false
				}
				t, err := transform(a)
				if err != nil {
					errCaptured = fmt.Errorf("error in map field '%v' key '%v': %v", fd.Name(), k, err)
					return false
				}
				last.Value.Map().Set(k, protoreflect.ValueOfMessage(t.ProtoReflect()))
				return true
			})
			if errCaptured != nil {
				return errCaptured
			}
			return nil
		}
		// Handle message field case.
		a, ok := last.Value.Message().Interface().(*anypb.Any)
		if !ok {
			return nil
		}
		t, err := transform(a)
		if err != nil {
			return fmt.Errorf("error in message field '%v': %v", fd.Name(), err)
		}
		beforeLast := p.Index(-2)
		switch last.Step.Kind() {
		case protopath.FieldAccessStep:
			m := beforeLast.Value.Message()
			m.Set(fd, protoreflect.ValueOfMessage(t.ProtoReflect()))
		default:
			// ListIndexStep and MapIndexStep are handled separately above.
			// AnyExpandStep will be prevented by the above logic.
			// All other step cases indicate a serious error.
			return fmt.Errorf("Unexpected Step case happened during proto traversal %v", last.Step.Kind())
		}

		return nil
	})
}

// WrapAnyProtos wraps all Any protos within the provided message into a custom proto
// 'AnyWrapper' with identical fields in order to avoid that the Any protos are unpacked
// by the proto <-> JSON conversion.
func WrapAnyProtos(m proto.Message) error {
	return transformAnyProtos(m, wrapAnyProto)
}

// UnwrapAnyProtos reverses the [WrapAnyProtos] changes.
func UnwrapAnyProtos(m proto.Message) error {
	return transformAnyProtos(m, unwrapAnyProto)
}

// SnapToProto converts a Firestore DocumentSnapshot to the given proto.
func SnapToProto(d *firestore.DocumentSnapshot, m proto.Message) error {
	b, err := json.Marshal(d.Data())
	if err != nil {
		return fmt.Errorf("marshal proto message: %v", err)
	}
	// DiscardUnknown provides forward compatibility when fields got added.
	opts := protojson.UnmarshalOptions{DiscardUnknown: true}
	if err := opts.Unmarshal(b, m); err != nil {
		return fmt.Errorf("unmarshal json to proto: %v", err)
	}
	if err := UnwrapAnyProtos(m); err != nil {
		return fmt.Errorf("unwrapping protobuf.Any protos: %v", err)
	}
	return nil
}

// ReadYAMLDataFiles reads FileReference yaml files and creates a
// datafilespb.DataFiles list from them.  The returned value is nil if the list is
// empty.
func ReadYAMLDataFiles(filePaths []string) (*datafilespb.DataFiles, error) {
	dataFiles := &datafilespb.DataFiles{}
	for _, p := range filePaths {
		// filePaths often is created by strings.Split, which creates a slice with
		// one empty string for an empty string argument.
		if p == "" {
			continue
		}

		refYaml, err := os.ReadFile(p)
		if err != nil {
			return dataFiles, fmt.Errorf("Failed to read file reference %q: %w", p, err)
		}

		dec := yaml.NewDecoder(bytes.NewBuffer(refYaml))
		var obj map[string]any
		for {
			if err := dec.Decode(&obj); err == io.EOF {
				break
			} else if err != nil {
				return dataFiles, fmt.Errorf("Failed to decode file reference %q: %w", p, err)
			}
			fileRef, err := workcellspec.UnmarshalFileReference(obj)
			if err != nil {
				return dataFiles, fmt.Errorf("Failed to unmarshal file reference %q: %w", p, err)
			}
			dataFiles.Files = append(dataFiles.GetFiles(), fileRef)
		}
	}
	return dataFiles, nil
}
